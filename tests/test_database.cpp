#include <QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "persistence/database_manager.h"
#include "services/budget_manager.h"

using namespace amt;

namespace {

SessionState makeState(int transcriptCount) {
  SessionState state;
  state.tableId = "table-1";
  state.title = "Persistence test";
  state.updatedAt = QDateTime::currentDateTimeUtc();
  state.phase = Phase::Research;
  state.round = 2;
  const QDateTime timestamp =
      QDateTime::fromString("2026-01-01T00:00:00Z", Qt::ISODate);
  for (int i = 0; i < transcriptCount; ++i) {
    TranscriptEntry entry;
    entry.entryId = QString("entry-%1").arg(i, 3, 10, QChar('0'));
    entry.tableId = state.tableId;
    entry.phase = state.phase;
    entry.round = state.round;
    entry.speakerName = "Speaker";
    entry.content = QString("Content %1").arg(i);
    entry.timestamp = timestamp;
    state.transcript.append(entry);
  }
  return state;
}

int scalarInt(QSqlDatabase &database, const QString &sql) {
  QSqlQuery query(database);
  if (!query.exec(sql) || !query.next()) {
    return -1;
  }
  return query.value(0).toInt();
}

} // namespace

class DatabaseTests final : public QObject {
  Q_OBJECT

private slots:
  void incrementalDeltaRoundTripAndIndexes();
  void legacyTranscriptSchemaMigrates();
  void failedCommitCanBeRetried();
  void failedRestoreIsNotAnEmptyDatabase();
};

void DatabaseTests::failedRestoreIsNotAnEmptyDatabase() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString connection = QUuid::createUuid().toString();
  DatabaseManager manager(directory.filePath("restore-failure.db"), connection);
  bool restored = true;
  QVERIFY(manager.loadTables(&restored).isEmpty());
  QVERIFY(!restored);
  QVERIFY(manager.initialize());
  QVERIFY(manager.loadTables(&restored).isEmpty());
  QVERIFY(restored);
  QVERIFY(manager.saveTable(makeState(1)));
  auto database = QSqlDatabase::database(connection);
  QSqlQuery query(database);
  QVERIFY(query.exec("UPDATE meeting_tables SET attachments_json='{'"));
  QVERIFY(manager.loadTables(&restored).isEmpty());
  QVERIFY(!restored);
  QVERIFY(query.exec("UPDATE meeting_tables SET attachments_json='[1]'"));
  QVERIFY(manager.loadTables(&restored).isEmpty());
  QVERIFY(!restored);
  QVERIFY(query.exec("UPDATE meeting_tables SET attachments_json='[]'"));
  for (const QString table : {"transcript_entries", "log_events", "artifact_versions", "meeting_tables"}) {
    QVERIFY(query.exec("ALTER TABLE " + table + " RENAME TO unavailable"));
    QVERIFY(manager.loadTables(&restored).isEmpty());
    QVERIFY(!restored);
    QVERIFY(query.exec("ALTER TABLE unavailable RENAME TO " + table));
    QCOMPARE(manager.loadTables(&restored).size(), 1);
    QVERIFY(restored);
  }
}

