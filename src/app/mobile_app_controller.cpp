#include "app/mobile_app_controller.h"

#include <algorithm>

#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QLocale>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QTimer>
#include <QUuid>

#include "core/logging.h"
#include "core/visible_text.h"
#include "core/startup_timeline.h"

namespace amt {

namespace {

QString formatElapsed(int totalSeconds)
{
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

QString phaseBadge(const SessionState &state)
{
    if (state.continuationPending) {
        return "Needs continuation";
    }
    if (state.paused || state.phase == Phase::Paused) {
        return "Paused";
    }
    return toString(state.phase);
}

bool hasUserMessage(const SessionState &state)
{
    for (const auto &entry : state.transcript) {
        if (entry.isUser && !entry.content.trimmed().isEmpty()) {
            return true;
        }
    }
    return false;
}

QString attachmentImportRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/attachments";
}

QString defaultSeatColor(int index)
{
    static const QStringList colors{
        "#49bd99", "#e0a44d", "#6fa8dc", "#c27ba0",
        "#8e7cc3", "#76a5af", "#cc7a6f", "#93c47d"
    };
    return colors.at(qBound(0, index, colors.size() - 1));
}

QString logEventTypeLabel(LogEventType type)
{
    switch (type) {
    case LogEventType::SessionStarted: return "Session";
    case LogEventType::UserMessageAdded: return "User";
    case LogEventType::PhaseStarted: return "Phase";
    case LogEventType::TurnStarted: return "Turn";
    case LogEventType::AISpoke: return "AI";
    case LogEventType::AISkipped: return "Skip";
    case LogEventType::ProviderCallFailed: return "Provider Error";
    case LogEventType::RetryScheduled: return "Retry";
    case LogEventType::PhaseEnded: return "Phase";
    case LogEventType::FinalDecisionMade: return "Decision";
    case LogEventType::SessionStopped: return "Stopped";
    case LogEventType::LimitReached: return "Limit";
    }
    return "Log";
}

QString attachmentImportErrorMessage(AttachmentImportStatus status)
{
    switch (status) {
    case AttachmentImportStatus::TooLarge:
        return "Attachments must be 25 MiB or smaller.";
    case AttachmentImportStatus::InsufficientStorage:
        return "Not enough storage is available to import this attachment safely.";
    case AttachmentImportStatus::ProviderFailure:
        return "The selected attachment could not be read.";
    case AttachmentImportStatus::DestinationFailure:
    case AttachmentImportStatus::HashFailure:
    case AttachmentImportStatus::RenameFailure:
        return "The attachment could not be saved to app storage.";
    case AttachmentImportStatus::Timeout:
        return "Attachment import timed out because no data was received.";
    case AttachmentImportStatus::Cancelled:
        return "Attachment import was cancelled.";
    case AttachmentImportStatus::Success:
        return {};
    }
    return "Attachment import failed.";
}

} // namespace

MobileAppController::MobileAppController(QObject *parent)
    : QObject(parent)
{
    connect(m_context.sessionRunner(), &SessionRunner::sessionStateChanged, this, [this](const SessionState &state) {
        notifyStateChange(state, true);
        schedulePersistence(state.tableId);
    });
    connect(m_context.sessionRunner(), &SessionRunner::continuationRequested, this, [this](const QString &tableId, const QString &reason, int) {
        if (tableId == m_currentTableId) {
            emit continuationRequested(reason);
            emit stateChanged();
        }
    });
    connect(m_context.modelCatalogManager(), &ModelCatalogManager::statusesChanged, this, [this]() {
        emit settingsChanged();
    });
    connect(&m_attachmentImportManager,
            &AttachmentImportManager::importFinished,
            this,
            &MobileAppController::handleAttachmentImportFinished);
}

bool MobileAppController::initialized() const
{
    return m_initialized;
}

bool MobileAppController::running() const
{
    const auto *state = currentState();
    return state && (isRunningPhase(state->phase) || state->waitingForNextTurn) && !state->paused;
}

QString MobileAppController::currentTableId() const
{
    return m_currentTableId;
}

bool MobileAppController::attachmentImportInProgress() const
{
    return m_attachmentImportInProgress;
}

QString MobileAppController::attachmentImportStatus() const
{
    return m_attachmentImportStatus;
}

bool MobileAppController::initialize()
{
    if (m_initialized) {
        return true;
    }
    if (!m_context.initialize()) {
        setError("Failed to initialize local storage.");
        return false;
    }
    QSettings settings;
    m_currentTableId = settings.value("mobile/currentTableId").toString();
    qCDebug(diagnosticsLog).noquote()
        << QString("Persistence restore: saved selection exists=%1")
               .arg((!m_currentTableId.isEmpty() && m_context.tableHandle(m_currentTableId)) ? "true" : "false");
    selectFirstTableIfNeeded();
    QElapsedTimer snapshotTimer;
    snapshotTimer.start();
    for (const auto &table : m_context.tables()) {
        if (table) {
            m_uiSnapshots.insert(table->tableId, uiSnapshot(*table));
        }
    }
    StartupTimeline::instance().mark(StartupStage::ControllerSnapshotConstruction,
                                     snapshotTimer.elapsed());
    m_initialized = true;
    if (const auto *state = currentState()) {
        qCDebug(diagnosticsLog).noquote() << QString("Persistence restore: selected transcript=%1 artifacts=%2 logs=%3")
                                 .arg(QString::number(state->transcript.size()),
                                      QString::number(state->artifacts.size()),
                                      QString::number(state->log.size()));
    } else {
        qWarning().noquote() << "Persistence restore: no current table selected";
    }
    emit initializedChanged();
    emit tablesChanged();
    emit stateChanged();
    emit seatsChanged();
    emit transcriptChanged();
    emit attachmentsChanged();
    emit artifactsChanged();
    emit logsChanged();
    return true;
}

void MobileAppController::startupInitialRefreshStarted()
{
    StartupTimeline::instance().beginInitialRefresh();
}

void MobileAppController::startupInitialRefreshCompleted()
{
    StartupTimeline::instance().completeInitialRefresh();
}

void MobileAppController::startupTranscriptVisualStable()
{
    StartupTimeline::instance().markTranscriptVisualStable();
}

void MobileAppController::startupPrimaryControlsReady()
{
    StartupTimeline::instance().markPrimaryControlsReady();
}

QVariantList MobileAppController::tables() const
{
    QVariantList rows;
    QVector<const SessionState *> sorted;
    for (const auto &table : m_context.tables()) {
        if (table) {
            sorted.append(table.get());
        }
    }
    std::sort(sorted.begin(), sorted.end(), [](const SessionState *lhs, const SessionState *rhs) {
        if (lhs->pinned != rhs->pinned) {
            return lhs->pinned && !rhs->pinned;
        }
        if (lhs->updatedAt != rhs->updatedAt) {
            return lhs->updatedAt > rhs->updatedAt;
        }
        return lhs->title.toLower() < rhs->title.toLower();
    });
    for (const auto *state : sorted) {
        rows.append(tableSummary(*state));
    }
    return sanitizedRows(rows);
}

QVariantMap MobileAppController::currentTable() const
{
    const auto *state = currentState();
    return state ? tableSummary(*state) : QVariantMap{};
}

QVariantList MobileAppController::seats() const
{
    QVariantList rows;
    const auto *state = currentState();
    if (!state) {
        return rows;
    }
    const QVector<SeatConfig> source = hasPendingSeatChanges(*state) ? state->pendingSeats : state->seats;
    for (int i = 0; i < source.size(); ++i) {
        rows.append(seatSummary(source.at(i), i));
    }
    return sanitizedRows(rows);
}

QVariantList MobileAppController::transcript() const
{
    QVariantList rows;
    const auto *state = currentState();
    if (!state) {
        return rows;
    }
    for (const auto &entry : state->transcript) {
        rows.append(transcriptSummary(entry));
    }
    return sanitizedRows(rows);
}

QVariantList MobileAppController::attachments() const
{
    QVariantList rows;
    const auto *state = currentState();
    if (!state) {
        return rows;
    }
    for (const auto &attachment : state->attachments) {
        rows.append(attachmentSummary(attachment));
    }
    return sanitizedRows(rows);
}

QString MobileAppController::fullTranscriptText() const
{
    const auto *state = currentState();
    if (!state || state->transcript.isEmpty()) {
        return {};
    }

    QStringList entries;
    const auto secrets = presentationSecrets();
    entries.reserve(state->transcript.size());
    for (const auto &entry : state->transcript) {
        const QString speaker = entry.isUser ? QStringLiteral("You") : entry.speakerName;
        entries.append(QStringLiteral("[%1] %2 | %3 | Round %4\n%5")
                           .arg(entry.timestamp.toLocalTime().toString("HH:mm:ss"),
                                visibleText(speaker, secrets),
                                toString(entry.phase),
                                QString::number(entry.round),
                                visibleText(entry.content, secrets)));
    }
    return entries.join("\n\n");
}

bool MobileAppController::copyFullTranscript()
{
    const QString text = fullTranscriptText();
    if (text.isEmpty()) {
        setError("There is no transcript to copy.");
        return false;
    }
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        setError("The system clipboard is unavailable.");
        return false;
    }
    clipboard->setText(text, QClipboard::Clipboard);
    setError({});
    return true;
}

