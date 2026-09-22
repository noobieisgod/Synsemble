#include "providers/provider_gateway.h"
#include "providers/network_diagnostics.h"

#include <functional>

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>

#include "core/response_parser.h"
#include "services/credential_store.h"

namespace amt {

namespace {

struct NetworkResult {
    int statusCode = 0;
    QByteArray body;
    QString error;
    int networkErrorCode = 0;
    int sslErrorCount = 0;
    QString tlsCategory;
    bool requestSent = false;
    QList<QNetworkReply::RawHeaderPair> headers;
    ProviderDeliveryOutcome deliveryOutcome = ProviderDeliveryOutcome::DefiniteFailure;
};

using JsonRequestHandler = std::function<NetworkResult(const QNetworkRequest &,
                                                       const QByteArray &,
                                                       const QByteArray &)>;

void captureSslErrors(QNetworkReply *reply, NetworkResult *result)
{
    QObject::connect(reply, &QNetworkReply::sslErrors, reply, [result](const QList<QSslError> &errors) {
        result->sslErrorCount += static_cast<int>(errors.size());
        result->tlsCategory = tlsFailureCategory(errors);
    });
}

// HTTP rejections take precedence over Qt's corresponding network-error enum.
ProviderDeliveryOutcome classifyDelivery(const NetworkResult &result, bool generationPost)
{
    if (result.statusCode >= 200 && result.statusCode < 300 && result.error.isEmpty())
        return ProviderDeliveryOutcome::Succeeded;
    if (!generationPost) return ProviderDeliveryOutcome::DefiniteFailure;
    switch (result.statusCode) {
    case 400: case 401: case 403: case 404: case 413: case 422: case 429:
        return ProviderDeliveryOutcome::DefiniteFailure;
    }
    if (!result.requestSent && result.statusCode == 0) {
        switch (result.networkErrorCode) {
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::SslHandshakeFailedError:
            return ProviderDeliveryOutcome::DefiniteFailure;
        case QNetworkReply::OperationCanceledError:
            return result.error == "timeout" ? ProviderDeliveryOutcome::OutcomeUnknown
                                             : ProviderDeliveryOutcome::Cancelled;
        }
    }
    return ProviderDeliveryOutcome::OutcomeUnknown;
}

QString safeFailureDetail(const NetworkResult &result)
{
    switch (result.statusCode) {
    case 400: case 422: return "request rejected; check model and task configuration";
    case 401: return "authentication rejected; replace the saved provider key";
    case 403: return "access denied; check provider and model permissions";
    case 404: return "model or endpoint unavailable; check the selected model";
    case 413: return "request too large; reduce task or attachment size";
    case 429: return "rate limit or quota reached; check provider limits before another run";
    }
    if (result.error == "timeout") return "connection timed out";
    switch (result.networkErrorCode) {
    case QNetworkReply::SslHandshakeFailedError:
        return "TLS connection failed" + (result.tlsCategory.isEmpty() ? QString{} : ": " + result.tlsCategory);
    case QNetworkReply::HostNotFoundError: return "server address could not be resolved; check DNS/network";
    case QNetworkReply::ConnectionRefusedError: return "connection refused; check network access";
    case QNetworkReply::OperationCanceledError: return "request interrupted";
    case QNetworkReply::RemoteHostClosedError: return "connection closed before a complete response";
    default: return result.statusCode >= 500 ? "server or proxy failure" : "unconfirmed network response";
    }
}

NetworkResult performJsonRequest(QNetworkAccessManager &manager,
                                 const QNetworkRequest &request,
                                 const QByteArray &body,
                                 const QByteArray &method = "POST",
                                 const JsonRequestHandler &handler = {})
{
    if (handler) {
        return handler(request, body, method);
    }

    QNetworkRequest safeRequest(request);
    safeRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = nullptr;
    if (method == "POST") {
        reply = manager.post(safeRequest, body);
    } else {
        reply = manager.sendCustomRequest(safeRequest, method, body);
    }

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeoutTimer, &QTimer::timeout, reply, [reply, &timedOut]() {
        timedOut = true;
        reply->abort();
    });
    NetworkResult result;
    captureSslErrors(reply, &result);
    QObject::connect(reply, &QNetworkReply::requestSent, reply, [&result]() { result.requestSent = true; });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeoutTimer.start(90000);
    loop.exec();

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.headers = reply->rawHeaderPairs();
    if (reply->error() != QNetworkReply::NoError) {
        result.networkErrorCode = static_cast<int>(reply->error());
        result.error = timedOut
            ? "timeout"
            : "network";
    }
    result.deliveryOutcome = classifyDelivery(result, method == "POST");
    reply->deleteLater();
    return result;
}

NetworkResult performMultipartRequest(QNetworkAccessManager &manager,
                                      const QNetworkRequest &request,
                                      QHttpMultiPart *multipart)
{
    QNetworkRequest safeRequest(request);
    safeRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = manager.post(safeRequest, multipart);
    multipart->setParent(reply);

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeoutTimer, &QTimer::timeout, reply, [reply, &timedOut]() {
        timedOut = true;
        reply->abort();
    });
    NetworkResult result;
    captureSslErrors(reply, &result);
    QObject::connect(reply, &QNetworkReply::requestSent, reply, [&result]() { result.requestSent = true; });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeoutTimer.start(90000);
    loop.exec();

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.headers = reply->rawHeaderPairs();
    if (reply->error() != QNetworkReply::NoError) {
        result.networkErrorCode = static_cast<int>(reply->error());
        result.error = timedOut
            ? "timeout"
            : "network";
    }
    reply->deleteLater();
    return result;
}

