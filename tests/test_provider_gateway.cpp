#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include "providers/network_diagnostics.h"
#include <QUrlQuery>

#include "providers/provider_gateway.h"

using namespace amt;

namespace {

constexpr auto unknownOutcomeMessage =
    "The provider may have completed the request, but the app did not receive a confirmed result. Trying again could duplicate provider work or usage.";
constexpr auto syntheticKey = "synthetic-unit-marker";

enum ProviderMode {
    OpenAiMode,
    GeminiMode,
    GeminiSearchMode,
    AnthropicMode
};

enum FailureScenario {
    TimeoutScenario,
    ConnectionLossScenario,
    TlsFailureScenario,
    RemoteCloseScenario,
    CancellationScenario,
    MalformedResponseScenario,
    DelayedSuccessScenario,
    SuccessScenario
};

ProviderRequest requestFor(int mode)
{
    ProviderRequest request;
    request.requestId = "logical-operation";
    request.sessionId = "session";
    request.seatId = "seat";
    request.apiKey = syntheticKey;
    request.phase = mode == GeminiSearchMode ? Phase::Research : Phase::Planning;
    request.prompt.insert("instruction", "Synthetic provider transport test");
    switch (mode) {
    case OpenAiMode:
        request.provider = ProviderKind::OpenAI;
        request.model = "gpt-5-mini";
        break;
    case GeminiMode:
    case GeminiSearchMode:
        request.provider = ProviderKind::Gemini;
        request.model = "gemini-2.5-flash";
        break;
    case AnthropicMode:
        request.provider = ProviderKind::Anthropic;
        request.model = "claude-sonnet-4";
        break;
    }
    return request;
}

QByteArray successBody(int mode)
{
    switch (mode) {
    case OpenAiMode:
        return QJsonDocument(QJsonObject{
            {"output", QJsonArray{QJsonObject{{"type", "output_text"}, {"text", "confirmed"}}}},
            {"usage", QJsonObject{{"input_tokens", 10}, {"output_tokens", 5}, {"total_tokens", 15}}}
        }).toJson(QJsonDocument::Compact);
    case GeminiMode:
    case GeminiSearchMode:
        return QJsonDocument(QJsonObject{
            {"candidates", QJsonArray{QJsonObject{{"content", QJsonObject{{"parts", QJsonArray{QJsonObject{{"text", "confirmed"}}}}}}}}},
            {"usageMetadata", QJsonObject{{"promptTokenCount", 10}, {"candidatesTokenCount", 5}, {"totalTokenCount", 15}}}
        }).toJson(QJsonDocument::Compact);
    case AnthropicMode:
        return QJsonDocument(QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "confirmed"}}}},
            {"usage", QJsonObject{{"input_tokens", 10}, {"output_tokens", 5}}}
        }).toJson(QJsonDocument::Compact);
    }
    return {};
}

QByteArray headerValue(const ProviderTestRequest &request, const QByteArray &name)
{
    for (const auto &header : request.headers) {
        if (header.first.compare(name, Qt::CaseInsensitive) == 0) {
            return header.second;
        }
    }
    return {};
}