void DatabaseTests::failedCommitCanBeRetried() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString connection = QUuid::createUuid().toString();
  DatabaseManager manager(directory.filePath("commit-failure.db"), connection);
  QVERIFY(manager.initialize());
  auto state = makeState(1);
  QVERIFY(manager.saveTable(state));
  auto database = QSqlDatabase::database(connection);
  QSqlQuery query(database);
  QVERIFY(query.exec("PRAGMA foreign_keys=ON"));
  QVERIFY(query.exec("CREATE TABLE fixture_parent (id INTEGER PRIMARY KEY)"));
  QVERIFY(query.exec("CREATE TABLE fixture_child (id INTEGER REFERENCES fixture_parent(id) DEFERRABLE INITIALLY DEFERRED)"));
  QVERIFY(query.exec("CREATE TRIGGER fixture_failure AFTER UPDATE ON meeting_tables BEGIN INSERT INTO fixture_child VALUES (42); END"));
  state.title = "Must not persist yet";
  QVERIFY(!manager.saveTable(state));
  QCOMPARE(manager.loadTables().first().title, QString("Persistence test"));
  QVERIFY(query.exec("DROP TRIGGER fixture_failure"));
  QVERIFY(manager.saveTable(state));
  QCOMPARE(manager.loadTables().first().title, state.title);
  QVERIFY(query.exec("CREATE TRIGGER fixture_failure AFTER DELETE ON meeting_tables BEGIN INSERT INTO fixture_child VALUES (42); END"));
  QVERIFY(!manager.deleteTable(state.tableId));
  QCOMPARE(manager.loadTables().size(), 1);
  QVERIFY(query.exec("DROP TRIGGER fixture_failure"));
  QVERIFY(manager.deleteTable(state.tableId));
  QVERIFY(manager.loadTables().isEmpty());
}