NetworkResult performDeviceRequest(QNetworkAccessManager &manager,
                                   const QNetworkRequest &request,
                                   QIODevice *body)
{
    QNetworkRequest safeRequest(request);
    safeRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = manager.post(safeRequest, body);

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeoutTimer, &QTimer::timeout, reply, [reply, &timedOut]() {
        timedOut = true;
        reply->abort();
    });
    NetworkResult result;
    captureSslErrors(reply, &result);
    QObject::connect(reply, &QNetworkReply::requestSent, reply, [&result]() { result.requestSent = true; });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeoutTimer.start(90000);
    loop.exec();

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.headers = reply->rawHeaderPairs();
    if (reply->error() != QNetworkReply::NoError) {
        result.networkErrorCode = static_cast<int>(reply->error());
        result.error = timedOut
            ? "timeout"
            : "network";
    }
    reply->deleteLater();
    return result;
}

QString headerValue(const QList<QNetworkReply::RawHeaderPair> &headers, const QByteArray &key)
{
    for (const auto &header : headers) {
        if (header.first.compare(key, Qt::CaseInsensitive) == 0) {
            return QString::fromUtf8(header.second).trimmed();
        }
    }
    return {};
}

QString providerRequestFailure(const ProviderRequest &request,
                               const QString &providerName,
                               const QString &endpointType,
                               const NetworkResult &result,
                               const QString &stage = "failed after sending",
                               const QStringList &extraDetails = {})
{
    Q_UNUSED(request);
    Q_UNUSED(stage);
    Q_UNUSED(extraDetails);
    return QString("%1 attachment preparation failed (%2): %3. No generation request was sent; an upload may have been created.")
        .arg(providerName, endpointType, safeFailureDetail(result));
}

QString joinedText(const QStringList &blocks)
{
    QStringList visible;
    for (const QString &block : blocks) {
        if (!block.trimmed().isEmpty()) {
            visible.append(block.trimmed());
        }
    }
    return visible.join("\n\n");
}

QString openAiVisibleText(const QJsonObject &json)
{
    QStringList blocks;
    for (const QJsonValue &itemValue : json.value("output").toArray()) {
        const QJsonObject item = itemValue.toObject();
        const QString itemType = item.value("type").toString();
        QJsonArray content;
        if (itemType == "output_text" || itemType == "refusal") {
            content.append(item);
        } else if (itemType == "message") {
            content = item.value("content").toArray();
        }
        for (const QJsonValue &blockValue : content) {
            const QJsonObject block = blockValue.toObject();
            const QString type = block.value("type").toString();
            if (type == "output_text" && block.value("text").isString()) {
                blocks.append(block.value("text").toString());
            } else if (type == "refusal" && block.value("refusal").isString()) {
                blocks.append(QString("Provider refusal: %1").arg(block.value("refusal").toString()));
            }
        }
    }
    return joinedText(blocks);
}

QString geminiVisibleText(const QJsonObject &json)
{
    const QJsonArray candidates = json.value("candidates").toArray();
    if (candidates.isEmpty()) {
        return {};
    }
    QStringList blocks;
    for (const QJsonValue &partValue : candidates.first().toObject()
             .value("content").toObject().value("parts").toArray()) {
        const QJsonObject part = partValue.toObject();
        if (!part.value("thought").toBool(false) && part.value("text").isString()) {
            blocks.append(part.value("text").toString());
        }
    }
    return joinedText(blocks);
}

QString anthropicVisibleText(const QJsonObject &json)
{
    QStringList blocks;
    for (const QJsonValue &blockValue : json.value("content").toArray()) {
        const QJsonObject block = blockValue.toObject();
        if (block.value("type").toString() == "text" && block.value("text").isString()) {
            blocks.append(block.value("text").toString());
        }
    }
    return joinedText(blocks);
}

int estimatedTokenCount(const QString &text)
{
    return text.isEmpty() ? 0 : qMax(1, (text.size() + 3) / 4);
}

QString phasePurposeText(Phase phase)
{
    switch (phase) {
    case Phase::Research:
        return "Research phase: Work independently. Gather evidence, identify uncertainties, and note useful constraints. Do not collaborate, create the final result, issue a plan, make a QC ruling, or make a final decision.";
    case Phase::Planning:
        return "Planning phase: Participants contribute concise new constraints first. The Lead Planner then owns the authoritative consolidated plan. Do not create the final result.";
    case Phase::Execution:
        return "Execution phase: Participants contribute concise new material first. The Lead Executioner then creates or patches the authoritative artifact directly. Other seats must not present a competing artifact.";
    case Phase::QualityControl:
        return "Quality Control phase: Review and verify the current artifact without rewriting it. Participants contribute new evidence first. The Lead Quality Control reviewer consolidates all blocking corrections, optional improvements, open findings, and resolved findings in one review.";
    case Phase::Present:
        return "Final Decision phase: Review the artifact and discussion. Only the Final Decision Maker may approve, revise, stop, or issue final delivery rulings.";
    default:
        return {};
    }
}

QString stablePromptText()
{
    return QStringList{
        "Stable behavior:",
        "You are one seat in a multi-agent meeting table. Obey the current phase before any prior conversation history.",
        "Respond in English unless the user explicitly asks for another language.",
        "Do not produce final deliverables before Execution. Do not overstep your seat authority.",
        "Research policy: use online research only when research tools are available and the task benefits from current, factual, niche, legal, technical, market, scientific, product, policy, or news information.",
        "Every substantive response must include exactly one short line: Research used: {brief source/tool summary}. If research tools are unavailable, write: Research used: none available. If tools are available but not needed, write: Research used: not needed.",
        "Quality rules: be concise and direct. Avoid filler, flattery, motivational language, generic AI-sounding structure, and formulaic ceremony. Do not use em dashes. Think through uncertainty and contradictions silently, then respond with the final answer only.",
        "Convergence rules: add only new information, do not restate settled facts, do not repeat authority disclaimers or labels such as support notes, and keep participant contributions substantially shorter than the authoritative phase output. Resolved findings stay resolved unless new contradictory evidence appears.",
        "If evaluating or grading, define a rubric first and grade against it with justification. If MLA citations are requested, use current MLA guidance and cite in MLA style.",
        "History, roster, artifacts, and attachments are context. User instructions and current phase rules take priority."
    }.join("\n");
}