void verifyRequestShape(const ProviderTestRequest &exchange, int mode)
{
    QCOMPARE(exchange.method, QByteArray("POST"));
    QCOMPARE(headerValue(exchange, "Content-Type"), QByteArray("application/json"));
    QVERIFY(headerValue(exchange, "Idempotency-Key").isEmpty());
    QVERIFY(!exchange.body.contains("logical-operation"));

    const QJsonObject body = QJsonDocument::fromJson(exchange.body).object();
    QVERIFY(!body.isEmpty());
    switch (mode) {
    case OpenAiMode:
        QCOMPARE(exchange.url, QUrl("https://api.openai.com/v1/responses"));
        QCOMPARE(headerValue(exchange, "Authorization"),
                 QByteArray("Bearer ") + syntheticKey);
        QCOMPARE(body.value("model").toString(), QString("gpt-5-mini"));
        QVERIFY(!body.value("input").toArray().isEmpty());
        break;
    case GeminiMode:
    case GeminiSearchMode: {
        QCOMPARE(exchange.url.scheme(), QString("https"));
        QCOMPARE(exchange.url.host(), QString("generativelanguage.googleapis.com"));
        QCOMPARE(exchange.url.path(), QString("/v1beta/models/gemini-2.5-flash:generateContent"));
        QCOMPARE(QUrlQuery(exchange.url).queryItemValue("key"), QString(syntheticKey));
        QVERIFY(!body.value("contents").toArray().isEmpty());
        QCOMPARE(body.contains("tools"), mode == GeminiSearchMode);
        if (mode == GeminiSearchMode) {
            const QJsonObject tool = body.value("tools").toArray().first().toObject();
            QVERIFY(tool.contains("google_search"));
        }
        break;
    }
    case AnthropicMode:
        QCOMPARE(exchange.url, QUrl("https://api.anthropic.com/v1/messages"));
        QCOMPARE(headerValue(exchange, "x-api-key"), QByteArray(syntheticKey));
        QCOMPARE(headerValue(exchange, "anthropic-version"), QByteArray("2023-06-01"));
        QCOMPARE(headerValue(exchange, "anthropic-beta"), QByteArray("files-api-2025-04-14"));
        QCOMPARE(body.value("model").toString(), QString("claude-sonnet-4"));
        QVERIFY(!body.value("messages").toArray().isEmpty());
        break;
    }
}

ProviderTestNetworkResult resultFor(int mode, int scenario)
{
    ProviderTestNetworkResult result;
    switch (scenario) {
    case TimeoutScenario:
        result.transportError = "timeout";
        result.networkErrorCode = QNetworkReply::TimeoutError;
        break;
    case ConnectionLossScenario:
        result.transportError = "network";
        result.networkErrorCode = QNetworkReply::TemporaryNetworkFailureError;
        break;
    case TlsFailureScenario:
        result.transportError = "network";
        result.networkErrorCode = QNetworkReply::SslHandshakeFailedError;
        result.sslErrorCount = 1;
        result.requestSent = false;
        result.tlsErrors = {QSslError(QSslError::UnableToGetLocalIssuerCertificate)};
        break;
    case RemoteCloseScenario:
        result.transportError = "network";
        result.networkErrorCode = QNetworkReply::RemoteHostClosedError;
        break;
    case CancellationScenario:
        result.transportError = "network";
        result.networkErrorCode = QNetworkReply::OperationCanceledError;
        result.cancelled = true;
        break;
    case MalformedResponseScenario:
        result.statusCode = 200;
        result.body = "{\"unexpected\":true}";
        break;
    case DelayedSuccessScenario:
        QThread::msleep(15);
        [[fallthrough]];
    case SuccessScenario:
        result.statusCode = 200;
        result.body = successBody(mode);
        break;
    }
    return result;
}

} // namespace

class ProviderGatewayTests final : public QObject
{
    Q_OBJECT

private slots:
    void generationPostIsNotReplayed_data();
    void generationPostIsNotReplayed();
    void definitePreTransmissionFailureDoesNotSend_data();
    void definitePreTransmissionFailureDoesNotSend();
    void definiteTransientRetryPolicyRemainsAvailable();
    void deliveryClassification();
    void realQtHttpRejections();
    void tlsCategories();
    void geminiRejectedReusableHandleIsNotReplayed();
    void providerVisibleContentExtraction_data();
    void providerVisibleContentExtraction();
    void nonVisibleContentFailsClosed_data();
    void nonVisibleContentFailsClosed();
    void malformedJsonAndProviderErrors_data();
    void malformedJsonAndProviderErrors();
    void usageMetadataAndFallback();
};