QVariantList MobileAppController::artifacts() const
{
    QVariantList rows;
    const auto *state = currentState();
    if (!state) {
        return rows;
    }
    for (const auto &artifact : state->artifacts) {
        rows.append(artifactSummary(artifact));
    }
    return sanitizedRows(rows);
}

QString MobileAppController::artifactContent(const QString &versionId) const
{
    const auto *state = currentState();
    if (!state) {
        return {};
    }

    const auto it = std::find_if(state->artifacts.cbegin(), state->artifacts.cend(), [&versionId](const ArtifactVersion &artifact) {
        return artifact.versionId == versionId;
    });
    if (it == state->artifacts.cend()) {
        return {};
    }

    QFile file(it->filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    constexpr qint64 maxArtifactPreviewBytes = 65536;
    QString content = QString::fromUtf8(file.read(maxArtifactPreviewBytes));
    if (file.size() > maxArtifactPreviewBytes) {
        content += QString("\n\n[Artifact preview truncated. Full artifact is %1 bytes.]").arg(file.size());
    }
    return visibleText(content, presentationSecrets());
}

QVariantList MobileAppController::logs() const
{
    QVariantList rows;
    const auto *state = currentState();
    if (!state) {
        return rows;
    }
    for (const auto &event : state->log) {
        rows.append(logSummary(event));
    }
    return sanitizedRows(rows);
}

QVariantList MobileAppController::modelsForProvider(int providerIndex) const
{
    QVariantList rows;
    for (const auto &entry : m_context.modelCatalogManager()->catalogForProvider(providerFromIndex(providerIndex))) {
        QVariantMap row;
        row.insert("id", entry.id);
        row.insert("displayName", entry.displayName);
        row.insert("supportsEffort", entry.supportsEffort);
        row.insert("preview", entry.isPreview);
        rows.append(row);
    }
    return rows;
}

QVariantList MobileAppController::modelRefreshStatuses() const
{
    return m_context.modelCatalogManager()->fetchStatuses();
}

QVariantMap MobileAppController::settings() const
{
    const auto &settings = m_context.appSettings();
    const auto &budget = settings.globalBudgetDefaults;
    return {
        {"appearance", toString(settings.theme)},
        {"colorTheme", settings.colorTheme},
        {"fontStyle", settings.fontStyle},
        {"quickGuideSeen", QSettings().value("ui/quickGuideSeen", false).toBool()},
        {"maxTokensPerPhase", budget.maxTokensPerPhase},
        {"maxTotalTokens", budget.maxTotalTokens},
        {"maxRounds", budget.maxRounds},
        {"maxExecQcLoops", budget.maxExecQcLoops},
        {"maxPhaseSeconds", budget.maxPhaseSeconds},
        {"maxSessionSeconds", budget.maxSessionSeconds}
    };
}

QVariantMap MobileAppController::attachmentSafeguards() const
{
    return {
        {"maximumAttachmentMiB", AttachmentImportManager::maximumAttachmentBytes / (1024 * 1024)},
        {"freeSpaceReserveMiB", AttachmentImportManager::freeSpaceReserveBytes / (1024 * 1024)},
        {"noProgressTimeoutSeconds", AttachmentImportManager::noProgressTimeoutMs / 1000}
    };
}

bool MobileAppController::hasCredential(int providerIndex) const
{
    return !m_context.credentialStore()
                ->loadApiKey(providerFromIndex(providerIndex))
                .trimmed()
                .isEmpty();
}

QString MobileAppController::lastError() const
{
    return m_lastError;
}

void MobileAppController::selectTable(const QString &tableId)
{
    if (!m_context.tableHandle(tableId)) {
        return;
    }
    m_currentTableId = tableId;
    QSettings settings;
    settings.setValue("mobile/currentTableId", m_currentTableId);
    settings.sync();
    if (const auto *state = currentState()) {
        m_uiSnapshots.insert(state->tableId, uiSnapshot(*state));
    }
    emit stateChanged();
    emit tablesChanged();
    emit seatsChanged();
    emit transcriptChanged();
    emit attachmentsChanged();
    emit artifactsChanged();
    emit logsChanged();
}

bool MobileAppController::createTable(const QString &title)
{
    if (!m_initialized) {
        setError("Local storage must initialize successfully before creating a table.");
        return false;
    }
    SessionState state;
    state.tableId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state.title = title.trimmed().isEmpty() ? "New table" : title.trimmed();
    state.updatedAt = QDateTime::currentDateTimeUtc();
    state.phase = Phase::Idle;
    state.round = 1;
    state.logVisible = false;
    m_context.applyEffectiveBudgetPolicy(state);
    if (!m_context.save(state)) {
        setError("The table could not be created.");
        return false;
    }
    selectTable(state.tableId);
    return true;
}

bool MobileAppController::duplicateCurrentTable()
{
    auto *state = currentState();
    if (!state) {
        setError("No table is selected.");
        return false;
    }
    SessionState copy = *state;
    copy.tableId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.title = state->title + " Copy";
    copy.updatedAt = QDateTime::currentDateTimeUtc();
    copy.phase = Phase::Idle;
    copy.round = 1;
    copy.execQcLoopCount = 0;
    copy.elapsedSeconds = 0;
    copy.phaseElapsedSeconds = 0;
    copy.usedTokens = 0;
    copy.usedCost = 0.0;
    copy.phaseUsedTokens = 0;
    copy.phaseUsedCost = 0.0;
    copy.seatUsage.clear();
    copy.usageEstimateUsed = false;
    copy.costEstimateComplete = true;
    copy.pendingResearchResponses = 0;
    copy.arbitrationSatisfied = false;
    if (hasPendingSeatChanges(copy)) copy.seats = copy.pendingSeats;
    copy.pendingSeats.clear();
    copy.finalDecisionMakerSeatId = findFinalDecisionMakerSeatId(copy.seats);
    copy.activeSeatId.clear();
    copy.transcript.clear();
    copy.log.clear();
    copy.artifacts.clear();
    copy.attachments.clear();
    copy.queuedInputIds.clear();
    copy.currentArtifactVersionId.clear();
    copy.waitingForNextTurn = false;
    copy.paused = false;
    copy.pauseRequested = false;
    copy.continuationPending = false;
    copy.continuationLimitKind = 0;
    copy.continuationReason.clear();
    copy.continuationCommand = {};
    copy.pausedResumePhase = Phase::Idle;
    if (!m_context.save(copy)) {
        setError("The table copy could not be saved.");
        return false;
    }
    selectTable(copy.tableId);
    return true;
}

bool MobileAppController::renameCurrentTable(const QString &title)
{
    auto *state = currentState();
    const QString trimmed = title.trimmed();
    if (!state || trimmed.isEmpty()) {
        setError("A table name is required.");
        return false;
    }
    SessionState candidate = *state;
    candidate.title = trimmed;
    return saveAndNotify(candidate, true);
}

bool MobileAppController::deleteCurrentTable()
{
    const QString tableId = m_currentTableId;
    if (tableId.isEmpty()) {
        return false;
    }
    if (m_attachmentImportInProgress && m_attachmentImportTableId == tableId) {
        m_suppressAttachmentCancellationError = true;
        m_attachmentImportStatus = "Cancelling attachment import...";
        m_attachmentImportManager.cancelActive();
        emit attachmentImportChanged();
    }
    if (!m_context.removeTable(tableId)) {
        setError("The table could not be deleted.");
        return false;
    }
    m_context.sessionRunner()->discardSession(tableId);
    m_uiSnapshots.remove(tableId);
    m_pendingSaveIds.remove(tableId);
    m_currentTableId.clear();
    selectFirstTableIfNeeded();
    emit tablesChanged();
    emit stateChanged();
    emit seatsChanged();
    emit transcriptChanged();
    emit attachmentsChanged();
    emit artifactsChanged();
    emit logsChanged();
    return true;
}

bool MobileAppController::togglePinCurrentTable()
{
    auto *state = currentState();
    if (!state) {
        return false;
    }
    SessionState candidate = *state;
    candidate.pinned = !candidate.pinned;
    return saveAndNotify(candidate, true);
}

bool MobileAppController::saveSeat(int seatIndex,
                                   bool occupied,
                                   const QString &displayName,
                                   int providerIndex,
                                   const QString &modelId,
                                   int effortIndex,
                                   int roleIndex,
                                   const QString &color)
{
    auto *state = currentState();
    if (!state || seatIndex < 0 || seatIndex >= 8) {
        setError("Invalid seat.");
        return false;
    }
    SessionState candidate = *state;
    QVector<SeatConfig> targetSeats = hasPendingSeatChanges(candidate) ? candidate.pendingSeats : candidate.seats;
    if (seatIndex > targetSeats.size()) {
        setError("Invalid seat.");
        return false;
    }
    SeatConfig seat;
    seat.seatId = QString("seat-%1").arg(seatIndex + 1);
    seat.displayName = displayName.trimmed().isEmpty() ? QString("Seat %1").arg(seatIndex + 1) : displayName.trimmed();
    seat.occupied = occupied;
    seat.enabled = occupied;
    seat.provider = providerFromIndex(providerIndex);
    seat.modelId = modelId;
    seat.modelPreset = preferredModelDisplayName(seat.provider, modelId, modelId);
    seat.effort = effortFromEditorIndex(effortIndex);
    seat.role = roleFromEditorIndex(roleIndex);
    static const QRegularExpression colorPattern("^#[0-9a-fA-F]{6}$");
    seat.color = colorPattern.match(color).hasMatch() ? color.toLower() : defaultSeatColor(seatIndex);
    if (!occupied) {
        seat.role = Role::None;
        seat.effort = ModelEffort::Auto;
    }
    normalizeSeatModel(seat);
    if (seatIndex == targetSeats.size()) {
        targetSeats.append(seat);
    } else {
        targetSeats[seatIndex] = seat;
    }
    const QString roleError = validateSeatRoleAssignments(targetSeats);
    const bool anyOccupied = std::any_of(targetSeats.cbegin(), targetSeats.cend(), [](const SeatConfig &item) {
        return item.occupied && item.enabled;
    });
    if (anyOccupied && !roleError.isEmpty()) {
        setError(roleError);
        return false;
    }
    if (isRunningPhase(candidate.phase)) {
        candidate.pendingSeats = targetSeats;
    } else {
        candidate.seats = targetSeats;
        candidate.pendingSeats.clear();
        candidate.finalDecisionMakerSeatId = findFinalDecisionMakerSeatId(candidate.seats);
    }
    return saveAndNotify(candidate);
}

bool MobileAppController::sendMessage(const QString &message)
{
    auto *state = currentState();
    const QString trimmed = message.trimmed();
    if (!state || trimmed.isEmpty()) {
        setError("Enter a message first.");
        return false;
    }
    if (state->continuationCommand.payload.value("outcomeUnknown").toBool()) {
        setError("This session is locked because provider work is unconfirmed. Create a fresh table to start a new task.");
        return false;
    }
    SessionState candidate = *state;
    TranscriptEntry entry;
    entry.entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.tableId = candidate.tableId;
    entry.phase = candidate.phase;
    entry.round = candidate.round;
    entry.speakerSeatId = "user";
    entry.speakerName = "You";
    entry.isUser = true;
    entry.content = trimmed;
    entry.timestamp = QDateTime::currentDateTimeUtc();
    candidate.transcript.append(entry);
    if (isRunningPhase(candidate.phase) || candidate.waitingForNextTurn) {
        candidate.queuedInputIds.append(entry.entryId);
    }
    LogEvent log;
    log.logId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    log.tableId = candidate.tableId;
    log.type = LogEventType::UserMessageAdded;
    log.actorName = "You";
    log.phase = candidate.phase;
    log.round = candidate.round;
    log.timestamp = QDateTime::currentDateTimeUtc();
    log.summary = "User added new instructions.";
    candidate.log.append(log);
    return saveAndNotify(candidate);
}

bool MobileAppController::submitTask(const QString &message)
{
    auto *state = currentState();
    if (!state || message.trimmed().isEmpty()) {
        setError("Enter a task first.");
        return false;
    }
    const bool start = state->phase == Phase::Idle;
    if (start) {
        SessionState candidate = *state;
        TranscriptEntry task;
        task.isUser = true;
        task.content = message.trimmed();
        candidate.transcript.append(task);
        if (!validateRunnable(candidate)) return false;
        for (const auto &seat : candidate.seats) {
            if (seat.occupied && seat.enabled && !hasCredential(indexFromProviderKind(seat.provider))) {
                setError(QString("Save an API key for %1 in Settings first.").arg(toString(seat.provider)));
                return false;
            }
        }
    }
    if (!sendMessage(message)) return false;
    if (start) m_context.sessionRunner()->startSession(*currentState());
    return true;
}

bool MobileAppController::runOrResume()
{
    auto *state = currentState();
    if (!state) {
        return false;
    }
    if (state->continuationCommand.payload.value("outcomeUnknown").toBool()) {
        setError("The provider outcome is unknown. This operation cannot be replayed.");
        return false;
    }
    if (state->continuationPending) {
        m_context.sessionRunner()->grantContinuation(*state, static_cast<BudgetLimitKind>(state->continuationLimitKind));
        m_context.sessionRunner()->resumeSession(*state);
        return true;
    }
    if (state->paused || state->phase == Phase::Paused) {
        if (state->continuationCommand.commandType == RunnerCommandType::None) {
            setError("No pending operation was saved. Create a fresh table; this session cannot safely resume.");
            return false;
        }
        m_context.sessionRunner()->resumeSession(*state);
        return true;
    }
    if (!validateRunnable(*state)) {
        return false;
    }
    m_context.sessionRunner()->startSession(*state);
    return true;
}

bool MobileAppController::pauseSession()
{
    auto *state = currentState();
    if (!state) {
        return false;
    }
    m_context.sessionRunner()->requestPause(*state);
    return true;
}

bool MobileAppController::stopSession()
{
    auto *state = currentState();
    if (!state) {
        return false;
    }
    m_context.sessionRunner()->stopSession(*state, "Stopped from Android app.");
    return true;
}

bool MobileAppController::addAttachment(const QUrl &url)
{
    return addAttachmentForTable(m_currentTableId, url);
}

bool MobileAppController::addAttachmentForTable(const QString &tableId, const QUrl &url)
{
    const auto state = m_context.tableHandle(tableId);
    if (!state) {
        setError("No table is selected.");
        return false;
    }
    if (state->continuationCommand.payload.value("outcomeUnknown").toBool()) {
        setError("This session is locked because provider work is unconfirmed. Create a fresh table to start a new task.");
        return false;
    }
    if (m_attachmentImportInProgress) {
        setError("An attachment import is already in progress.");
        return false;
    }

    const QString operationId = m_attachmentImportManager.startImport(
        state->tableId, url, attachmentImportRoot());
    if (operationId.isEmpty()) {
        setError("Attachment import could not be started.");
        return false;
    }

    m_attachmentImportOperationId = operationId;
    m_attachmentImportTableId = state->tableId;
    m_attachmentImportStatus = "Importing attachment...";
    m_attachmentImportInProgress = true;
    m_suppressAttachmentCancellationError = false;
    setError({});
    emit attachmentImportChanged();
    return true;
}

bool MobileAppController::cancelAttachmentImport()
{
    if (!m_attachmentImportInProgress) {
        return false;
    }
    if (!m_attachmentImportManager.cancelActive()) {
        return false;
    }
    m_attachmentImportStatus = "Cancelling attachment import...";
    emit attachmentImportChanged();
    return true;
}

bool MobileAppController::removeAttachment(const QString &attachmentId)
{
    auto *state = currentState();
    if (!state) {
        return false;
    }
    if (state->continuationCommand.payload.value("outcomeUnknown").toBool()) {
        setError("This session is locked because provider work is unconfirmed. Create a fresh table to start a new task.");
        return false;
    }
    SessionState candidate = *state;
    QString removedFilePath;
    const auto newEnd = std::remove_if(candidate.attachments.begin(), candidate.attachments.end(), [&](const AttachmentRecord &attachment) {
        if (attachment.attachmentId == attachmentId) {
            removedFilePath = attachment.filePath;
            return true;
        }
        return attachment.attachmentId == attachmentId;
    });
    if (newEnd == candidate.attachments.end()) {
        return false;
    }
    candidate.attachments.erase(newEnd, candidate.attachments.end());
    candidate.queuedInputIds.removeAll(attachmentId);
    if (!saveAndNotify(candidate)) {
        return false;
    }
    m_context.cleanupAttachmentFileIfUnreferenced(removedFilePath);
    return true;
}

bool MobileAppController::openAttachment(const QString &attachmentId)
{
    const auto *state = currentState();
    if (!state) {
        setError("No table is selected.");
        return false;
    }
    const auto attachment = std::find_if(
        state->attachments.cbegin(), state->attachments.cend(),
        [&attachmentId](const AttachmentRecord &record) {
            return record.attachmentId == attachmentId;
        });
    if (attachment == state->attachments.cend()) {
        setError("The attachment is no longer available.");
        return false;
    }
    switch (AttachmentImportManager::openImportedAttachment(
        attachment->filePath, attachment->displayName)) {
    case AttachmentOpenStatus::Opened:
        setError({});
        return true;
    case AttachmentOpenStatus::FileMissing:
        setError("The imported attachment file no longer exists.");
        break;
    case AttachmentOpenStatus::NoCompatibleApplication:
        setError("No compatible application can open this attachment.");
        break;
    case AttachmentOpenStatus::AccessDenied:
        setError("Android could not grant access to this attachment.");
        break;
    }
    return false;
}

bool MobileAppController::saveApiKey(int providerIndex, const QString &apiKey)
{
    QString error;
    if (!m_context.credentialStore()->saveApiKey(providerFromIndex(providerIndex), apiKey, &error, true)) {
        setError(error);
        return false;
    }
    setError({});
    emit settingsChanged();
    return true;
}

void MobileAppController::refreshModels()
{
    m_context.modelCatalogManager()->fetchModelsAsync();
    emit settingsChanged();
}

void MobileAppController::refreshProviderModels(int providerIndex)
{
    if (providerIndex < 0 || providerIndex > 2) return;
    m_context.modelCatalogManager()->fetchModelsAsync(providerFromIndex(providerIndex));
}

bool MobileAppController::acknowledgeQuickGuide()
{
    QSettings settings;
    settings.setValue("ui/quickGuideSeen", true);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError("Could not save the quick-guide preference.");
        return false;
    }
    emit settingsChanged();
    return true;
}