QString researchToolText(const ProviderRequest &request)
{
    if (request.provider == ProviderKind::Gemini && request.phase == Phase::Research) {
        return "Research tools available this turn: Gemini Google Search.";
    }
    return "Research tools available this turn: none available.";
}

QString leadAuthorityText(Phase phase, const QString &role)
{
    if (phase == Phase::Planning && role == "Lead Planner") {
        return "Lead authority: You are the Lead Planner. Produce the authoritative consolidated plan after participant input. Other seats may identify a concrete, evidence-backed flaw but must not present a competing plan.";
    }
    if (phase == Phase::Execution && role == "Lead Executioner") {
        return "Lead authority: You are the Lead Executioner. Produce the authoritative artifact directly, patching the current draft when revising. Other seats must not present their own answer as the official artifact.";
    }
    if (phase == Phase::QualityControl && role == "Lead Quality Control") {
        return "Lead authority: You are the Lead Quality Control reviewer. Consolidate every detectable blocking issue in one review, preserve resolved findings, separate optional improvements, and rule whether revision is required. Other seats may provide evidence but must not issue the final QC ruling.";
    }
    if (phase == Phase::Present && role == "Final Decision Maker") {
        return "Lead authority: You are the Final Decision Maker. End with exactly one parseable ruling line: FINAL_RULING: APPROVE, FINAL_RULING: REVISE, or FINAL_RULING: STOP.";
    }
    if (role == "Final Decision Maker") {
        return "Lead authority: You are the Final Decision Maker for this phase only. Resolve phase disputes with PROCEED, REVISE, or STOP. Do not approve the final artifact before Present.";
    }
    return "Authority reminder: Do not overstep your role. Defer to the phase lead unless you identify a concrete, evidence-backed flaw.";
}

QString buildPromptText(const ProviderRequest &request)
{
    QStringList lines;

    lines << stablePromptText();
    lines << "";
    lines << "Dynamic context:";

    const QString displayName = request.prompt.value("seat_display_name").toString();
    const QString modelName = request.prompt.value("model_display_name").toString();
    if (!displayName.isEmpty()) {
        lines << QString("Seat: %1%2")
                    .arg(displayName, modelName.isEmpty() ? "" : QString(", powered by %1").arg(modelName));
    }

    lines << QString("Table: %1").arg(request.prompt.value("table_title").toString());
    lines << QString("Phase: %1").arg(toString(request.phase));
    const QString role = request.prompt.value("role").toString();
    lines << QString("Seat role: %1").arg(role);
    lines << researchToolText(request);

    const QString purpose = phasePurposeText(request.phase);
    if (!purpose.isEmpty()) {
        lines << purpose;
    }
    lines << leadAuthorityText(request.phase, role);

    const auto rosterArray = request.prompt.value("participant_roster").toArray();
    if (!rosterArray.isEmpty()) {
        QStringList rosterEntries;
        for (const auto &p : rosterArray) {
            const auto obj = p.toObject();
            rosterEntries << QString("- %1 (%2, %3)")
                                .arg(obj.value("name").toString(),
                                     obj.value("model").toString(),
                                     obj.value("role").toString());
        }
        lines << "Other participants at this table:";
        lines << rosterEntries.join("\n");
    }

    const QString latestUser = request.prompt.value("latest_user_message").toString().trimmed();
    if (!latestUser.isEmpty()) {
        lines << QString("Current user objective: %1").arg(latestUser);
    }

    const QString instruction = request.prompt.value("instruction").toString().trimmed();
    if (!instruction.isEmpty()) {
        lines << QString("Instruction: %1").arg(instruction);
    }

    // Token discipline
    lines << "Keep the response compact enough to leave room for the user's task. Preserve necessary detail and avoid repetition.";

    const QString artifactSummary = request.prompt.value("current_artifact_summary").toString().trimmed();
    if (!artifactSummary.isEmpty()) {
        lines << QString("Current artifact summary: %1").arg(artifactSummary);
    }
    const QString artifactContent = request.prompt.value("current_artifact_content").toString().trimmed();
    if (!artifactContent.isEmpty()) {
        lines << "Current artifact content:";
        lines << artifactContent;
    }

    const auto transcriptHistory = request.prompt.value("transcript_history").toArray();
    if (!transcriptHistory.isEmpty()) {
        lines << "Conversation history:";
        for (const auto &entryValue : transcriptHistory) {
            const auto entry = entryValue.toObject();
            lines << QString("- %1: %2").arg(entry.value("speaker").toString(), entry.value("content").toString());
        }
    }

    return lines.join("\n");
}

QString detectMimeType(const QString &filePath)
{
    const QMimeType mimeType = QMimeDatabase().mimeTypeForFile(filePath, QMimeDatabase::MatchContent);
    if (mimeType.isValid() && !mimeType.name().isEmpty()) {
        return mimeType.name();
    }
    return "application/octet-stream";
}

QString readTextAttachment(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const qint64 fileSize = file.size();
    const qint64 maxRead = 32768;
    const QByteArray data = file.read(maxRead);
    QString text = QString::fromUtf8(data);
    if (fileSize > maxRead) {
        text += QString("\n\n[Content truncated. Original file is %1 bytes]").arg(fileSize);
    }
    return text;
}

bool isAnthropicFileBlockMime(const QString &mimeType)
{
    return mimeType.startsWith("image/") || mimeType == "application/pdf";
}

QString geminiHandle(const QString &uri, const QString &mimeType)
{
    return QString("%1|%2").arg(uri, mimeType);
}

QString geminiUriFromHandle(const QString &handle)
{
    const QStringList parts = handle.split('|');
    if (parts.size() >= 3) {
        return parts.at(1);
    }
    return parts.value(0);
}

QString geminiMimeFromHandle(const QString &handle)
{
    const QStringList parts = handle.split('|');
    if (parts.size() >= 3) {
        return parts.at(2);
    }
    return parts.value(1);
}