void ProviderGatewayTests::generationPostIsNotReplayed_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("scenario");
    const QStringList modes{"openai", "gemini", "gemini-search", "anthropic"};
    const QStringList scenarios{
        "timeout", "connection-loss", "tls-failure", "remote-close",
        "cancellation", "malformed", "delayed-success", "success"
    };
    for (int mode = OpenAiMode; mode <= AnthropicMode; ++mode) {
        for (int scenario = TimeoutScenario; scenario <= SuccessScenario; ++scenario) {
            const QByteArray rowName = QString("%1-%2")
                                           .arg(modes.at(mode), scenarios.at(scenario))
                                           .toLatin1();
            QTest::newRow(rowName.constData()) << mode << scenario;
        }
    }
}

void ProviderGatewayTests::generationPostIsNotReplayed()
{
    QFETCH(int, mode);
    QFETCH(int, scenario);

    const ProviderRequest request = requestFor(mode);
    QVector<ProviderTestRequest> exchanges;
    const ProviderResponse response = ProviderGateway::processForTesting(
        request,
        [&](const ProviderTestRequest &exchange) {
            exchanges.append(exchange);
            return resultFor(mode, scenario);
        });

    QCOMPARE(exchanges.size(), 1);
    verifyRequestShape(exchanges.first(), mode);
    QVERIFY(!response.errorMessage.contains(syntheticKey));
    QVERIFY(!response.errorMessage.contains("https://"));
    QVERIFY(!response.errorMessage.contains("networkCode"));
    QVERIFY(!response.errorMessage.contains("private response"));

    if (scenario == SuccessScenario || scenario == DelayedSuccessScenario) {
        QVERIFY(response.success);
        QCOMPARE(static_cast<int>(response.deliveryOutcome),
                 static_cast<int>(ProviderDeliveryOutcome::Succeeded));
        QCOMPARE(response.content, QString("confirmed"));
        QCOMPARE(response.usedTokens, 15);
    } else if (scenario == TlsFailureScenario) {
        QVERIFY(!response.success);
        QCOMPARE(static_cast<int>(response.deliveryOutcome),
                 static_cast<int>(ProviderDeliveryOutcome::DefiniteFailure));
        QVERIFY(response.errorMessage.contains("missing issuer"));
    } else if (scenario == MalformedResponseScenario) {
        QVERIFY(!response.success);
        QCOMPARE(static_cast<int>(response.deliveryOutcome),
                 static_cast<int>(ProviderDeliveryOutcome::DefiniteFailure));
        QVERIFY(response.content.isEmpty());
        QVERIFY(response.errorMessage.contains("user-visible"));
    } else {
        QVERIFY(!response.success);
        QCOMPARE(static_cast<int>(response.deliveryOutcome),
                 static_cast<int>(ProviderDeliveryOutcome::OutcomeUnknown));
        QVERIFY(response.errorMessage.contains(unknownOutcomeMessage));
    }
}

void ProviderGatewayTests::definitePreTransmissionFailureDoesNotSend_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("openai") << int(OpenAiMode);
    QTest::newRow("gemini") << int(GeminiMode);
    QTest::newRow("gemini-search") << int(GeminiSearchMode);
    QTest::newRow("anthropic") << int(AnthropicMode);
}

void ProviderGatewayTests::definitePreTransmissionFailureDoesNotSend()
{
    QFETCH(int, mode);
    ProviderRequest request = requestFor(mode);
    request.apiKey.clear();
    int invocations = 0;
    const ProviderResponse response = ProviderGateway::processForTesting(
        request,
        [&](const ProviderTestRequest &) {
            ++invocations;
            return ProviderTestNetworkResult{};
        });

    QCOMPARE(invocations, 0);
    QVERIFY(!response.success);
    QCOMPARE(static_cast<int>(response.deliveryOutcome),
             static_cast<int>(ProviderDeliveryOutcome::DefiniteFailure));
    QVERIFY(response.errorMessage.contains("blocked before sending"));
}