bool MobileAppController::removeAgent(int seatIndex)
{
    const auto *state = currentState();
    if (!state) return false;
    const auto &seats = hasPendingSeatChanges(*state) ? state->pendingSeats : state->seats;
    if (seatIndex < 0 || seatIndex >= seats.size()) {
        setError("Invalid agent.");
        return false;
    }
    // Reuse the existing validated inactive-slot path; preserve stable IDs/history.
    const auto seat = seats[seatIndex];
    return saveSeat(seatIndex, false, seat.displayName, indexFromProviderKind(seat.provider),
                    seat.modelId, 0, 0, seat.color);
}

void MobileAppController::setTheme(const QString &theme)
{
    m_context.appSettings().theme = themeModeFromString(theme);
    m_context.saveAppSettings();
    emit settingsChanged();
}

bool MobileAppController::saveAppearance(const QString &appearance,
                                         const QString &colorTheme,
                                         const QString &fontStyle)
{
    const QStringList appearances{"System", "Light", "Dark"};
    const QStringList colorThemes{"Signal Session", "Calm Workspace"};
    const QStringList fontStyles{"System", "Workspace", "Console"};
    if (!appearances.contains(appearance)
        || !colorThemes.contains(colorTheme)
        || !fontStyles.contains(fontStyle)) {
        setError("Choose a supported appearance, color theme, and font style.");
        return false;
    }
    auto &settings = m_context.appSettings();
    settings.theme = themeModeFromString(appearance);
    settings.colorTheme = colorTheme;
    settings.fontStyle = fontStyle;
    m_context.saveAppSettings();
    setError({});
    emit settingsChanged();
    return true;
}