QString geminiNameFromHandle(const QString &handle)
{
    const QStringList parts = handle.split('|');
    return parts.size() >= 3 ? parts.at(0) : QString();
}

QString geminiHandle(const QString &name, const QString &uri, const QString &mimeType)
{
    return QString("%1|%2|%3").arg(name, uri, mimeType);
}

QUrl geminiApiUrl(const QString &path, const QString &apiKey)
{
    QUrl url(QString("https://generativelanguage.googleapis.com%1").arg(path));
    QUrlQuery query;
    query.addQueryItem("key", apiKey);
    url.setQuery(query);
    return url;
}

QJsonObject pollGeminiFileReady(QNetworkAccessManager &manager, const QString &apiKey, QJsonObject fileObject)
{
    const QString fileName = fileObject.value("name").toString();
    if (fileName.isEmpty()) {
        return fileObject;
    }

    QString stateName = fileObject.value("state").toObject().value("name").toString();
    if (stateName.isEmpty() || stateName == "ACTIVE") {
        return fileObject;
    }

    for (int attempt = 0; attempt < 12; ++attempt) {
        QThread::sleep(1);
        QNetworkRequest request{geminiApiUrl(QString("/v1beta/%1").arg(fileName), apiKey)};
        const auto result = performJsonRequest(manager, request, QByteArray(), "GET");
        if (!result.error.isEmpty() || result.statusCode >= 300) {
            break;
        }
        const auto refreshedFile = QJsonDocument::fromJson(result.body).object();
        const QString refreshedState = refreshedFile.value("state").toObject().value("name").toString();
        if (refreshedState.isEmpty() || refreshedState == "ACTIVE") {
            return refreshedFile;
        }
        fileObject = refreshedFile;
    }

    return fileObject;
}

bool geminiHandleLooksReusable(const QString &handle)
{
    const QString uri = geminiUriFromHandle(handle).trimmed();
    const QString mimeType = geminiMimeFromHandle(handle).trimmed();
    return !uri.isEmpty() && uri.startsWith("https://") && !mimeType.isEmpty();
}

void setUsage(ProviderResponse &response,
              const QString &promptText,
              int inputTokens,
              int outputTokens,
              int totalTokens,
              bool providerReported)
{
    response.inputTokens = qMax(0, inputTokens);
    response.outputTokens = qMax(0, outputTokens);
    response.usageReported = providerReported;
    response.usageEstimated = !providerReported;
    if (providerReported) {
        response.usedTokens = qMax(0, totalTokens);
    } else {
        response.inputTokens = estimatedTokenCount(promptText);
        response.outputTokens = estimatedTokenCount(response.content);
        response.usedTokens = response.inputTokens + response.outputTokens;
    }
}

void setReportedFailureUsage(const ProviderRequest &request,
                             const QByteArray &body,
                             ProviderResponse &response)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const QJsonObject json = document.object();
    QJsonObject usage;
    int inputTokens = 0;
    int outputTokens = 0;
    int totalTokens = 0;
    switch (request.provider) {
    case ProviderKind::OpenAI:
        usage = json.value("usage").toObject();
        response.modelUsed = json.value("model").toString(request.model);
        inputTokens = usage.value("input_tokens").toInt();
        outputTokens = usage.value("output_tokens").toInt();
        totalTokens = usage.value("total_tokens").toInt(inputTokens + outputTokens);
        response.cachedTokens = usage.value("input_tokens_details").toObject()
                                    .value("cached_tokens").toInt();
        response.reasoningTokens = usage.value("output_tokens_details").toObject()
                                       .value("reasoning_tokens").toInt();
        break;
    case ProviderKind::Gemini:
        usage = json.value("usageMetadata").toObject();
        response.modelUsed = json.value("modelVersion").toString(request.model);
        inputTokens = usage.value("promptTokenCount").toInt();
        response.cachedTokens = usage.value("cachedContentTokenCount").toInt();
        response.reasoningTokens = usage.value("thoughtsTokenCount").toInt();
        outputTokens = usage.value("candidatesTokenCount").toInt() + response.reasoningTokens;
        totalTokens = usage.value("totalTokenCount").toInt(inputTokens + outputTokens);
        break;
    case ProviderKind::Anthropic:
        usage = json.value("usage").toObject();
        response.modelUsed = json.value("model").toString(request.model);
        inputTokens = usage.value("input_tokens").toInt();
        outputTokens = usage.value("output_tokens").toInt();
        response.cachedTokens = usage.value("cache_creation_input_tokens").toInt()
            + usage.value("cache_read_input_tokens").toInt();
        totalTokens = inputTokens + outputTokens + response.cachedTokens;
        break;
    }
    if (!usage.isEmpty()) {
        setUsage(response, {}, inputTokens, outputTokens, totalTokens, true);
    }
}

QString outcomeUnknownMessage()
{
    return QStringLiteral("The provider may have completed the request, but the app did not receive a confirmed result. Trying again could duplicate provider work or usage.");
}

QString cancelledMessage()
{
    return QStringLiteral("The provider request was cancelled and was not automatically retried.");
}

ProviderResponse makeErrorResponse(
    const ProviderRequest &request,
    const QString &message,
    ProviderDeliveryOutcome deliveryOutcome = ProviderDeliveryOutcome::DefiniteFailure)
{
    ProviderResponse response;
    response.requestId = request.requestId;
    response.sessionId = request.sessionId;
    response.seatId = request.seatId;
    response.runGeneration = request.runGeneration;
    response.success = false;
    response.deliveryOutcome = deliveryOutcome;
    response.errorMessage = message;
    return response;
}