void ProviderGatewayTests::deliveryClassification()
{
    for (int mode = OpenAiMode; mode <= AnthropicMode; ++mode) {
        for (int status : {400, 401, 403, 404, 413, 422, 429, 408, 500, 502, 503, 307}) {
            int sends = 0;
            const auto response = ProviderGateway::processForTesting(requestFor(mode), [&](const ProviderTestRequest &) {
                ++sends;
                ProviderTestNetworkResult result;
                result.statusCode = status;
                result.networkErrorCode = QNetworkReply::UnknownContentError;
                result.transportError = "private transport";
                result.body = "private body and signature";
                return result;
            });
            QCOMPARE(sends, 1);
            QCOMPARE(response.deliveryOutcome, (status == 408 || status >= 500 || status == 307)
                ? ProviderDeliveryOutcome::OutcomeUnknown : ProviderDeliveryOutcome::DefiniteFailure);
            QVERIFY(!response.errorMessage.contains("private"));
        }
        for (int code : {int(QNetworkReply::HostNotFoundError), int(QNetworkReply::ConnectionRefusedError), int(QNetworkReply::SslHandshakeFailedError), int(QNetworkReply::OperationCanceledError)}) {
            for (bool sent : {false, true}) {
                const auto response = ProviderGateway::processForTesting(requestFor(mode), [&](const ProviderTestRequest &) {
                    ProviderTestNetworkResult result;
                    result.requestSent = sent; result.networkErrorCode = code; result.transportError = "network";
                    return result;
                });
                QCOMPARE(response.deliveryOutcome, sent ? ProviderDeliveryOutcome::OutcomeUnknown
                    : code == QNetworkReply::OperationCanceledError ? ProviderDeliveryOutcome::Cancelled : ProviderDeliveryOutcome::DefiniteFailure);
            }
        }
    }
}

void ProviderGatewayTests::realQtHttpRejections()
{
    for (int status : {401, 403, 429, 503, 307}) {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int sends = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
                socket->readAll();
                if (socket->property("answered").toBool()) return;
                socket->setProperty("answered", true); ++sends;
                socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: 2\r\nConnection: close\r\nLocation: /must-not-replay\r\n\r\n{}");
                socket->disconnectFromHost();
            });
        });
        const auto response = ProviderGateway::localRequestForTesting(QUrl(QString("http://127.0.0.1:%1/fixture").arg(server.serverPort())));
        QCOMPARE(sends, 1);
        QCOMPARE(response.deliveryOutcome, status == 503 || status == 307
            ? ProviderDeliveryOutcome::OutcomeUnknown : ProviderDeliveryOutcome::DefiniteFailure);
    }
}

void ProviderGatewayTests::tlsCategories()
{
    const QList<QSslError> errors{QSslError(QSslError::UnableToGetLocalIssuerCertificate), QSslError(QSslError::CertificateUntrusted), QSslError(QSslError::SelfSignedCertificate), QSslError(QSslError::HostNameMismatch), QSslError(QSslError::CertificateExpired), QSslError(QSslError::CertificateNotYetValid)};
    const auto message = tlsFailureCategory(errors);
    for (const auto *category : {"missing issuer", "untrusted", "self-signed", "hostname", "expired", "not yet valid"})
        QVERIFY(message.contains(category));
}

void ProviderGatewayTests::definiteTransientRetryPolicyRemainsAvailable()
{
    ProviderResponse response;
    response.success = false;
    response.deliveryOutcome = ProviderDeliveryOutcome::DefiniteFailure;
    response.errorMessage = "Synthetic upload failure http=503";
    QVERIFY(!ProviderGateway::shouldRetryForTesting(response));

    response.errorMessage = "Synthetic non-transient failure";
    QVERIFY(!ProviderGateway::shouldRetryForTesting(response));

    response.errorMessage = "Synthetic failure http=503";
    response.deliveryOutcome = ProviderDeliveryOutcome::OutcomeUnknown;
    QVERIFY(!ProviderGateway::shouldRetryForTesting(response));
    response.deliveryOutcome = ProviderDeliveryOutcome::Cancelled;
    QVERIFY(!ProviderGateway::shouldRetryForTesting(response));
}