bool MobileAppController::saveGlobalBudget(int maxTokensPerPhase,
                                           int maxTotalTokens,
                                           int maxRounds,
                                           int maxExecQcLoops,
                                           int maxPhaseSeconds,
                                           int maxSessionSeconds)
{
    if (maxTokensPerPhase <= 0
        || maxTotalTokens <= 0
        || maxRounds <= 0
        || maxExecQcLoops <= 0
        || maxPhaseSeconds <= 0
        || maxSessionSeconds <= 0) {
        setError("All hard-stop limits must be positive values.");
        return false;
    }
    if (maxTotalTokens < maxTokensPerPhase) {
        setError("Maximum total tokens must be at least the per-phase token limit.");
        return false;
    }
    if (maxSessionSeconds < maxPhaseSeconds) {
        setError("Maximum session seconds must be at least the per-phase time limit.");
        return false;
    }

    BudgetPolicy policy = m_context.appSettings().globalBudgetDefaults;
    policy.maxTokensPerPhase = maxTokensPerPhase;
    policy.maxTotalTokens = maxTotalTokens;
    policy.maxRounds = maxRounds;
    policy.maxExecQcLoops = maxExecQcLoops;
    policy.maxPhaseSeconds = maxPhaseSeconds;
    policy.maxSessionSeconds = maxSessionSeconds;

    QVector<SessionState> originals;
    QVector<SessionState> candidates;
    for (const auto &table : m_context.tables()) {
        if (table && !table->useBudgetOverrides) {
            originals.append(*table);
            SessionState candidate = *table;
            candidate.budgetPolicy = policy;
            candidates.append(candidate);
        }
    }
    for (int i = 0; i < candidates.size(); ++i) {
        if (m_context.save(candidates.at(i))) {
            continue;
        }
        for (int rollbackIndex = 0; rollbackIndex < i; ++rollbackIndex) {
            m_context.save(originals.at(rollbackIndex));
        }
        setError("The hard-stop settings could not be saved to every table.");
        return false;
    }

    auto &settings = m_context.appSettings();
    settings.globalBudgetDefaults = policy;
    m_context.saveAppSettings();
    emit settingsChanged();
    emit stateChanged();
    return true;
}