ProviderResponse makeGenerationFailureResponse(const ProviderRequest &request,
                                               const NetworkResult &result)
{
    ProviderResponse response;
    if (result.deliveryOutcome == ProviderDeliveryOutcome::Cancelled) {
        response = makeErrorResponse(request, cancelledMessage(),
                                     ProviderDeliveryOutcome::Cancelled);
    } else if (result.deliveryOutcome == ProviderDeliveryOutcome::DefiniteFailure) {
        const QString status = result.statusCode > 0
            ? QString(" (HTTP %1)").arg(result.statusCode)
            : QString{};
        response = makeErrorResponse(
            request,
            QString("%1%2: %3. No generation result was produced.")
                .arg(providerKindToString(request.provider), status, safeFailureDetail(result)),
            ProviderDeliveryOutcome::DefiniteFailure);
    } else {
        response = makeErrorResponse(request, QString("%1: %2. %3").arg(providerKindToString(request.provider), safeFailureDetail(result), outcomeUnknownMessage()),
                                     ProviderDeliveryOutcome::OutcomeUnknown);
    }
    if (response.deliveryOutcome == ProviderDeliveryOutcome::DefiniteFailure) {
        setReportedFailureUsage(request, result.body, response);
    }
    return response;
}

ProviderResponse missingCredentialResponse(const ProviderRequest &request)
{
    return makeErrorResponse(
        request,
        QString("%1 request blocked before sending | model=%2 | keyPresent=no | stage=missing API key")
            .arg(providerKindToString(request.provider),
                 request.model.trimmed().isEmpty() ? "<empty>" : request.model));
}

void enrichParsedSignals(const ProviderRequest &request, ProviderResponse &response)
{
    const QString trimmed = response.content.trimmed();
    response.skipped = amt::response::hasSkipPrefix(trimmed);
    const bool finalDecisionRequest = request.phase == Phase::Present
        || request.prompt.value("role").toString().compare("Final Decision Maker", Qt::CaseInsensitive) == 0
        || request.prompt.value("decision_mode").toString().compare("arbitration", Qt::CaseInsensitive) == 0;
    if (finalDecisionRequest) {
        const bool arbitration = request.prompt.value("decision_mode").toString().compare("arbitration", Qt::CaseInsensitive) == 0;
        const auto parsed = amt::response::parseDecision(trimmed, arbitration);
        response.decisionOutcome = parsed.outcome;
        response.multipleDecisionRulings = parsed.hasMultipleExplicitRulings();
    }
}

ProviderResponse callOpenAi(QNetworkAccessManager &manager,
                            const ProviderRequest &request,
                            const JsonRequestHandler &jsonHandler = {})
{
    ProviderResponse response;
    response.requestId = request.requestId;
    response.sessionId = request.sessionId;
    response.seatId = request.seatId;
    response.runGeneration = request.runGeneration;

    const QString promptText = buildPromptText(request);
    QJsonArray content;
    content.append(QJsonObject{{"type", "input_text"}, {"text", promptText}});

    for (const auto &attachmentValue : request.prompt.value("attachments").toArray()) {
        const auto attachment = attachmentValue.toObject();
        QString fileId = attachment.value("providerHandle").toString();
        if (fileId.isEmpty()) {
            auto *file = new QFile(attachment.value("filePath").toString());
            if (!file->open(QIODevice::ReadOnly)) {
                delete file;
                return makeErrorResponse(request, QString("Failed to open attachment: %1").arg(attachment.value("displayName").toString()));
            }

            auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
            file->setParent(multipart);

            QHttpPart purposePart;
            purposePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"purpose\""));
            purposePart.setBody("user_data");
            multipart->append(purposePart);

            QHttpPart filePart;
            filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                               QVariant(QString("form-data; name=\"file\"; filename=\"%1\"").arg(attachment.value("fileName").toString())));
            filePart.setHeader(QNetworkRequest::ContentTypeHeader, detectMimeType(attachment.value("filePath").toString()));
            filePart.setBodyDevice(file);
            multipart->append(filePart);

            QNetworkRequest uploadRequest{QUrl("https://api.openai.com/v1/files")};
            uploadRequest.setRawHeader("Authorization", QByteArray("Bearer ") + request.apiKey.toUtf8());
            const auto uploadResult = performMultipartRequest(manager, uploadRequest, multipart);
            if (!uploadResult.error.isEmpty() || uploadResult.statusCode >= 300) {
                return makeErrorResponse(request, providerRequestFailure(request, "OpenAI", "file upload", uploadResult));
            }
            const auto uploadJson = QJsonDocument::fromJson(uploadResult.body).object();
            fileId = uploadJson.value("id").toString();
            if (fileId.isEmpty()) {
                return makeErrorResponse(request, "OpenAI file upload did not return a file id.");
            }
            response.attachmentProviderHandles.insert(attachment.value("attachmentId").toString(), fileId);
        }
        content.append(QJsonObject{{"type", "input_file"}, {"file_id", fileId}});
    }

    QJsonObject body{
        {"model", request.model},
        {"input", QJsonArray{QJsonObject{{"role", "user"}, {"content", content}}}}
    };
    const QString reasoningEffort = request.prompt.value("reasoning_effort").toString();
    if (!reasoningEffort.isEmpty()) {
        body.insert("reasoning", QJsonObject{{"effort", reasoningEffort}});
    }

    QNetworkRequest apiRequest{QUrl("https://api.openai.com/v1/responses")};
    apiRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    apiRequest.setRawHeader("Authorization", QByteArray("Bearer ") + request.apiKey.toUtf8());
    const auto apiResult = performJsonRequest(
        manager,
        apiRequest,
        QJsonDocument(body).toJson(QJsonDocument::Compact),
        "POST",
        jsonHandler);
    if (!apiResult.error.isEmpty() || apiResult.statusCode >= 300) {
        return makeGenerationFailureResponse(request, apiResult);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(apiResult.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return makeErrorResponse(request, "OpenAI returned a malformed response.");
    }
    const auto json = document.object();
    response.success = true;
    response.content = openAiVisibleText(json);
    response.modelUsed = json.value("model").toString(request.model);
    const auto usage = json.value("usage").toObject();
    const int inputTokens = usage.value("input_tokens").toInt();
    const int outputTokens = usage.value("output_tokens").toInt();
    response.cachedTokens = usage.value("input_tokens_details").toObject()
                                .value("cached_tokens").toInt();
    response.reasoningTokens = usage.value("output_tokens_details").toObject()
                                   .value("reasoning_tokens").toInt();
    setUsage(response, promptText, inputTokens, outputTokens,
             usage.value("total_tokens").toInt(inputTokens + outputTokens),
             !usage.isEmpty());
    return response;
}