void ProviderGatewayTests::geminiRejectedReusableHandleIsNotReplayed()
{
    ProviderRequest request = requestFor(GeminiMode);
    request.prompt.insert("attachments", QJsonArray{QJsonObject{
        {"attachmentId", "attachment"},
        {"displayName", "synthetic.pdf"},
        {"filePath", "unused-synthetic-path"},
        {"providerHandle", "files/old|https://generativelanguage.googleapis.com/v1beta/files/old|application/pdf"}
    }});

    QVector<ProviderTestRequest> exchanges;
    const ProviderResponse response = ProviderGateway::processForTesting(
        request,
        [&](const ProviderTestRequest &exchange) {
            exchanges.append(exchange);
            ProviderTestNetworkResult result;
            result.statusCode = 400;
            result.body = "{\"error\":{\"message\":\"synthetic rejection\"}}";
            return result;
        });

    QCOMPARE(exchanges.size(), 1);
    verifyRequestShape(exchanges.first(), GeminiMode);
    QVERIFY(!response.success);
    QCOMPARE(static_cast<int>(response.deliveryOutcome),
             static_cast<int>(ProviderDeliveryOutcome::DefiniteFailure));
    QVERIFY(response.errorMessage.contains("HTTP 400"));
}

void ProviderGatewayTests::providerVisibleContentExtraction_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("expected");

    QTest::newRow("openai-ignores-reasoning-and-joins-text")
        << int(OpenAiMode)
        << QJsonDocument(QJsonObject{
               {"output", QJsonArray{
                    QJsonObject{{"type", "reasoning"}, {"encrypted_content", "gAAAA-sanitized"}},
                    QJsonObject{{"type", "message"}, {"content", QJsonArray{
                        QJsonObject{{"type", "output_text"}, {"text", "Visible one"}},
                        QJsonObject{{"type", "output_text"}, {"text", "Visible two"}}
                    }}}
               }},
               {"model", "gpt-5-mini"},
               {"usage", QJsonObject{{"input_tokens", 12}, {"output_tokens", 8}, {"total_tokens", 20}}}
           }).toJson(QJsonDocument::Compact)
        << QString("Visible one\n\nVisible two");
    QTest::newRow("openai-refusal-is-visible")
        << int(OpenAiMode)
        << QJsonDocument(QJsonObject{
               {"output", QJsonArray{QJsonObject{{"type", "message"}, {"content", QJsonArray{
                    QJsonObject{{"type", "refusal"}, {"refusal", "I cannot help with that."}}
               }}}}},
               {"usage", QJsonObject{{"input_tokens", 4}, {"output_tokens", 5}, {"total_tokens", 9}}}
           }).toJson(QJsonDocument::Compact)
        << QString("Provider refusal: I cannot help with that.");
    QTest::newRow("anthropic-ignores-thinking-and-non-text")
        << int(AnthropicMode)
        << QJsonDocument(QJsonObject{
               {"content", QJsonArray{
                    QJsonObject{{"type", "thinking"}, {"thinking", "internal"}, {"signature", "EoAc-sanitized"}},
                    QJsonObject{{"type", "text"}, {"text", "Visible one"}},
                    QJsonObject{{"type", "tool_use"}, {"id", "tool-sanitized"}},
                    QJsonObject{{"type", "text"}, {"text", "Visible two"}}
               }},
               {"model", "claude-sonnet-4"},
               {"usage", QJsonObject{{"input_tokens", 12}, {"output_tokens", 8}}}
           }).toJson(QJsonDocument::Compact)
        << QString("Visible one\n\nVisible two");
}

