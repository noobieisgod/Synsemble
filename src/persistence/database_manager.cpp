#include "persistence/database_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDebug>
#include <QElapsedTimer>
#include <QSet>
#include <QUuid>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>

#include "core/logging.h"
#include "core/startup_timeline.h"

namespace amt {

namespace {

QSet<QString> loadExistingIds(QSqlDatabase &db, const QString &sql, const QString &tableId)
{
    QSet<QString> ids;
    QSqlQuery query(db);
    query.prepare(sql);
    query.addBindValue(tableId);
    if (!query.exec()) {
        return ids;
    }
    while (query.next()) {
        ids.insert(query.value(0).toString());
    }
    return ids;
}

bool deleteIds(QSqlDatabase &db, const QString &sql, const QString &tableId, const QSet<QString> &ids)
{
    if (ids.isEmpty()) {
        return true;
    }
    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        return false;
    }
    for (const auto &id : ids) {
        query.bindValue(0, tableId);
        query.bindValue(1, id);
        if (!query.exec()) {
            return false;
        }
    }
    return true;
}

QString nonNullString(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString sqlErrorSummary(const QSqlError &error)
{
    return QString("type=%1 nativeCode=%2")
        .arg(static_cast<int>(error.type()))
        .arg(error.nativeErrorCode().isEmpty() ? "none" : error.nativeErrorCode());
}

} // namespace

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
{
}

DatabaseManager::DatabaseManager(const QString &databasePathOverride,
                                 const QString &connectionNameOverride,
                                 QObject *parent)
    : QObject(parent),
      m_databasePathOverride(databasePathOverride),
      m_connectionNameOverride(connectionNameOverride)
{
}

DatabaseManager::~DatabaseManager()
{
    if (m_db.isValid()) {
        const QString name = m_db.connectionName();
        if (m_db.isOpen()) {
            m_db.close();
        }
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
}

bool DatabaseManager::initialize()
{
    QElapsedTimer openTimer;
    openTimer.start();
    QDir().mkpath(QFileInfo(databasePath()).absolutePath());
    const QString name = connectionName();
    if (QSqlDatabase::contains(name)) {
        m_db = QSqlDatabase::database(name);
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", name);
    }
    m_db.setDatabaseName(databasePath());
    if (!m_db.open()) {
        qWarning().noquote() << QString("Database open failed: %1").arg(sqlErrorSummary(m_db.lastError()));
        return false;
    }
    StartupTimeline::instance().mark(StartupStage::DatabaseOpen, openTimer.elapsed());
    qCDebug(diagnosticsLog) << "Database open succeeded";
    logDatabaseFileState("after open");

    QSqlQuery pragmaQuery(m_db);
    if (!pragmaQuery.exec("PRAGMA journal_mode = WAL")) {
        qWarning().noquote() << QString("Database pragma failed: journal_mode %1").arg(sqlErrorSummary(pragmaQuery.lastError()));
        return false;
    }
    if (!pragmaQuery.exec("PRAGMA synchronous = NORMAL")) {
        qWarning().noquote() << QString("Database pragma failed: synchronous %1").arg(sqlErrorSummary(pragmaQuery.lastError()));
        return false;
    }

    QElapsedTimer schemaTimer;
    schemaTimer.start();
    const bool schemaCreated = createSchema();
    if (schemaCreated) {
        StartupTimeline::instance().mark(StartupStage::SchemaInitialization,
                                         schemaTimer.elapsed());
    }
    return schemaCreated;
}

bool DatabaseManager::createSchema()
{
    QSqlQuery query(m_db);
    const bool tableOk = query.exec(
        "CREATE TABLE IF NOT EXISTS meeting_tables ("
        "table_id TEXT PRIMARY KEY,"
        "title TEXT NOT NULL,"
        "pinned INTEGER NOT NULL DEFAULT 0,"
        "updated_at TEXT NOT NULL DEFAULT '',"
        "phase TEXT NOT NULL,"
        "round_no INTEGER NOT NULL,"
        "active_seat_id TEXT,"
        "final_decision_maker_seat_id TEXT,"
        "used_tokens INTEGER NOT NULL,"
        "used_cost REAL NOT NULL,"
        "phase_used_tokens INTEGER NOT NULL DEFAULT 0,"
        "phase_used_cost REAL NOT NULL DEFAULT 0,"
        "elapsed_seconds INTEGER NOT NULL,"
        "pending_research_responses INTEGER NOT NULL,"
        "use_budget_overrides INTEGER NOT NULL DEFAULT 0,"
        "budget_override_json TEXT NOT NULL DEFAULT '{}',"
        "stop_policy_json TEXT NOT NULL DEFAULT '{}',"
        "seat_usage_json TEXT NOT NULL DEFAULT '[]',"
        "pending_seats_json TEXT NOT NULL DEFAULT '[]',"
        "attachments_json TEXT NOT NULL DEFAULT '[]',"
        "queued_input_ids_json TEXT NOT NULL DEFAULT '[]',"
        "current_artifact_version_id TEXT NOT NULL DEFAULT '',"
        "paused_resume_phase TEXT NOT NULL DEFAULT '',"
        "continuation_pending INTEGER NOT NULL DEFAULT 0,"
        "continuation_limit_kind INTEGER NOT NULL DEFAULT 0,"
        "continuation_reason TEXT NOT NULL DEFAULT '',"
        "arbitration_satisfied INTEGER NOT NULL DEFAULT 0,"
        "log_visible INTEGER NOT NULL,"
        "phase_elapsed_seconds INTEGER NOT NULL DEFAULT 0,"
        "seats_json TEXT NOT NULL,"
        "usage_estimate_used INTEGER NOT NULL DEFAULT 0,"
        "cost_estimate_complete INTEGER NOT NULL DEFAULT 1,"
        "continuation_command_json TEXT NOT NULL DEFAULT '{}',"
        "exec_qc_loop_count INTEGER NOT NULL DEFAULT 0)");
    if (!tableOk) {
        return false;
    }

    const struct {
        QString name;
        QString ddl;
    } columns[] = {
        {"pinned", "ALTER TABLE meeting_tables ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0"},
        {"updated_at", "ALTER TABLE meeting_tables ADD COLUMN updated_at TEXT NOT NULL DEFAULT ''"},
        {"use_budget_overrides", "ALTER TABLE meeting_tables ADD COLUMN use_budget_overrides INTEGER NOT NULL DEFAULT 0"},
        {"budget_override_json", "ALTER TABLE meeting_tables ADD COLUMN budget_override_json TEXT NOT NULL DEFAULT '{}'"},
        {"stop_policy_json", "ALTER TABLE meeting_tables ADD COLUMN stop_policy_json TEXT NOT NULL DEFAULT '{}'"},
        {"seat_usage_json", "ALTER TABLE meeting_tables ADD COLUMN seat_usage_json TEXT NOT NULL DEFAULT '[]'"},
        {"pending_seats_json", "ALTER TABLE meeting_tables ADD COLUMN pending_seats_json TEXT NOT NULL DEFAULT '[]'"},
        {"attachments_json", "ALTER TABLE meeting_tables ADD COLUMN attachments_json TEXT NOT NULL DEFAULT '[]'"},
        {"queued_input_ids_json", "ALTER TABLE meeting_tables ADD COLUMN queued_input_ids_json TEXT NOT NULL DEFAULT '[]'"},
        {"current_artifact_version_id", "ALTER TABLE meeting_tables ADD COLUMN current_artifact_version_id TEXT NOT NULL DEFAULT ''"},
        {"paused_resume_phase", "ALTER TABLE meeting_tables ADD COLUMN paused_resume_phase TEXT NOT NULL DEFAULT ''"},
        {"continuation_pending", "ALTER TABLE meeting_tables ADD COLUMN continuation_pending INTEGER NOT NULL DEFAULT 0"},
        {"continuation_limit_kind", "ALTER TABLE meeting_tables ADD COLUMN continuation_limit_kind INTEGER NOT NULL DEFAULT 0"},
        {"continuation_reason", "ALTER TABLE meeting_tables ADD COLUMN continuation_reason TEXT NOT NULL DEFAULT ''"},
        {"arbitration_satisfied", "ALTER TABLE meeting_tables ADD COLUMN arbitration_satisfied INTEGER NOT NULL DEFAULT 0"},
        {"phase_used_tokens", "ALTER TABLE meeting_tables ADD COLUMN phase_used_tokens INTEGER NOT NULL DEFAULT 0"},
        {"phase_used_cost", "ALTER TABLE meeting_tables ADD COLUMN phase_used_cost REAL NOT NULL DEFAULT 0"},
        {"usage_estimate_used", "ALTER TABLE meeting_tables ADD COLUMN usage_estimate_used INTEGER NOT NULL DEFAULT 0"},
        {"cost_estimate_complete", "ALTER TABLE meeting_tables ADD COLUMN cost_estimate_complete INTEGER NOT NULL DEFAULT 1"},
        {"continuation_command_json", "ALTER TABLE meeting_tables ADD COLUMN continuation_command_json TEXT NOT NULL DEFAULT '{}'"},
        {"exec_qc_loop_count", "ALTER TABLE meeting_tables ADD COLUMN exec_qc_loop_count INTEGER NOT NULL DEFAULT 0"}
        // phase_elapsed_seconds is in the base CREATE TABLE schema; no migration needed for new installs.
    };
    for (const auto &column : columns) {
        if (!meetingTablesColumnExists(column.name) && !query.exec(column.ddl)) {
            return false;
        }
    }

    return query.exec(
        "CREATE TABLE IF NOT EXISTS transcript_entries ("
        "entry_id TEXT PRIMARY KEY,"
        "table_id TEXT NOT NULL,"
        "phase TEXT NOT NULL,"
        "round_no INTEGER NOT NULL,"
        "speaker_seat_id TEXT,"
        "speaker_name TEXT NOT NULL,"
        "is_decision INTEGER NOT NULL,"
        "entry_type TEXT NOT NULL DEFAULT 'assistant',"
        "content TEXT NOT NULL,"
        "timestamp TEXT NOT NULL)")
        && (transcriptEntriesColumnExists("entry_type")
            || query.exec("ALTER TABLE transcript_entries ADD COLUMN entry_type TEXT NOT NULL DEFAULT 'assistant'"))
        && query.exec(
        "CREATE TABLE IF NOT EXISTS log_events ("
        "log_id TEXT PRIMARY KEY,"
        "table_id TEXT NOT NULL,"
        "type INTEGER NOT NULL,"
        "actor_seat_id TEXT,"
        "actor_name TEXT,"
        "phase TEXT NOT NULL,"
        "round_no INTEGER NOT NULL,"
        "timestamp TEXT NOT NULL,"
        "summary TEXT NOT NULL)")
        && query.exec(
        "CREATE TABLE IF NOT EXISTS artifact_versions ("
        "version_id TEXT PRIMARY KEY,"
        "table_id TEXT NOT NULL,"
        "parent_version_id TEXT,"
        "phase TEXT NOT NULL,"
        "round_no INTEGER NOT NULL,"
        "created_at TEXT NOT NULL,"
        "summary TEXT NOT NULL,"
        "file_path TEXT NOT NULL)")
        && query.exec("CREATE INDEX IF NOT EXISTS idx_transcript_table_timestamp ON transcript_entries(table_id, timestamp)")
        && query.exec("CREATE INDEX IF NOT EXISTS idx_log_table_timestamp ON log_events(table_id, timestamp)")
        && query.exec("CREATE INDEX IF NOT EXISTS idx_artifact_table_created ON artifact_versions(table_id, created_at)");
}

bool DatabaseManager::meetingTablesColumnExists(const QString &columnName) const
{
    QSqlQuery query(m_db);
    if (!query.exec("PRAGMA table_info(meeting_tables)")) {
        return false;
    }

    while (query.next()) {
        if (query.value(1).toString() == columnName) {
            return true;
        }
    }
    return false;
}

bool DatabaseManager::transcriptEntriesColumnExists(const QString &columnName) const
{
    QSqlQuery query(m_db);
    if (!query.exec("PRAGMA table_info(transcript_entries)")) {
        return false;
    }

    while (query.next()) {
        if (query.value(1).toString() == columnName) {
            return true;
        }
    }
    return false;
}

bool DatabaseManager::tableRowExists(const QString &tableId) const
{
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM meeting_tables WHERE table_id = ? LIMIT 1");
    query.addBindValue(tableId);
    if (!query.exec()) {
        qWarning().noquote() << QString("Database row check failed: table=%1 error=%2").arg(tableId, query.lastError().text());
        return false;
    }
    return query.next();
}

int DatabaseManager::rowCount(const QString &tableName, const QString &tableId) const
{
    static const QSet<QString> allowedTables = {
        "meeting_tables",
        "transcript_entries",
        "artifact_versions",
        "log_events"
    };
    if (!allowedTables.contains(tableName)) {
        return -1;
    }

    QSqlQuery query(m_db);
    if (tableId.isEmpty()) {
        query.prepare(QString("SELECT COUNT(*) FROM %1").arg(tableName));
    } else {
        query.prepare(QString("SELECT COUNT(*) FROM %1 WHERE table_id = ?").arg(tableName));
        query.addBindValue(tableId);
    }
    if (!query.exec() || !query.next()) {
        qWarning().noquote() << QString("Database row count failed: table=%1 tableId=%2 error=%3")
                                    .arg(tableName, tableId, query.lastError().text());
        return -1;
    }
    return query.value(0).toInt();
}

void DatabaseManager::logDatabaseFileState(const QString &context) const
{
    if (!diagnosticsLog().isDebugEnabled()) {
        return;
    }
    const QFileInfo info(databasePath());
    qCDebug(diagnosticsLog).noquote() << QString("Database file state: context=%1 exists=%2 size=%3")
                             .arg(context,
                                  info.exists() ? "true" : "false",
                                  QString::number(info.exists() ? info.size() : 0));
}

void DatabaseManager::logTableRowCounts(const QString &context, const QString &tableId) const
{
    if (!diagnosticsLog().isDebugEnabled()) {
        return;
    }
    qCDebug(diagnosticsLog).noquote() << QString("Database row counts: context=%1 tables=%2 transcript=%3 artifacts=%4 logs=%5")
                             .arg(context,
                                  QString::number(rowCount("meeting_tables")),
                                  QString::number(rowCount("transcript_entries", tableId)),
                                  QString::number(rowCount("artifact_versions", tableId)),
                                  QString::number(rowCount("log_events", tableId)));
}

QVector<SessionState> DatabaseManager::loadTables(bool *success)
{
    if (success) *success = false;
    QVector<SessionState> tables;
    qint64 tableRestoreMs = 0;
    qint64 transcriptRestoreMs = 0;
    qint64 logRestoreMs = 0;
    qint64 artifactRestoreMs = 0;
    if (!m_db.isOpen()) {
        return tables;
    }

    logDatabaseFileState("before load");
    QElapsedTimer restoreTimer;
    restoreTimer.start();
    QSqlQuery query(m_db);
    if (!query.exec("SELECT table_id, title, pinned, updated_at, phase, round_no, active_seat_id, final_decision_maker_seat_id, used_tokens, used_cost, phase_used_tokens, phase_used_cost, elapsed_seconds, phase_elapsed_seconds, pending_research_responses, use_budget_overrides, budget_override_json, stop_policy_json, seat_usage_json, pending_seats_json, attachments_json, queued_input_ids_json, current_artifact_version_id, paused_resume_phase, continuation_pending, continuation_limit_kind, continuation_reason, arbitration_satisfied, log_visible, seats_json, usage_estimate_used, cost_estimate_complete, continuation_command_json, exec_qc_loop_count FROM meeting_tables")) {
        qWarning().noquote() << QString("Database load failed: meeting_tables error=%1").arg(query.lastError().text());
        return tables;
    }
    tableRestoreMs += restoreTimer.elapsed();
    while (query.next()) {
        restoreTimer.restart();
        SessionState state;
        state.tableId = query.value(0).toString();
        state.title = query.value(1).toString();
        state.pinned = query.value(2).toInt() != 0;
        state.updatedAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODate);
        if (!state.updatedAt.isValid()) {
            state.updatedAt = QDateTime::currentDateTimeUtc();
        }
        const QString phaseString = query.value(4).toString();
        const QList<Phase> phases = {Phase::Idle, Phase::Research, Phase::Planning, Phase::Execution, Phase::QualityControl, Phase::Present, Phase::Paused, Phase::Completed, Phase::Stopped, Phase::Failed};
        for (const auto phase : phases) {
            if (toString(phase) == phaseString) {
                state.phase = phase;
                break;
            }
        }
        state.round = query.value(5).toInt();
        state.activeSeatId = query.value(6).toString();
        state.finalDecisionMakerSeatId = query.value(7).toString();
        state.usedTokens = query.value(8).toInt();
        state.usedCost = query.value(9).toDouble();
        state.phaseUsedTokens = query.value(10).toInt();
        state.phaseUsedCost = query.value(11).toDouble();
        state.elapsedSeconds = query.value(12).toInt();
        state.phaseElapsedSeconds = query.value(13).toInt();
        state.pendingResearchResponses = query.value(14).toInt();
        state.useBudgetOverrides = query.value(15).toInt() != 0;
        state.budgetOverrides = budgetPolicyFromJson(QJsonDocument::fromJson(query.value(16).toByteArray()).object());
        state.stopPolicy = stopPolicyFromJson(QJsonDocument::fromJson(query.value(17).toByteArray()).object());
        const auto seatUsageArray = QJsonDocument::fromJson(query.value(18).toByteArray()).array();
        for (const auto &usageValue : seatUsageArray) {
            state.seatUsage.append(seatUsageFromJson(usageValue.toObject()));
        }
        const auto pendingSeatArray = QJsonDocument::fromJson(query.value(19).toByteArray()).array();
        for (const auto &seatValue : pendingSeatArray) {
            state.pendingSeats.append(seatFromJson(seatValue.toObject()));
        }
        const auto attachmentDocument = QJsonDocument::fromJson(query.value(20).toByteArray());
        if (!attachmentDocument.isArray()) {
            qWarning() << "Database restore failed: invalid attachment references";
            return {};
        }
        const auto attachmentArray = attachmentDocument.array();
        for (const auto &attachmentValue : attachmentArray) {
            if (!attachmentValue.isObject()) return {};
            state.attachments.append(attachmentFromJson(attachmentValue.toObject()));
        }
        for (const auto &queuedValue : QJsonDocument::fromJson(query.value(21).toByteArray()).array()) {
            state.queuedInputIds.append(queuedValue.toString());
        }
        state.currentArtifactVersionId = query.value(22).toString();
        const QString pausedResumePhaseString = query.value(23).toString();
        for (const auto phase : phases) {
            if (toString(phase) == pausedResumePhaseString) {
                state.pausedResumePhase = phase;
                break;
            }
        }
        state.continuationPending = query.value(24).toInt() != 0;
        state.continuationLimitKind = query.value(25).toInt();
        state.continuationReason = query.value(26).toString();
        state.arbitrationSatisfied = query.value(27).toInt() != 0;
        state.logVisible = query.value(28).toInt() != 0;

        const auto seatArray = QJsonDocument::fromJson(query.value(29).toByteArray()).array();
        for (const auto &seatValue : seatArray) {
            state.seats.append(seatFromJson(seatValue.toObject()));
        }
        state.usageEstimateUsed = query.value(30).toInt() != 0;
        state.costEstimateComplete = query.value(31).toInt() != 0;
        state.continuationCommand = workflowCommandFromJson(
            QJsonDocument::fromJson(query.value(32).toByteArray()).object());
        state.execQcLoopCount = query.value(33).toInt();
        tableRestoreMs += restoreTimer.elapsed();
        restoreTimer.restart();
        if (!loadTranscript(state)) return {};
        transcriptRestoreMs += restoreTimer.elapsed();
        restoreTimer.restart();
        if (!loadLog(state)) return {};
        logRestoreMs += restoreTimer.elapsed();
        restoreTimer.restart();
        if (!loadArtifacts(state)) return {};
        artifactRestoreMs += restoreTimer.elapsed();
        PersistedChildIds childIds;
        for (const auto &entry : state.transcript) {
            childIds.transcript.insert(entry.entryId);
        }
        for (const auto &entry : state.log) {
            childIds.log.insert(entry.logId);
        }
        for (const auto &artifact : state.artifacts) {
            childIds.artifacts.insert(artifact.versionId);
        }
        m_persistedChildIds.insert(state.tableId, childIds);
        qCDebug(diagnosticsLog).noquote() << QString("Database load table: transcript=%1 artifacts=%2 logs=%3")
                                  .arg(QString::number(state.transcript.size()),
                                       QString::number(state.artifacts.size()),
                                       QString::number(state.log.size()));
        tables.append(state);
    }
    if (query.lastError().isValid()) return {};
    if (success) *success = true;
    if (diagnosticsLog().isDebugEnabled()) {
        qCDebug(diagnosticsLog).noquote() << QString("Database load summary: tables=%1 totalTranscript=%2 totalArtifacts=%3 totalLogs=%4")
                                 .arg(QString::number(tables.size()),
                                      QString::number(rowCount("transcript_entries")),
                                      QString::number(rowCount("artifact_versions")),
                                      QString::number(rowCount("log_events")));
    }
    auto &startup = StartupTimeline::instance();
    startup.mark(StartupStage::TableRestoration, tableRestoreMs);
    startup.mark(StartupStage::TranscriptRestoration, transcriptRestoreMs);
    startup.mark(StartupStage::LogRestoration, logRestoreMs);
    startup.mark(StartupStage::ArtifactRestoration, artifactRestoreMs);
    return tables;
}

bool DatabaseManager::saveTable(const SessionState &state)
{
    if (!m_db.isOpen()) {
        qWarning().noquote() << QString("Database save failed: database is not open table=%1").arg(state.tableId);
        return false;
    }

    QJsonArray seats;
    for (const auto &seat : state.seats) {
        seats.append(seatToJson(seat));
    }

    PersistedChildIds existingIds = m_persistedChildIds.value(state.tableId);
    const bool cacheKnown = m_persistedChildIds.contains(state.tableId);
    const bool rowExistedBeforeSave = cacheKnown || tableRowExists(state.tableId);
    if (!cacheKnown && rowExistedBeforeSave) {
        existingIds.transcript = loadExistingIds(m_db, "SELECT entry_id FROM transcript_entries WHERE table_id = ?", state.tableId);
        existingIds.log = loadExistingIds(m_db, "SELECT log_id FROM log_events WHERE table_id = ?", state.tableId);
        existingIds.artifacts = loadExistingIds(m_db, "SELECT version_id FROM artifact_versions WHERE table_id = ?", state.tableId);
    }

    if (!m_db.transaction()) {
        qWarning().noquote() << QString("Database save failed: transaction table=%1 error=%2").arg(state.tableId, m_db.lastError().text());
        return false;
    }
    QSqlQuery query(m_db);

    // Prepare the common bind values shared by both UPDATE and INSERT paths.
    const QString titleVal = nonNullString(state.title);
    const int pinnedVal = state.pinned ? 1 : 0;
    const QString updatedAtVal = state.updatedAt.toUTC().toString(Qt::ISODate);
    const QString phaseVal = toString(state.phase);
    const int roundVal = state.round;
    const QString activeSeatVal = nonNullString(state.activeSeatId);
    const QString fdmSeatVal = nonNullString(state.finalDecisionMakerSeatId);
    const int usedTokensVal = state.usedTokens;
    const double usedCostVal = state.usedCost;
    const int phaseTokensVal = state.phaseUsedTokens;
    const double phaseCostVal = state.phaseUsedCost;
    const int elapsedVal = state.elapsedSeconds;
    const int phaseElapsedVal = state.phaseElapsedSeconds;
    const int pendingResearchVal = state.pendingResearchResponses;
    const int useOverridesVal = state.useBudgetOverrides ? 1 : 0;
    const QString budgetJson = QJsonDocument(budgetPolicyToJson(state.budgetOverrides)).toJson(QJsonDocument::Compact);
    const QString stopJson = QJsonDocument(stopPolicyToJson(state.stopPolicy)).toJson(QJsonDocument::Compact);
    const QString seatUsageJson = QJsonDocument(seatUsageToJson(state.seatUsage)).toJson(QJsonDocument::Compact);
    const QString pendingSeatsJson = QJsonDocument(seatsToJson(state.pendingSeats)).toJson(QJsonDocument::Compact);
    const QString attachmentsJson = QJsonDocument(attachmentsToJson(state.attachments)).toJson(QJsonDocument::Compact);
    QJsonArray queuedInputs;
    for (const auto &queuedId : state.queuedInputIds) {
        queuedInputs.append(queuedId);
    }
    const QString queuedJson = QJsonDocument(queuedInputs).toJson(QJsonDocument::Compact);
    const QString artifactVersionVal = nonNullString(state.currentArtifactVersionId);
    const QString pausedResumePhaseVal = nonNullString(toString(state.pausedResumePhase));
    const int continuationPendingVal = state.continuationPending ? 1 : 0;
    const int continuationLimitKindVal = state.continuationLimitKind;
    const QString continuationReasonVal = nonNullString(state.continuationReason);
    const int arbitrationSatisfiedVal = state.arbitrationSatisfied ? 1 : 0;
    const int logVisibleVal = state.logVisible ? 1 : 0;
    const QString seatsJson = QJsonDocument(seatsToJson(state.seats)).toJson(QJsonDocument::Compact);
    const int usageEstimateUsedVal = state.usageEstimateUsed ? 1 : 0;
    const int costEstimateCompleteVal = state.costEstimateComplete ? 1 : 0;
    const QString continuationCommandJson = QJsonDocument(
        workflowCommandToJson(state.continuationCommand))
                                                .toJson(QJsonDocument::Compact);
    const int execQcLoopCountVal = state.execQcLoopCount;

    query.prepare(
        "UPDATE meeting_tables SET "
        "title=?, pinned=?, updated_at=?, phase=?, round_no=?, active_seat_id=?, final_decision_maker_seat_id=?, "
        "used_tokens=?, used_cost=?, phase_used_tokens=?, phase_used_cost=?, elapsed_seconds=?, phase_elapsed_seconds=?, "
        "pending_research_responses=?, use_budget_overrides=?, budget_override_json=?, stop_policy_json=?, "
        "seat_usage_json=?, pending_seats_json=?, attachments_json=?, queued_input_ids_json=?, "
        "current_artifact_version_id=?, paused_resume_phase=?, continuation_pending=?, continuation_limit_kind=?, continuation_reason=?, arbitration_satisfied=?, log_visible=?, seats_json=?, usage_estimate_used=?, cost_estimate_complete=?, continuation_command_json=?, exec_qc_loop_count=? "
        "WHERE table_id=?");
    query.addBindValue(titleVal);
    query.addBindValue(pinnedVal);
    query.addBindValue(updatedAtVal);
    query.addBindValue(phaseVal);
    query.addBindValue(roundVal);
    query.addBindValue(activeSeatVal);
    query.addBindValue(fdmSeatVal);
    query.addBindValue(usedTokensVal);
    query.addBindValue(usedCostVal);
    query.addBindValue(phaseTokensVal);
    query.addBindValue(phaseCostVal);
    query.addBindValue(elapsedVal);
    query.addBindValue(phaseElapsedVal);
    query.addBindValue(pendingResearchVal);
    query.addBindValue(useOverridesVal);
    query.addBindValue(budgetJson);
    query.addBindValue(stopJson);
    query.addBindValue(seatUsageJson);
    query.addBindValue(pendingSeatsJson);
    query.addBindValue(attachmentsJson);
    query.addBindValue(queuedJson);
    query.addBindValue(artifactVersionVal);
    query.addBindValue(pausedResumePhaseVal);
    query.addBindValue(continuationPendingVal);
    query.addBindValue(continuationLimitKindVal);
    query.addBindValue(continuationReasonVal);
    query.addBindValue(arbitrationSatisfiedVal);
    query.addBindValue(logVisibleVal);
    query.addBindValue(seatsJson);
    query.addBindValue(usageEstimateUsedVal);
    query.addBindValue(costEstimateCompleteVal);
    query.addBindValue(continuationCommandJson);
    query.addBindValue(execQcLoopCountVal);
    query.addBindValue(state.tableId);
    if (!query.exec()) {
        qWarning().noquote() << QString("Database save failed: update table=%1 error=%2").arg(state.tableId, query.lastError().text());
        m_db.rollback();
        return false;
    }

    if (query.numRowsAffected() == 0 && !rowExistedBeforeSave) {
        // Row does not exist yet, so insert it.
        query.prepare(
            "INSERT INTO meeting_tables "
            "(table_id, title, pinned, updated_at, phase, round_no, active_seat_id, final_decision_maker_seat_id, used_tokens, used_cost, phase_used_tokens, phase_used_cost, elapsed_seconds, phase_elapsed_seconds, pending_research_responses, use_budget_overrides, budget_override_json, stop_policy_json, seat_usage_json, pending_seats_json, attachments_json, queued_input_ids_json, current_artifact_version_id, paused_resume_phase, continuation_pending, continuation_limit_kind, continuation_reason, arbitration_satisfied, log_visible, seats_json, usage_estimate_used, cost_estimate_complete, continuation_command_json, exec_qc_loop_count) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        query.addBindValue(state.tableId);
        query.addBindValue(titleVal);
        query.addBindValue(pinnedVal);
        query.addBindValue(updatedAtVal);
        query.addBindValue(phaseVal);
        query.addBindValue(roundVal);
        query.addBindValue(activeSeatVal);
        query.addBindValue(fdmSeatVal);
        query.addBindValue(usedTokensVal);
        query.addBindValue(usedCostVal);
        query.addBindValue(phaseTokensVal);
        query.addBindValue(phaseCostVal);
        query.addBindValue(elapsedVal);
        query.addBindValue(phaseElapsedVal);
        query.addBindValue(pendingResearchVal);
        query.addBindValue(useOverridesVal);
        query.addBindValue(budgetJson);
        query.addBindValue(stopJson);
        query.addBindValue(seatUsageJson);
        query.addBindValue(pendingSeatsJson);
        query.addBindValue(attachmentsJson);
        query.addBindValue(queuedJson);
        query.addBindValue(artifactVersionVal);
        query.addBindValue(pausedResumePhaseVal);
        query.addBindValue(continuationPendingVal);
        query.addBindValue(continuationLimitKindVal);
        query.addBindValue(continuationReasonVal);
        query.addBindValue(arbitrationSatisfiedVal);
        query.addBindValue(logVisibleVal);
        query.addBindValue(seatsJson);
        query.addBindValue(usageEstimateUsedVal);
        query.addBindValue(costEstimateCompleteVal);
        query.addBindValue(continuationCommandJson);
        query.addBindValue(execQcLoopCountVal);
        if (!query.exec()) {
            qWarning().noquote() << QString("Database save failed: insert table=%1 error=%2").arg(state.tableId, query.lastError().text());
            m_db.rollback();
            return false;
        }
    }

    QSet<QString> retainedTranscriptIds;
    QSet<QString> retainedLogIds;
    QSet<QString> retainedArtifactIds;

    QSqlQuery transcriptInsert(m_db);
    if (!transcriptInsert.prepare("INSERT OR REPLACE INTO transcript_entries (entry_id, table_id, phase, round_no, speaker_seat_id, speaker_name, is_decision, entry_type, content, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")) {
        m_db.rollback();
        return false;
    }
    for (const auto &entry : state.transcript) {
        retainedTranscriptIds.insert(entry.entryId);
        if (existingIds.transcript.contains(entry.entryId)) {
            continue;
        }
        transcriptInsert.bindValue(0, entry.entryId);
        transcriptInsert.bindValue(1, entry.tableId);
        transcriptInsert.bindValue(2, toString(entry.phase));
        transcriptInsert.bindValue(3, entry.round);
        transcriptInsert.bindValue(4, entry.speakerSeatId);
        transcriptInsert.bindValue(5, entry.speakerName);
        transcriptInsert.bindValue(6, entry.isUser ? -1 : (entry.isDecision ? 1 : 0));
        transcriptInsert.bindValue(7, entry.isUser ? "user" : (entry.isDecision ? "decision" : "assistant"));
        transcriptInsert.bindValue(8, entry.content);
        transcriptInsert.bindValue(9, entry.timestamp.toUTC().toString(Qt::ISODate));
        if (!transcriptInsert.exec()) {
            qWarning().noquote() << QString("Database save failed: transcript table=%1 entry=%2 error=%3")
                                        .arg(state.tableId, entry.entryId, transcriptInsert.lastError().text());
            m_db.rollback();
            return false;
        }
    }
    if (!deleteIds(m_db,
                   "DELETE FROM transcript_entries WHERE table_id = ? AND entry_id = ?",
                   state.tableId,
                   existingIds.transcript - retainedTranscriptIds)) {
        m_db.rollback();
        return false;
    }

    QSqlQuery logInsert(m_db);
    if (!logInsert.prepare("INSERT OR REPLACE INTO log_events (log_id, table_id, type, actor_seat_id, actor_name, phase, round_no, timestamp, summary) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)")) {
        m_db.rollback();
        return false;
    }
    for (const auto &entry : state.log) {
        retainedLogIds.insert(entry.logId);
        if (existingIds.log.contains(entry.logId)) {
            continue;
        }
        logInsert.bindValue(0, entry.logId);
        logInsert.bindValue(1, entry.tableId);
        logInsert.bindValue(2, static_cast<int>(entry.type));
        logInsert.bindValue(3, entry.actorSeatId);
        logInsert.bindValue(4, entry.actorName);
        logInsert.bindValue(5, toString(entry.phase));
        logInsert.bindValue(6, entry.round);
        logInsert.bindValue(7, entry.timestamp.toUTC().toString(Qt::ISODate));
        logInsert.bindValue(8, entry.summary);
        if (!logInsert.exec()) {
            qWarning().noquote() << QString("Database save failed: log table=%1 log=%2 error=%3")
                                        .arg(state.tableId, entry.logId, logInsert.lastError().text());
            m_db.rollback();
            return false;
        }
    }
    if (!deleteIds(m_db,
                   "DELETE FROM log_events WHERE table_id = ? AND log_id = ?",
                   state.tableId,
                   existingIds.log - retainedLogIds)) {
        m_db.rollback();
        return false;
    }

    QSqlQuery artifactInsert(m_db);
    if (!artifactInsert.prepare("INSERT OR REPLACE INTO artifact_versions (version_id, table_id, parent_version_id, phase, round_no, created_at, summary, file_path) VALUES (?, ?, ?, ?, ?, ?, ?, ?)")) {
        m_db.rollback();
        return false;
    }
    for (const auto &artifact : state.artifacts) {
        retainedArtifactIds.insert(artifact.versionId);
        if (existingIds.artifacts.contains(artifact.versionId)) {
            continue;
        }
        artifactInsert.bindValue(0, artifact.versionId);
        artifactInsert.bindValue(1, state.tableId);
        artifactInsert.bindValue(2, artifact.parentVersionId);
        artifactInsert.bindValue(3, toString(artifact.createdByPhase));
        artifactInsert.bindValue(4, artifact.createdByRound);
        artifactInsert.bindValue(5, artifact.createdAt.toUTC().toString(Qt::ISODate));
        artifactInsert.bindValue(6, artifact.summary);
        artifactInsert.bindValue(7, artifact.filePath);
        if (!artifactInsert.exec()) {
            qWarning().noquote() << QString("Database save failed: artifact table=%1 version=%2 error=%3")
                                        .arg(state.tableId, artifact.versionId, artifactInsert.lastError().text());
            m_db.rollback();
            return false;
        }
    }
    if (!deleteIds(m_db,
                   "DELETE FROM artifact_versions WHERE table_id = ? AND version_id = ?",
                   state.tableId,
                   existingIds.artifacts - retainedArtifactIds)) {
        m_db.rollback();
        return false;
    }
    if (!m_db.commit()) {
        qWarning().noquote() << QString("Database save failed: commit table=%1 error=%2").arg(state.tableId, m_db.lastError().text());
        m_db.rollback();
        return false;
    }
    m_persistedChildIds.insert(state.tableId, PersistedChildIds{retainedTranscriptIds, retainedLogIds, retainedArtifactIds});
    logDatabaseFileState("after save");
    logTableRowCounts("after save", state.tableId);
    qCDebug(diagnosticsLog).noquote() << QString("Database save table: transcript=%1 artifacts=%2 logs=%3")
                             .arg(QString::number(state.transcript.size()),
                                  QString::number(state.artifacts.size()),
                                  QString::number(state.log.size()));
    return true;
}

bool DatabaseManager::loadTranscript(SessionState &state) const
{
    QSqlQuery query(m_db);
    const bool hasEntryType = transcriptEntriesColumnExists("entry_type");
    query.prepare(hasEntryType
                      ? "SELECT entry_id, phase, round_no, speaker_seat_id, speaker_name, is_decision, entry_type, content, timestamp FROM transcript_entries WHERE table_id = ? ORDER BY timestamp, rowid"
                      : "SELECT entry_id, phase, round_no, speaker_seat_id, speaker_name, is_decision, content, timestamp FROM transcript_entries WHERE table_id = ? ORDER BY timestamp, rowid");
    query.addBindValue(state.tableId);
    if (!query.exec()) {
        qWarning().noquote() << QString("Database load failed: transcript table=%1 error=%2").arg(state.tableId, query.lastError().text());
        return false;
    }
    const QList<Phase> phases = {Phase::Idle, Phase::Research, Phase::Planning, Phase::Execution, Phase::QualityControl, Phase::Present, Phase::Paused, Phase::Completed, Phase::Stopped, Phase::Failed};
    while (query.next()) {
        TranscriptEntry entry;
        entry.entryId = query.value(0).toString();
        entry.tableId = state.tableId;
        const QString phaseString = query.value(1).toString();
        for (const auto phase : phases) {
            if (toString(phase) == phaseString) {
                entry.phase = phase;
                break;
            }
        }
        entry.round = query.value(2).toInt();
        entry.speakerSeatId = query.value(3).toString();
        entry.speakerName = query.value(4).toString();
        const int legacyState = query.value(5).toInt();
        if (hasEntryType) {
            const QString entryType = query.value(6).toString();
            entry.isUser = entryType == "user";
            entry.isDecision = entryType == "decision";
            entry.content = query.value(7).toString();
            entry.timestamp = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
        } else {
            entry.isUser = legacyState < 0;
            entry.isDecision = legacyState > 0;
            entry.content = query.value(6).toString();
            entry.timestamp = QDateTime::fromString(query.value(7).toString(), Qt::ISODate);
        }
        state.transcript.append(entry);
    }
    return !query.lastError().isValid();
}

bool DatabaseManager::loadLog(SessionState &state) const
{
    QSqlQuery query(m_db);
    query.prepare("SELECT log_id, type, actor_seat_id, actor_name, phase, round_no, timestamp, summary FROM log_events WHERE table_id = ? ORDER BY timestamp, rowid");
    query.addBindValue(state.tableId);
    if (!query.exec()) {
        qWarning().noquote() << QString("Database load failed: log table=%1 error=%2").arg(state.tableId, query.lastError().text());
        return false;
    }
    const QList<Phase> phases = {Phase::Idle, Phase::Research, Phase::Planning, Phase::Execution, Phase::QualityControl, Phase::Present, Phase::Paused, Phase::Completed, Phase::Stopped, Phase::Failed};
    while (query.next()) {
        LogEvent entry;
        entry.logId = query.value(0).toString();
        entry.tableId = state.tableId;
        entry.type = static_cast<LogEventType>(query.value(1).toInt());
        entry.actorSeatId = query.value(2).toString();
        entry.actorName = query.value(3).toString();
        const QString phaseString = query.value(4).toString();
        for (const auto phase : phases) {
            if (toString(phase) == phaseString) {
                entry.phase = phase;
                break;
            }
        }
        entry.round = query.value(5).toInt();
        entry.timestamp = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
        entry.summary = query.value(7).toString();
        state.log.append(entry);
    }
    return !query.lastError().isValid();
}

bool DatabaseManager::loadArtifacts(SessionState &state) const
{
    QSqlQuery query(m_db);
    query.prepare("SELECT version_id, parent_version_id, phase, round_no, created_at, summary, file_path FROM artifact_versions WHERE table_id = ? ORDER BY created_at, rowid");
    query.addBindValue(state.tableId);
    if (!query.exec()) {
        qWarning().noquote() << QString("Database load failed: artifacts table=%1 error=%2").arg(state.tableId, query.lastError().text());
        return false;
    }
    const QList<Phase> phases = {Phase::Idle, Phase::Research, Phase::Planning, Phase::Execution, Phase::QualityControl, Phase::Present, Phase::Paused, Phase::Completed, Phase::Stopped, Phase::Failed};
    while (query.next()) {
        ArtifactVersion artifact;
        artifact.versionId = query.value(0).toString();
        artifact.parentVersionId = query.value(1).toString();
        const QString phaseString = query.value(2).toString();
        for (const auto phase : phases) {
            if (toString(phase) == phaseString) {
                artifact.createdByPhase = phase;
                break;
            }
        }
        artifact.createdByRound = query.value(3).toInt();
        artifact.createdAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODate);
        artifact.summary = query.value(5).toString();
        artifact.filePath = query.value(6).toString();
        state.artifacts.append(artifact);
    }
    if (state.currentArtifactVersionId.isEmpty() && !state.artifacts.isEmpty()) {
        state.currentArtifactVersionId = state.artifacts.constLast().versionId;
    }
    return !query.lastError().isValid();
}

QString DatabaseManager::databasePath() const
{
    if (!m_databasePathOverride.isEmpty()) {
        return m_databasePathOverride;
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ai_meeting_table.db";
}

QString DatabaseManager::connectionName() const
{
    if (!m_connectionNameOverride.isEmpty()) {
        return m_connectionNameOverride;
    }
    return QStringLiteral("ai_meeting_table_main");
}

bool DatabaseManager::deleteTable(const QString &tableId)
{
    if (!m_db.isOpen()) {
        return false;
    }

    SessionState state;
    state.tableId = tableId;
    if (!loadArtifacts(state)) return false;

    if (!m_db.transaction()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM transcript_entries WHERE table_id = ?");
    query.addBindValue(tableId);
    if (!query.exec()) {
        m_db.rollback();
        return false;
    }
    query.prepare("DELETE FROM log_events WHERE table_id = ?");
    query.addBindValue(tableId);
    if (!query.exec()) {
        m_db.rollback();
        return false;
    }
    query.prepare("DELETE FROM artifact_versions WHERE table_id = ?");
    query.addBindValue(tableId);
    if (!query.exec()) {
        m_db.rollback();
        return false;
    }
    query.prepare("DELETE FROM meeting_tables WHERE table_id = ?");
    query.addBindValue(tableId);
    if (!query.exec()) {
        m_db.rollback();
        return false;
    }
    if (!m_db.commit()) {
        m_db.rollback();
        return false;
    }

    for (const auto &artifact : state.artifacts) {
        QFile::remove(artifact.filePath);
    }
    m_persistedChildIds.remove(tableId);
    return true;
}

} // namespace amt