void DatabaseTests::incrementalDeltaRoundTripAndIndexes() {
  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());
  const QString databasePath = temporaryDirectory.filePath("meeting.db");
  const QString managerConnection = QUuid::createUuid().toString();

  DatabaseManager manager(databasePath, managerConnection);
  QVERIFY(manager.initialize());

  SessionState state = makeState(100);
  LogEvent log;
  log.logId = "log-1";
  log.tableId = state.tableId;
  log.phase = state.phase;
  log.timestamp = state.transcript.first().timestamp;
  log.summary = "Test log";
  state.log.append(log);

  ArtifactVersion artifact;
  artifact.versionId = "artifact-1";
  artifact.createdByPhase = state.phase;
  artifact.createdAt = state.transcript.first().timestamp;
  artifact.summary = "Test artifact";
  artifact.filePath = temporaryDirectory.filePath("artifact.md");
  state.artifacts.append(artifact);
  state.currentArtifactVersionId = artifact.versionId;
  state.usageEstimateUsed = true;
  state.costEstimateComplete = false;
  state.phase = Phase::Paused;
  state.pausedResumePhase = Phase::Planning;
  state.continuationPending = true;
  state.continuationLimitKind =
      static_cast<int>(BudgetLimitKind::MaxTotalTokens);
  state.continuationReason =
      "The maximum total token limit has been reached.";
  state.activeSeatId = "seat-1";
  state.usedTokens = 100;
  state.execQcLoopCount = 2;
  state.continuationCommand.commandType =
      RunnerCommandType::RequestDecision;
  state.continuationCommand.sessionId = state.tableId;
  state.continuationCommand.targetPhase = Phase::Planning;
  state.continuationCommand.targetSeatId = "seat-1";
  state.continuationCommand.payload.insert("mode", "arbitration");
  SeatUsageTally usage;
  usage.seatId = "seat-1";
  usage.totalTokens = 12;
  usage.inputTokens = 7;
  usage.outputTokens = 5;
  state.seatUsage.append(usage);
  QVERIFY(manager.saveTable(state));

  const QString auditConnectionName = QUuid::createUuid().toString();
  {
    QSqlDatabase auditDatabase =
        QSqlDatabase::addDatabase("QSQLITE", auditConnectionName);
    auditDatabase.setDatabaseName(databasePath);
    QVERIFY(auditDatabase.open());
    QSqlQuery query(auditDatabase);
    QVERIFY(query.exec("CREATE TABLE write_audit (value INTEGER)"));
    QVERIFY(query.exec(
        "CREATE TRIGGER count_transcript_insert AFTER INSERT ON "
        "transcript_entries BEGIN INSERT INTO write_audit VALUES (1); END"));

    TranscriptEntry appended = state.transcript.constLast();
    appended.entryId = "entry-new";
    appended.content = "New content";
    state.transcript.append(appended);
    QVERIFY(manager.saveTable(state));
    QCOMPARE(scalarInt(auditDatabase, "SELECT COUNT(*) FROM write_audit"), 1);

    state.transcript.removeFirst();
    QVERIFY(manager.saveTable(state));
    QCOMPARE(
        scalarInt(auditDatabase, "SELECT COUNT(*) FROM transcript_entries"),
        100);
    QCOMPARE(scalarInt(auditDatabase, "SELECT COUNT(*) FROM log_events"), 1);
    QCOMPARE(scalarInt(auditDatabase, "SELECT COUNT(*) FROM artifact_versions"),
             1);

    const QStringList expectedIndexes = {"idx_artifact_table_created",
                                         "idx_log_table_timestamp",
                                         "idx_transcript_table_timestamp"};
    for (const QString &index : expectedIndexes) {
      QSqlQuery indexQuery(auditDatabase);
      indexQuery.prepare(
          "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name=?");
      indexQuery.addBindValue(index);
      QVERIFY(indexQuery.exec());
      QVERIFY(indexQuery.next());
      QCOMPARE(indexQuery.value(0).toInt(), 1);
    }
    auditDatabase.close();
  }
  QSqlDatabase::removeDatabase(auditConnectionName);

  const QVector<SessionState> restored = manager.loadTables();
  QCOMPARE(restored.size(), 1);
  QCOMPARE(restored.first().transcript.size(), 100);
  QCOMPARE(restored.first().transcript.first().entryId, QString("entry-001"));
  QCOMPARE(restored.first().transcript.constLast().entryId,
           QString("entry-new"));
  QCOMPARE(restored.first().log.first().summary, QString("Test log"));
  QCOMPARE(restored.first().artifacts.first().versionId, QString("artifact-1"));
  QVERIFY(restored.first().usageEstimateUsed);
  QVERIFY(!restored.first().costEstimateComplete);
  QCOMPARE(restored.first().seatUsage.first().inputTokens, 7);
  QCOMPARE(restored.first().seatUsage.first().outputTokens, 5);
  QCOMPARE(static_cast<int>(restored.first().phase),
           static_cast<int>(Phase::Paused));
  QCOMPARE(static_cast<int>(restored.first().pausedResumePhase),
           static_cast<int>(Phase::Planning));
  QVERIFY(restored.first().continuationPending);
  QCOMPARE(restored.first().continuationLimitKind,
           static_cast<int>(BudgetLimitKind::MaxTotalTokens));
  QCOMPARE(restored.first().continuationReason,
           QString("The maximum total token limit has been reached."));
  QCOMPARE(restored.first().activeSeatId, QString("seat-1"));
  QCOMPARE(restored.first().usedTokens, 100);
  QCOMPARE(restored.first().execQcLoopCount, 2);
  QCOMPARE(static_cast<int>(restored.first().continuationCommand.commandType),
           static_cast<int>(RunnerCommandType::RequestDecision));
  QCOMPARE(static_cast<int>(restored.first().continuationCommand.targetPhase),
           static_cast<int>(Phase::Planning));
  QCOMPARE(restored.first().continuationCommand.targetSeatId,
           QString("seat-1"));
  QCOMPARE(restored.first().continuationCommand.payload.value("mode").toString(),
           QString("arbitration"));
}