ProviderResponse callGemini(QNetworkAccessManager &manager,
                            const ProviderRequest &request,
                            const JsonRequestHandler &jsonHandler = {})
{
    ProviderResponse response;
    response.requestId = request.requestId;
    response.sessionId = request.sessionId;
    response.seatId = request.seatId;
    response.runGeneration = request.runGeneration;

    auto uploadAttachment = [&](const QJsonObject &attachment, QString *outUri, QString *outMime) -> QString {
        const QString filePath = attachment.value("filePath").toString();
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            response.errorMessage = QString("Failed to open attachment: %1").arg(attachment.value("displayName").toString());
            return {};
        }
        const qint64 fileSize = file.size();
        const QString mimeType = detectMimeType(filePath);

        QNetworkRequest startRequest{geminiApiUrl("/upload/v1beta/files", request.apiKey)};
        startRequest.setRawHeader("X-Goog-Upload-Protocol", "resumable");
        startRequest.setRawHeader("X-Goog-Upload-Command", "start");
        startRequest.setRawHeader("X-Goog-Upload-Header-Content-Length", QByteArray::number(fileSize));
        startRequest.setRawHeader("X-Goog-Upload-Header-Content-Type", mimeType.toUtf8());
        startRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        const QByteArray startBody = QJsonDocument(QJsonObject{
            {"file", QJsonObject{{"display_name", attachment.value("displayName").toString()}}}
        }).toJson(QJsonDocument::Compact);
        const auto startResult = performJsonRequest(manager, startRequest, startBody);
        const QString uploadUrl = headerValue(startResult.headers, "x-goog-upload-url");
        if (!startResult.error.isEmpty() || uploadUrl.isEmpty()) {
            response.errorMessage = providerRequestFailure(request, "Google", "file upload start", startResult);
            return {};
        }

        QNetworkRequest uploadRequest{QUrl(uploadUrl)};
        uploadRequest.setRawHeader("Content-Length", QByteArray::number(fileSize));
        uploadRequest.setRawHeader("X-Goog-Upload-Offset", "0");
        uploadRequest.setRawHeader("X-Goog-Upload-Command", "upload, finalize");
        const auto uploadResult = performDeviceRequest(manager, uploadRequest, &file);
        if (!uploadResult.error.isEmpty() || uploadResult.statusCode >= 300) {
            response.errorMessage = providerRequestFailure(request, "Google", "file upload", uploadResult);
            return {};
        }

        auto uploadJson = QJsonDocument::fromJson(uploadResult.body).object().value("file").toObject();
        uploadJson = pollGeminiFileReady(manager, request.apiKey, uploadJson);
        const QString fileName = uploadJson.value("name").toString();
        const QString fileUri = uploadJson.value("uri").toString();
        const QString resolvedMime = uploadJson.value("mimeType").toString(mimeType);
        const QString stateName = uploadJson.value("state").toObject().value("name").toString();
        if (fileUri.isEmpty() || (!stateName.isEmpty() && stateName != "ACTIVE")) {
            response.errorMessage = QString("Gemini attachment preparation did not return an active file. No generation request was sent.");
            return {};
        }

        if (outUri) {
            *outUri = fileUri;
        }
        if (outMime) {
            *outMime = resolvedMime;
        }
        return geminiHandle(fileName, fileUri, resolvedMime);
    };

    auto buildParts = [&](bool forceFreshUpload, bool *hadReusableHandles) {
        QJsonArray builtParts;
        builtParts.append(QJsonObject{{"text", buildPromptText(request)}});
        if (hadReusableHandles) {
            *hadReusableHandles = false;
        }

        for (const auto &attachmentValue : request.prompt.value("attachments").toArray()) {
            const auto attachment = attachmentValue.toObject();
            QString handle = attachment.value("providerHandle").toString();
            QString fileUri;
            QString mimeType;

            if (!forceFreshUpload && geminiHandleLooksReusable(handle)) {
                fileUri = geminiUriFromHandle(handle);
                mimeType = geminiMimeFromHandle(handle);
                if (hadReusableHandles) {
                    *hadReusableHandles = true;
                }
            } else {
                handle = uploadAttachment(attachment, &fileUri, &mimeType);
                if (handle.isEmpty()) {
                    return QJsonArray{};
                }
                response.attachmentProviderHandles.insert(attachment.value("attachmentId").toString(), handle);
            }

            builtParts.append(QJsonObject{
                {"file_data", QJsonObject{
                    {"mime_type", mimeType},
                    {"file_uri", fileUri}
                }}
            });
        }

        return builtParts;
    };

    QJsonArray parts = buildParts(false, nullptr);
    if (parts.isEmpty() && !request.prompt.value("attachments").toArray().isEmpty()) {
        return makeErrorResponse(request, response.errorMessage);
    }

    QJsonObject generationConfig;
    const QString thinkingLevel = request.prompt.value("thinking_level").toString();
    if (!thinkingLevel.isEmpty()) {
        if (request.model.startsWith("gemini-3")) {
            generationConfig.insert("thinkingConfig", QJsonObject{{"thinkingLevel", thinkingLevel}});
        } else {
            const int budget = request.prompt.value("thinking_budget_tokens").toInt(4096);
            generationConfig.insert("thinkingConfig", QJsonObject{{"thinkingBudget", budget}});
        }
    }

    QJsonObject body{
        {"contents", QJsonArray{QJsonObject{{"role", "user"}, {"parts", parts}}}}
    };
    if (!generationConfig.isEmpty()) {
        body.insert("generationConfig", generationConfig);
    }

    const bool geminiSearchEnabled = request.phase == Phase::Research;
    if (geminiSearchEnabled) {
        body.insert("tools", QJsonArray{
            QJsonObject{
                {"google_search", QJsonObject{}}
            }
        });
    }

    QNetworkRequest apiRequest{geminiApiUrl(QString("/v1beta/models/%1:generateContent").arg(request.model), request.apiKey)};
    apiRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const NetworkResult apiResult = performJsonRequest(
        manager,
        apiRequest,
        QJsonDocument(body).toJson(QJsonDocument::Compact),
        "POST",
        jsonHandler);
    if (!apiResult.error.isEmpty() || apiResult.statusCode >= 300) {
        return makeGenerationFailureResponse(request, apiResult);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(apiResult.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return makeErrorResponse(request, "Google returned a malformed response.");
    }
    const auto json = document.object();
    response.success = true;
    response.content = geminiVisibleText(json);
    response.modelUsed = json.value("modelVersion").toString(request.model);
    const auto usage = json.value("usageMetadata").toObject();
    const int promptTokens = usage.value("promptTokenCount").toInt();
    response.cachedTokens = usage.value("cachedContentTokenCount").toInt();
    response.reasoningTokens = usage.value("thoughtsTokenCount").toInt();
    const int outputTokens = usage.value("candidatesTokenCount").toInt()
        + response.reasoningTokens;
    setUsage(response, buildPromptText(request), promptTokens, outputTokens,
             usage.value("totalTokenCount").toInt(promptTokens + outputTokens),
             !usage.isEmpty());
    return response;
}