bool MobileAppController::flushCurrentSession()
{
    QSet<QString> pending = m_pendingSaveIds;
    if (!m_currentTableId.isEmpty()) pending.insert(m_currentTableId);
    bool saved = true;
    for (const auto &id : pending) {
        if (!m_context.tableHandle(id)) { m_pendingSaveIds.remove(id); continue; }
        if (m_context.saveExisting(id)) m_pendingSaveIds.remove(id);
        else { m_pendingSaveIds.insert(id); saved = false; }
    }
    QSettings settings;
    settings.setValue("mobile/currentTableId", m_currentTableId);
    settings.sync();
    return saved && settings.status() == QSettings::NoError;
}

SessionState *MobileAppController::currentState() const
{
    const auto handle = currentHandle();
    return handle ? handle.get() : nullptr;
}

ApplicationContext::SessionHandle MobileAppController::currentHandle() const
{
    return m_currentTableId.isEmpty() ? nullptr : m_context.tableHandle(m_currentTableId);
}

void MobileAppController::selectFirstTableIfNeeded()
{
    if (!m_currentTableId.isEmpty() && m_context.tableHandle(m_currentTableId)) {
        return;
    }

    ApplicationContext::SessionHandle newestWithContent;
    ApplicationContext::SessionHandle newest;
    for (const auto &table : m_context.tables()) {
        if (!table) {
            continue;
        }
        const bool hasContent = !table->transcript.isEmpty() || !table->artifacts.isEmpty() || !table->log.isEmpty();
        if (hasContent && (!newestWithContent || table->updatedAt > newestWithContent->updatedAt)) {
            newestWithContent = table;
        }
        if (!newest || table->updatedAt > newest->updatedAt) {
            newest = table;
        }
    }

    const auto selected = newestWithContent ? newestWithContent : newest;
    if (selected) {
        m_currentTableId = selected->tableId;
        QSettings settings;
        settings.setValue("mobile/currentTableId", m_currentTableId);
        settings.sync();
        qCDebug(diagnosticsLog).noquote() << QString("Persistence restore: selected fallback reason=%1 transcript=%2 artifacts=%3 logs=%4")
                                 .arg(newestWithContent ? "newest with content" : "newest",
                                      QString::number(selected->transcript.size()),
                                      QString::number(selected->artifacts.size()),
                                      QString::number(selected->log.size()));
    }
}