void DatabaseTests::legacyTranscriptSchemaMigrates() {
  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());
  const QString databasePath = temporaryDirectory.filePath("legacy.db");
  const QString setupConnectionName = QUuid::createUuid().toString();
  {
    QSqlDatabase setup =
        QSqlDatabase::addDatabase("QSQLITE", setupConnectionName);
    setup.setDatabaseName(databasePath);
    QVERIFY(setup.open());
    QSqlQuery query(setup);
    QVERIFY(query.exec(
        "CREATE TABLE meeting_tables ("
        "table_id TEXT PRIMARY KEY, title TEXT NOT NULL, pinned INTEGER NOT NULL DEFAULT 0, "
        "updated_at TEXT NOT NULL DEFAULT '', phase TEXT NOT NULL, round_no INTEGER NOT NULL, "
        "active_seat_id TEXT, final_decision_maker_seat_id TEXT, used_tokens INTEGER NOT NULL, "
        "used_cost REAL NOT NULL, phase_used_tokens INTEGER NOT NULL DEFAULT 0, "
        "phase_used_cost REAL NOT NULL DEFAULT 0, elapsed_seconds INTEGER NOT NULL, "
        "pending_research_responses INTEGER NOT NULL, use_budget_overrides INTEGER NOT NULL DEFAULT 0, "
        "budget_override_json TEXT NOT NULL DEFAULT '{}', stop_policy_json TEXT NOT NULL DEFAULT '{}', "
        "seat_usage_json TEXT NOT NULL DEFAULT '[]', pending_seats_json TEXT NOT NULL DEFAULT '[]', "
        "attachments_json TEXT NOT NULL DEFAULT '[]', queued_input_ids_json TEXT NOT NULL DEFAULT '[]', "
        "current_artifact_version_id TEXT NOT NULL DEFAULT '', paused_resume_phase TEXT NOT NULL DEFAULT '', "
        "continuation_pending INTEGER NOT NULL DEFAULT 0, continuation_limit_kind INTEGER NOT NULL DEFAULT 0, "
        "continuation_reason TEXT NOT NULL DEFAULT '', arbitration_satisfied INTEGER NOT NULL DEFAULT 0, "
        "log_visible INTEGER NOT NULL, phase_elapsed_seconds INTEGER NOT NULL DEFAULT 0, seats_json TEXT NOT NULL)"));
    QVERIFY(query.exec(
        "CREATE TABLE transcript_entries ("
        "entry_id TEXT PRIMARY KEY, table_id TEXT NOT NULL, phase TEXT NOT "
        "NULL, round_no INTEGER NOT NULL, "
        "speaker_seat_id TEXT, speaker_name TEXT NOT NULL, is_decision INTEGER "
        "NOT NULL, content TEXT NOT NULL, timestamp TEXT NOT NULL)"));
    setup.close();
  }
  QSqlDatabase::removeDatabase(setupConnectionName);

  const QString managerConnection = QUuid::createUuid().toString();
  DatabaseManager manager(databasePath, managerConnection);
  QVERIFY(manager.initialize());

  const QString inspectConnectionName = QUuid::createUuid().toString();
  {
    QSqlDatabase inspect =
        QSqlDatabase::addDatabase("QSQLITE", inspectConnectionName);
    inspect.setDatabaseName(databasePath);
    QVERIFY(inspect.open());
    QSqlQuery query(inspect);
    QVERIFY(query.exec("PRAGMA table_info(transcript_entries)"));
    bool foundEntryType = false;
    bool foundUsageEstimate = false;
    bool foundCostEstimateComplete = false;
    bool foundContinuationCommand = false;
    bool foundExecQcLoopCount = false;
    while (query.next()) {
      foundEntryType =
          foundEntryType || query.value(1).toString() == "entry_type";
    }
    QVERIFY(foundEntryType);
    QVERIFY(query.exec("PRAGMA table_info(meeting_tables)"));
    while (query.next()) {
      foundUsageEstimate = foundUsageEstimate
          || query.value(1).toString() == "usage_estimate_used";
      foundCostEstimateComplete = foundCostEstimateComplete
          || query.value(1).toString() == "cost_estimate_complete";
      foundContinuationCommand = foundContinuationCommand
          || query.value(1).toString() == "continuation_command_json";
      foundExecQcLoopCount = foundExecQcLoopCount
          || query.value(1).toString() == "exec_qc_loop_count";
    }
    QVERIFY(foundUsageEstimate);
    QVERIFY(foundCostEstimateComplete);
    QVERIFY(foundContinuationCommand);
    QVERIFY(foundExecQcLoopCount);
    inspect.close();
  }
  QSqlDatabase::removeDatabase(inspectConnectionName);
}

QTEST_GUILESS_MAIN(DatabaseTests)

#include "test_database.moc"