ProviderResponse callAnthropic(QNetworkAccessManager &manager,
                               const ProviderRequest &request,
                               const JsonRequestHandler &jsonHandler = {})
{
    ProviderResponse response;
    response.requestId = request.requestId;
    response.sessionId = request.sessionId;
    response.seatId = request.seatId;
    response.runGeneration = request.runGeneration;

    QString promptText = buildPromptText(request);
    QJsonArray content;
    content.append(QJsonObject{{"type", "text"}, {"text", promptText}});

    for (const auto &attachmentValue : request.prompt.value("attachments").toArray()) {
        const auto attachment = attachmentValue.toObject();
        QString fileId = attachment.value("providerHandle").toString();
        const QString filePath = attachment.value("filePath").toString();
        const QString mimeType = detectMimeType(filePath);

        if (!isAnthropicFileBlockMime(mimeType)) {
            const QString inlineText = readTextAttachment(filePath).trimmed();
            if (!inlineText.isEmpty()) {
                content.append(QJsonObject{{"type", "text"}, {"text", QString("Attachment %1:\n%2").arg(attachment.value("displayName").toString(), inlineText)}});
            }
            continue;
        }

        if (fileId.isEmpty()) {
            auto *file = new QFile(filePath);
            if (!file->open(QIODevice::ReadOnly)) {
                delete file;
                return makeErrorResponse(request, QString("Failed to open attachment: %1").arg(attachment.value("displayName").toString()));
            }

            auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
            file->setParent(multipart);
            QHttpPart filePart;
            filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                               QVariant(QString("form-data; name=\"file\"; filename=\"%1\"").arg(attachment.value("fileName").toString())));
            filePart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
            filePart.setBodyDevice(file);
            multipart->append(filePart);

            QNetworkRequest uploadRequest{QUrl("https://api.anthropic.com/v1/files")};
            uploadRequest.setRawHeader("x-api-key", request.apiKey.toUtf8());
            uploadRequest.setRawHeader("anthropic-version", "2023-06-01");
            uploadRequest.setRawHeader("anthropic-beta", "files-api-2025-04-14");
            const auto uploadResult = performMultipartRequest(manager, uploadRequest, multipart);
            if (!uploadResult.error.isEmpty() || uploadResult.statusCode >= 300) {
                return makeErrorResponse(request, providerRequestFailure(request, "Anthropic", "file upload", uploadResult));
            }
            const auto uploadJson = QJsonDocument::fromJson(uploadResult.body).object();
            fileId = uploadJson.value("id").toString();
            if (fileId.isEmpty()) {
                return makeErrorResponse(request, "Anthropic file upload did not return a file id.");
            }
            response.attachmentProviderHandles.insert(attachment.value("attachmentId").toString(), fileId);
        }

        if (mimeType.startsWith("image/")) {
            content.append(QJsonObject{
                {"type", "image"},
                {"source", QJsonObject{
                    {"type", "file"},
                    {"file_id", fileId}
                }}
            });
        } else {
            content.append(QJsonObject{
                {"type", "document"},
                {"source", QJsonObject{
                    {"type", "file"},
                    {"file_id", fileId}
                }}
            });
        }
    }

    const int thinkingBudget = request.prompt.value("thinking_budget_tokens").toInt();
    QJsonObject body{
        {"model", request.model},
        {"max_tokens", qMax(4096, thinkingBudget > 0 ? thinkingBudget + 4096 : 4096)},
        {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", content}}}}
    };
    if (thinkingBudget > 0) {
        body.insert("thinking", QJsonObject{
            {"type", "enabled"},
            {"budget_tokens", thinkingBudget}
        });
    }

    QNetworkRequest apiRequest{QUrl("https://api.anthropic.com/v1/messages")};
    apiRequest.setRawHeader("x-api-key", request.apiKey.toUtf8());
    apiRequest.setRawHeader("anthropic-version", "2023-06-01");
    apiRequest.setRawHeader("anthropic-beta", "files-api-2025-04-14");
    apiRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const auto apiResult = performJsonRequest(
        manager,
        apiRequest,
        QJsonDocument(body).toJson(QJsonDocument::Compact),
        "POST",
        jsonHandler);
    if (!apiResult.error.isEmpty() || apiResult.statusCode >= 300) {
        return makeGenerationFailureResponse(request, apiResult);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(apiResult.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return makeErrorResponse(request, "Anthropic returned a malformed response.");
    }
    const auto json = document.object();
    response.success = true;
    response.content = anthropicVisibleText(json);
    response.modelUsed = json.value("model").toString(request.model);
    const auto usage = json.value("usage").toObject();
    const int inputTokens = usage.value("input_tokens").toInt();
    const int outputTokens = usage.value("output_tokens").toInt();
    response.cachedTokens = usage.value("cache_creation_input_tokens").toInt()
        + usage.value("cache_read_input_tokens").toInt();
    setUsage(response, promptText, inputTokens, outputTokens,
             inputTokens + outputTokens + response.cachedTokens,
             !usage.isEmpty());
    return response;
}

bool shouldAutomaticallyRetry(const ProviderResponse &response)
{
    Q_UNUSED(response);
    return false; // Generation retries require explicit user authorization.
}

ProviderResponse processProviderRequest(QNetworkAccessManager &manager,
                                        const ProviderRequest &request,
                                        const JsonRequestHandler &jsonHandler = {})
{
    ProviderResponse response;
    {
        switch (request.provider) {
        case ProviderKind::OpenAI:
            response = callOpenAi(manager, request, jsonHandler);
            break;
        case ProviderKind::Gemini:
            response = callGemini(manager, request, jsonHandler);
            break;
        case ProviderKind::Anthropic:
            response = callAnthropic(manager, request, jsonHandler);
            break;
        }
    }

    if (response.success) {
        if (response.content.trimmed().isEmpty()) {
            response.success = false;
            response.deliveryOutcome = ProviderDeliveryOutcome::DefiniteFailure;
            response.errorMessage = QString("%1 returned no user-visible assistant text.")
                                        .arg(providerKindToString(request.provider));
        } else {
            response.deliveryOutcome = ProviderDeliveryOutcome::Succeeded;
            enrichParsedSignals(request, response);
        }
    }
    return response;
}

} // namespace