bool MobileAppController::saveAndNotify(const SessionState &state, bool tableListChanged)
{
    SessionState candidate = state;
    m_context.applyEffectiveBudgetPolicy(candidate);
    if (!m_context.save(candidate)) {
        setError("The current table could not be saved.");
        return false;
    }
    const auto persisted = m_context.tableHandle(candidate.tableId);
    if (persisted) {
        notifyStateChange(*persisted, tableListChanged);
    }
    return true;
}

void MobileAppController::schedulePersistence(const QString &tableId)
{
    if (tableId.isEmpty()) {
        return;
    }
    m_pendingSaveIds.insert(tableId);
    if (m_persistenceScheduled) {
        return;
    }

    m_persistenceScheduled = true;
    QTimer::singleShot(0, this, [this]() { persistScheduledSessions(); });
}

void MobileAppController::persistScheduledSessions()
{
    const QSet<QString> pending = m_pendingSaveIds;
    m_pendingSaveIds.clear();
    m_persistenceScheduled = false;
    for (const auto &tableId : pending) {
        if (!m_context.saveExisting(tableId) && m_context.tableHandle(tableId)) {
            m_pendingSaveIds.insert(tableId);
            setError("A table could not be saved. Pending changes will be retried on flush.");
        }
    }
}

void MobileAppController::notifyStateChange(const SessionState &state, bool tableListChanged)
{
    const UiSnapshot previous = m_uiSnapshots.value(state.tableId);
    const UiSnapshot current = uiSnapshot(state);
    m_uiSnapshots.insert(state.tableId, current);

    if (tableListChanged) {
        emit tablesChanged();
    }
    if (state.tableId != m_currentTableId) {
        return;
    }

    emit stateChanged();
    const bool seatConfigurationChanged = previous.seatConfiguration != current.seatConfiguration;
    if (previous.activeSeatId != current.activeSeatId || seatConfigurationChanged) {
        emit seatsChanged();
    }
    if (previous.transcriptCount != current.transcriptCount || seatConfigurationChanged) {
        emit transcriptChanged();
    }
    if (previous.attachmentCount != current.attachmentCount) {
        emit attachmentsChanged();
    }
    if (previous.artifactCount != current.artifactCount) {
        emit artifactsChanged();
    }
    if (previous.logCount != current.logCount) {
        emit logsChanged();
    }
}

MobileAppController::UiSnapshot MobileAppController::uiSnapshot(const SessionState &state) const
{
    UiSnapshot snapshot;
    snapshot.transcriptCount = state.transcript.size();
    snapshot.attachmentCount = state.attachments.size();
    snapshot.artifactCount = state.artifacts.size();
    snapshot.logCount = state.log.size();
    snapshot.activeSeatId = state.activeSeatId;
    snapshot.seatConfiguration = QJsonDocument(seatsToJson(state.seats)).toJson(QJsonDocument::Compact);
    snapshot.seatConfiguration.append('\0');
    snapshot.seatConfiguration.append(QJsonDocument(seatsToJson(state.pendingSeats)).toJson(QJsonDocument::Compact));
    return snapshot;
}