void ProviderGatewayTests::providerVisibleContentExtraction()
{
    QFETCH(int, mode);
    QFETCH(QByteArray, body);
    QFETCH(QString, expected);
    const ProviderResponse response = ProviderGateway::processForTesting(
        requestFor(mode), [body](const ProviderTestRequest &) {
            ProviderTestNetworkResult result;
            result.statusCode = 200;
            result.body = body;
            return result;
        });
    QVERIFY(response.success);
    QCOMPARE(response.content, expected);
    QVERIFY(!response.content.contains("sanitized"));
}

void ProviderGatewayTests::nonVisibleContentFailsClosed_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<QByteArray>("body");

    QTest::newRow("openai-encrypted-reasoning-only")
        << int(OpenAiMode)
        << QJsonDocument(QJsonObject{
               {"output", QJsonArray{QJsonObject{
                    {"type", "reasoning"},
                    {"encrypted_content", "gAAAA-sanitized"},
                    {"metadata", QJsonObject{{"arbitrary", "nested-sanitized"}}}
               }}},
               {"usage", QJsonObject{{"input_tokens", 6}, {"output_tokens", 4}, {"total_tokens", 10}}}
           }).toJson(QJsonDocument::Compact);
    QTest::newRow("openai-empty-output")
        << int(OpenAiMode)
        << QJsonDocument(QJsonObject{{"output", QJsonArray{}}}).toJson(QJsonDocument::Compact);
    QTest::newRow("anthropic-thinking-signature-only")
        << int(AnthropicMode)
        << QJsonDocument(QJsonObject{
               {"content", QJsonArray{QJsonObject{
                    {"type", "thinking"}, {"thinking", "internal-sanitized"},
                    {"signature", "EqoP-sanitized"}
               }}},
               {"usage", QJsonObject{{"input_tokens", 6}, {"output_tokens", 4}}}
           }).toJson(QJsonDocument::Compact);
    QTest::newRow("anthropic-non-text-only")
        << int(AnthropicMode)
        << QJsonDocument(QJsonObject{
               {"content", QJsonArray{QJsonObject{
                    {"type", "tool_use"}, {"id", "EpUc-sanitized"},
                    {"input", QJsonObject{{"arbitrary", "nested-sanitized"}}}
               }}}
           }).toJson(QJsonDocument::Compact);
    QTest::newRow("anthropic-empty-content")
        << int(AnthropicMode)
        << QJsonDocument(QJsonObject{{"content", QJsonArray{}}}).toJson(QJsonDocument::Compact);
}

void ProviderGatewayTests::nonVisibleContentFailsClosed()
{
    QFETCH(int, mode);
    QFETCH(QByteArray, body);
    const ProviderResponse response = ProviderGateway::processForTesting(
        requestFor(mode), [body](const ProviderTestRequest &) {
            ProviderTestNetworkResult result;
            result.statusCode = 200;
            result.body = body;
            return result;
        });
    QVERIFY(!response.success);
    QCOMPARE(static_cast<int>(response.deliveryOutcome),
             static_cast<int>(ProviderDeliveryOutcome::DefiniteFailure));
    QVERIFY(response.content.isEmpty());
    QVERIFY(!response.errorMessage.contains("sanitized"));
    if (body.contains("\"usage\"")) {
        QVERIFY(response.usageReported);
        QVERIFY(response.usedTokens > 0);
    }
}

void ProviderGatewayTests::malformedJsonAndProviderErrors_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("providerError");
    QTest::newRow("openai-malformed-json") << int(OpenAiMode) << false;
    QTest::newRow("anthropic-malformed-json") << int(AnthropicMode) << false;
    QTest::newRow("openai-provider-error") << int(OpenAiMode) << true;
    QTest::newRow("anthropic-provider-error") << int(AnthropicMode) << true;
}