class ProviderGatewayWorker final : public QObject
{
    Q_OBJECT

public:
    explicit ProviderGatewayWorker(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

public slots:
    void process(const amt::ProviderRequest &request)
    {
        if (!m_manager) {
            m_manager = new QNetworkAccessManager(this);
        }
        emit responseReady(processProviderRequest(*m_manager, request));
    }

signals:
    void responseReady(const amt::ProviderResponse &response);

private:
    QNetworkAccessManager *m_manager = nullptr;
};

ProviderGateway::ProviderGateway(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<amt::ProviderRequest>();
    qRegisterMetaType<amt::ProviderResponse>();
}

ProviderGateway::~ProviderGateway()
{
}

void ProviderGateway::setCredentialStore(CredentialStore *credentialStore)
{
    m_credentialStore = credentialStore;
}

#ifdef AMT_TESTING
ProviderResponse ProviderGateway::processForTesting(const ProviderRequest &request,
                                                    const ProviderTestTransport &transport)
{
    if (request.apiKey.trimmed().isEmpty()) {
        return missingCredentialResponse(request);
    }
    if (!transport) {
        return makeErrorResponse(request, "Synthetic provider transport is unavailable.");
    }

    const JsonRequestHandler handler = [transport](const QNetworkRequest &networkRequest,
                                                   const QByteArray &body,
                                                   const QByteArray &method) {
        ProviderTestRequest testRequest;
        testRequest.url = networkRequest.url();
        testRequest.method = method;
        testRequest.body = body;
        for (const QByteArray &name : networkRequest.rawHeaderList()) {
            testRequest.headers.append({name, networkRequest.rawHeader(name)});
        }
        const ProviderTestNetworkResult testResult = transport(testRequest);

        NetworkResult result;
        result.statusCode = testResult.statusCode;
        result.body = testResult.body;
        result.error = testResult.transportError;
        result.networkErrorCode = testResult.networkErrorCode;
        result.sslErrorCount = testResult.sslErrorCount;
        result.headers = testResult.headers;
        result.requestSent = testResult.requestSent;
        result.tlsCategory = tlsFailureCategory(testResult.tlsErrors);
        if (testResult.cancelled) {
            result.networkErrorCode = QNetworkReply::OperationCanceledError;
            result.error = "cancelled";
        }
        result.deliveryOutcome = classifyDelivery(result, method == "POST");
        return result;
    };

    QNetworkAccessManager manager;
    return processProviderRequest(manager, request, handler);
}

ProviderResponse ProviderGateway::localRequestForTesting(const QUrl &url)
{
    ProviderRequest request;
    if (url.scheme() != "http" || url.host() != "127.0.0.1")
        return makeErrorResponse(request, "Only loopback fixture URLs are permitted.");
    QNetworkAccessManager manager;
    const auto result = performJsonRequest(manager, QNetworkRequest(url), "{}");
    return makeGenerationFailureResponse(request, result);
}

bool ProviderGateway::shouldRetryForTesting(const ProviderResponse &response)
{
    return shouldAutomaticallyRetry(response);
}
#endif

void ProviderGateway::sendAsync(const ProviderRequest &request)
{
    ProviderRequest hydratedRequest = request;
    if (hydratedRequest.apiKey.isEmpty() && m_credentialStore) {
        hydratedRequest.apiKey = m_credentialStore->loadApiKey(request.provider);
    }
    if (hydratedRequest.apiKey.trimmed().isEmpty()) {
        emit responseReady(missingCredentialResponse(request));
        return;
    }

    // Spawn a dedicated thread per request so Research-phase calls run in parallel.
    auto *thread = new QThread();
    auto *worker = new ProviderGatewayWorker();
    worker->moveToThread(thread);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(worker, &ProviderGatewayWorker::responseReady, this, &ProviderGateway::responseReady, Qt::QueuedConnection);
    // Quit the thread immediately after the response is emitted (DirectConnection runs on worker thread).
    connect(worker, &ProviderGatewayWorker::responseReady, thread, &QThread::quit, Qt::DirectConnection);
    QMetaObject::invokeMethod(worker, "process", Qt::QueuedConnection, Q_ARG(amt::ProviderRequest, hydratedRequest));
    thread->start();
}

} // namespace amt

#include "provider_gateway.moc"