bool MobileAppController::validateRunnable(const SessionState &state)
{
    int participantCount = 0;
    for (const auto &seat : state.seats) {
        if (!seat.occupied || !seat.enabled) {
            continue;
        }
        if (!hasConcreteModelSelection(seat)) {
            setError(QString("%1 needs a model selection.").arg(displaySeatName(seat)));
            return false;
        }
        if (seat.role != Role::FinalDecisionMaker) {
            participantCount += 1;
        }
    }
    const QString roleError = validateSeatRoleAssignments(state.seats);
    if (!roleError.isEmpty()) {
        setError(roleError);
        return false;
    }
    if (participantCount == 0) {
        setError("At least one non-final participant is required.");
        return false;
    }
    if (!hasUserMessage(state)) {
        setError("Send a user message before running the session.");
        return false;
    }
    return true;
}

ProviderKind MobileAppController::providerFromIndex(int providerIndex) const
{
    return providerKindFromIndex(providerIndex);
}

QVariantMap MobileAppController::tableSummary(const SessionState &state) const
{
    int inputTokens = 0;
    int outputTokens = 0;
    for (const auto &usage : state.seatUsage) {
        inputTokens += usage.inputTokens;
        outputTokens += usage.outputTokens;
    }
    QVariantMap row;
    row.insert("continuationPending", state.continuationPending);
    row.insert("continuationReason", visibleText(state.continuationReason));
    row.insert("canResume", !state.continuationCommand.payload.value("outcomeUnknown").toBool());
    const bool unknown = state.continuationCommand.payload.value("outcomeUnknown").toBool();
    QString action, label, hint;
    QString latestFailure;
    for (auto it = state.log.crbegin(); it != state.log.crend(); ++it) {
        if (it->type == LogEventType::SessionStarted) break;
        if (it->type == LogEventType::ProviderCallFailed) { latestFailure = visibleText(it->summary); break; }
    }
    if (unknown) {
        action = "fresh"; label = "Create fresh table";
        hint = "Provider work may have occurred. This session cannot be replayed. Create a fresh table to start a new task.";
    } else if (state.continuationPending) {
        action = "continue"; label = "Continue";
        hint = "A configured limit paused the session. Continue authorizes the pending operation once. Sending instructions does not resume it.";
    } else if (state.phase == Phase::Paused || state.paused) {
        if (state.continuationCommand.commandType == RunnerCommandType::None) {
            action = "fresh"; label = "Create fresh table";
            hint = "No pending operation was saved. Create a fresh table to proceed without guessing what to replay.";
        } else if (state.continuationCommand.payload.value("responseRejected").toBool()) {
            action = "retry"; label = "Retry operation";
            hint = "The returned response could not be used. Review Activity and provider settings. Retry explicitly sends this operation again and may incur additional usage.";
        } else {
            action = "resume"; label = "Resume";
            hint = "Resume the saved pending operation. Sending instructions alone does not resume the session.";
        }
    } else if (state.phase == Phase::Idle) {
        action = "start"; label = "Start task";
        hint = "Describe a task, then choose Start task. Configure your team and provider keys first.";
    } else if (state.phase == Phase::Completed || state.phase == Phase::Stopped || state.phase == Phase::Failed) {
        action = "restart"; label = "Run again";
        hint = latestFailure.isEmpty()
            ? "Review the results or choose Run again to explicitly start another run with the existing task and history."
            : "The run ended after a provider failure. Review Activity, Providers and models, and Team. After correcting the issue, Run again starts a new run, not just the failed turn.";
        if (state.phase == Phase::Completed && !state.artifacts.isEmpty()) {
            hint.prepend("Your final result is ready. Tap Team, then choose Artifacts to read it. ");
        }
    } else {
        action = "pause"; label = "Pause";
        hint = latestFailure.isEmpty() ? "Agents are working. Pause waits for in-flight work; Stop is in session details."
            : "A provider operation failed. The workflow is continuing without that response. Review Activity and provider settings, or Pause.";
    }
    row.insert("nextAction", action);
    row.insert("actionLabel", label);
    row.insert("actionHint", hint);
    row.insert("latestFailure", latestFailure);
    row.insert("canSubmit", !unknown);
    row.insert("tableId", state.tableId);
    row.insert("title", state.title);
    row.insert("pinned", state.pinned);
    row.insert("phase", phaseBadge(state));
    row.insert("statusLabel", unknown ? QString("Paused · outcome unknown") : phaseBadge(state));
    row.insert("round", state.round);
    row.insert("activeSeatId", state.activeSeatId);
    row.insert("usedTokens", state.usedTokens);
    row.insert("inputTokens", inputTokens);
    row.insert("outputTokens", outputTokens);
    row.insert("tokenBreakdownKnown",
               state.usedTokens == 0 || inputTokens + outputTokens > 0);
    row.insert("usageEstimated", state.usageEstimateUsed);
    row.insert("maxTokens", state.budgetPolicy.maxTotalTokens);
    row.insert("elapsed", formatElapsed(state.elapsedSeconds));
    row.insert("transcriptCount", state.transcript.size());
    row.insert("attachmentCount", state.attachments.size());
    row.insert("artifactCount", state.artifacts.size());
    row.insert("updatedAt", state.updatedAt.toLocalTime().toString("yyyy-MM-dd hh:mm"));
    row.insert("selected", state.tableId == m_currentTableId);
    return row;
}

QVariantMap MobileAppController::seatSummary(const SeatConfig &seat, int index) const
{
    const auto *state = currentState();
    QVariantMap row;
    row.insert("seatId", seat.seatId);
    row.insert("index", index);
    row.insert("displayName", displaySeatName(seat, index));
    row.insert("providerIndex", indexFromProviderKind(seat.provider));
    row.insert("provider", toString(seat.provider));
    row.insert("modelId", effectiveModelId(seat));
    row.insert("model", effectiveModelName(seat));
    row.insert("effortIndex", indexFromEffort(seat.effort));
    row.insert("effort", toString(seat.effort));
    row.insert("roleIndex", indexFromRole(seat.role));
    row.insert("role", displaySeatRole(seat.role));
    row.insert("color", seat.color.isEmpty() ? defaultSeatColor(index) : seat.color);
    row.insert("occupied", seat.occupied);
    row.insert("enabled", seat.enabled);
    row.insert("active", state && state->activeSeatId == seat.seatId);
    row.insert("decisionMaker", seat.role == Role::FinalDecisionMaker);
    row.insert("pending", state && hasPendingSeatChanges(*state));
    return row;
}