void ProviderGatewayTests::malformedJsonAndProviderErrors()
{
    QFETCH(int, mode);
    QFETCH(bool, providerError);
    const ProviderResponse response = ProviderGateway::processForTesting(
        requestFor(mode), [mode, providerError](const ProviderTestRequest &) {
            ProviderTestNetworkResult result;
            if (!providerError) {
                result.statusCode = 200;
                result.body = "{not-json";
                return result;
            }
            result.statusCode = 400;
            const QJsonObject usage = mode == OpenAiMode
                ? QJsonObject{{"input_tokens", 5}, {"output_tokens", 2}, {"total_tokens", 7}}
                : QJsonObject{{"input_tokens", 5}, {"output_tokens", 2}};
            result.body = QJsonDocument(QJsonObject{
                {"error", QJsonObject{{"message", "sensitive-sanitized"}}},
                {"usage", usage}
            }).toJson(QJsonDocument::Compact);
            return result;
        });
    QVERIFY(!response.success);
    QCOMPARE(static_cast<int>(response.deliveryOutcome),
             static_cast<int>(ProviderDeliveryOutcome::DefiniteFailure));
    QVERIFY(response.content.isEmpty());
    QVERIFY(!response.errorMessage.contains("sensitive-sanitized"));
    if (providerError) {
        QVERIFY(response.usageReported);
        QCOMPARE(response.usedTokens, 7);
    } else {
        QVERIFY(response.errorMessage.contains("malformed"));
        QVERIFY(!response.usageReported);
    }
}

void ProviderGatewayTests::usageMetadataAndFallback()
{
    ProviderResponse reported = ProviderGateway::processForTesting(
        requestFor(OpenAiMode), [](const ProviderTestRequest &) {
            ProviderTestNetworkResult result;
            result.statusCode = 200;
            result.body = QJsonDocument(QJsonObject{
                {"model", "gpt-5-mini"},
                {"output", QJsonArray{QJsonObject{{"type", "output_text"}, {"text", "confirmed"}}}},
                {"usage", QJsonObject{
                    {"input_tokens", 10}, {"output_tokens", 6}, {"total_tokens", 16},
                    {"input_tokens_details", QJsonObject{{"cached_tokens", 3}}},
                    {"output_tokens_details", QJsonObject{{"reasoning_tokens", 2}}}
                }}
            }).toJson(QJsonDocument::Compact);
            return result;
        });
    QVERIFY(reported.success);
    QVERIFY(reported.usageReported);
    QVERIFY(!reported.usageEstimated);
    QCOMPARE(reported.usedTokens, 16);
    QCOMPARE(reported.cachedTokens, 3);
    QCOMPARE(reported.reasoningTokens, 2);

    ProviderResponse estimated = ProviderGateway::processForTesting(
        requestFor(AnthropicMode), [](const ProviderTestRequest &) {
            ProviderTestNetworkResult result;
            result.statusCode = 200;
            result.body = QJsonDocument(QJsonObject{
                {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "estimated usage"}}}}
            }).toJson(QJsonDocument::Compact);
            return result;
        });
    QVERIFY(estimated.success);
    QVERIFY(estimated.usageEstimated);
    QVERIFY(!estimated.usageReported);
    QVERIFY(estimated.usedTokens > 0);

    ProviderResponse anthropicReported = ProviderGateway::processForTesting(
        requestFor(AnthropicMode), [](const ProviderTestRequest &) {
            ProviderTestNetworkResult result;
            result.statusCode = 200;
            result.body = QJsonDocument(QJsonObject{
                {"model", "claude-sonnet-4"},
                {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "confirmed"}}}},
                {"usage", QJsonObject{
                    {"input_tokens", 10}, {"output_tokens", 6},
                    {"cache_creation_input_tokens", 2}, {"cache_read_input_tokens", 3}
                }}
            }).toJson(QJsonDocument::Compact);
            return result;
        });
    QVERIFY(anthropicReported.success);
    QVERIFY(anthropicReported.usageReported);
    QCOMPARE(anthropicReported.cachedTokens, 5);
    QCOMPARE(anthropicReported.usedTokens, 21);
}

QTEST_GUILESS_MAIN(ProviderGatewayTests)

#include "test_provider_gateway.moc"