QVariantMap MobileAppController::transcriptSummary(const TranscriptEntry &entry) const
{
    QVariantMap row;
    row.insert("entryId", entry.entryId);
    row.insert("speaker", entry.isUser ? "You" : entry.speakerName);
    row.insert("content", entry.content);
    row.insert("phase", toString(entry.phase));
    row.insert("round", entry.round);
    row.insert("isUser", entry.isUser);
    row.insert("isDecision", entry.isDecision);
    row.insert("timestamp", entry.timestamp.toLocalTime().toString("HH:mm:ss"));
    row.insert("seatId", entry.speakerSeatId);
    row.insert("role", "Participant");
    row.insert("model", entry.isUser ? "Local input" : "Unknown model");
    row.insert("color", "#6fa8dc");
    const auto *state = currentState();
    if (state && !entry.isUser) {
        const auto seat = std::find_if(state->seats.cbegin(), state->seats.cend(), [&entry](const SeatConfig &candidate) {
            return candidate.seatId == entry.speakerSeatId;
        });
        if (seat != state->seats.cend()) {
            const int index = static_cast<int>(std::distance(state->seats.cbegin(), seat));
            row.insert("role", displaySeatRole(seat->role));
            row.insert("model", effectiveModelName(*seat));
            row.insert("color", seat->color.isEmpty() ? defaultSeatColor(index) : seat->color);
        }
    }
    return row;
}

QVariantMap MobileAppController::attachmentSummary(const AttachmentRecord &attachment) const
{
    const QFileInfo file(attachment.filePath);
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(file);
    QVariantMap row;
    row.insert("attachmentId", attachment.attachmentId);
    row.insert("displayName", attachment.displayName);
    row.insert("sizeBytes", file.exists() ? file.size() : 0);
    row.insert("size", file.exists() ? QLocale().formattedDataSize(file.size()) : QString("Missing"));
    row.insert("mimeType", mime.isValid() ? mime.name() : QString("application/octet-stream"));
    row.insert("available", file.exists() && file.isFile());
    return row;
}

QVariantMap MobileAppController::artifactSummary(const ArtifactVersion &artifact) const
{
    QVariantMap row;
    row.insert("versionId", artifact.versionId);
    row.insert("summary", artifact.summary);
    row.insert("phase", toString(artifact.createdByPhase));
    row.insert("round", artifact.createdByRound);
    row.insert("createdAt", artifact.createdAt.toLocalTime().toString("yyyy-MM-dd hh:mm"));
    row.insert("filePath", artifact.filePath);
    return row;
}

QVariantMap MobileAppController::logSummary(const LogEvent &event) const
{
    QVariantMap row;
    row.insert("logId", event.logId);
    row.insert("summary", event.summary);
    row.insert("type", logEventTypeLabel(event.type));
    row.insert("actorName", event.actorName);
    row.insert("phase", toString(event.phase));
    row.insert("round", event.round);
    row.insert("timestamp", event.timestamp.toLocalTime().toString("HH:mm:ss"));
    return row;
}

void MobileAppController::handleAttachmentImportFinished(const AttachmentImportResult &result)
{
    if (result.operationId != m_attachmentImportOperationId) {
        if (!result.finalPath.isEmpty()) {
            m_context.cleanupAttachmentFileIfUnreferenced(result.finalPath);
        }
        return;
    }

    const QString targetTableId = m_attachmentImportTableId;
    const bool suppressCancellationError = m_suppressAttachmentCancellationError;
    m_attachmentImportOperationId.clear();
    m_attachmentImportTableId.clear();
    m_attachmentImportStatus.clear();
    m_attachmentImportInProgress = false;
    m_suppressAttachmentCancellationError = false;
    emit attachmentImportChanged();

    if (!result.succeeded()) {
        if (!(suppressCancellationError && result.status == AttachmentImportStatus::Cancelled)) {
            setError(attachmentImportErrorMessage(result.status));
            emit attachmentImportFailed();
        }
        return;
    }

    const auto handle = m_context.tableHandle(targetTableId);
    if (!handle || handle->continuationCommand.payload.value("outcomeUnknown").toBool()) {
        m_context.cleanupAttachmentFileIfUnreferenced(result.finalPath);
        return;
    }

    QString verificationError;
    AttachmentRecord attachment = m_context.uploadManager()->createAttachment(
        result.finalPath, result.sha256, result.byteCount, &verificationError);
    if (attachment.attachmentId.isEmpty()) {
        m_context.cleanupAttachmentFileIfUnreferenced(result.finalPath);
        setError("The imported attachment could not be verified.");
        emit attachmentImportFailed();
        return;
    }

    SessionState candidate = *handle;
    candidate.attachments.append(attachment);
    if (isRunningPhase(candidate.phase) || candidate.waitingForNextTurn) {
        candidate.queuedInputIds.append(attachment.attachmentId);
    }
    if (!saveAndNotify(candidate, true)) {
        m_context.cleanupAttachmentFileIfUnreferenced(result.finalPath);
        setError("The imported attachment could not be saved.");
        emit attachmentImportFailed();
        return;
    }
    setError({});
}

QStringList MobileAppController::presentationSecrets() const
{
    QStringList secrets;
    for (const auto provider : {ProviderKind::OpenAI, ProviderKind::Gemini, ProviderKind::Anthropic}) {
        const auto key = m_context.credentialStore()->loadApiKey(provider);
        if (!key.isEmpty()) secrets.append(key);
    }
    return secrets;
}

QVariantList MobileAppController::sanitizedRows(QVariantList rows) const
{
    const auto secrets = presentationSecrets();
    for (auto &value : rows) {
        auto *row = get_if<QVariantMap>(&value);
        if (!row) continue;
        for (auto it = row->begin(); it != row->end(); ++it) {
            // IDs and filesystem paths are operational data, not displayed content.
            if (it.key().endsWith("Id") || it.key() == "filePath") continue;
            if (it.value().metaType().id() == QMetaType::QString)
                it.value() = visibleText(it.value().toString(), secrets);
        }
    }
    return rows;
}

void MobileAppController::setError(const QString &error) const
{
    m_lastError = error;
}

} // namespace amt
