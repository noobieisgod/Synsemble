#include <QtTest>

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QNetworkProxy>

#include "app/application_context.h"
#include "app/mobile_app_controller.h"
#include "core/visible_text.h"

using namespace amt;

class ControllerTests final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void newTablesStartEmptyAndPersist();
  void granularSignalsOnlyRefreshChangedCollections();
  void fullTranscriptTextPreservesOrderAndMetadata();
  void budgetValidationRejectsInvalidRelationships();
  void legacyTokenBreakdownIsNotReportedAsZero();
  void credentialStatusExposesNoStoredSecret();
  void appearanceAndSeatColorAreExposed();
  void missingAttachmentCannotBeOpened();
  void attachmentCleanupIsReferenceAndPathSafe();
  void attachmentMetadataAddedOnlyAfterSuccess();
  void tableDeletionDuringImportRemovesCompletedResult();
  void staleAttachmentCompletionIsIgnored();
  void startupCleanupPreservesOwnedAttachments();
  void invalidComposerTaskDoesNotModifySession();
  void validComposerStartsAndPausesBeforeNetwork();
  void backgroundFlushSavesOriginatingTables();
  void presentationRedactsPrivateContent();
  void outcomeUnknownCannotResumeAfterRestore();
  void interruptedRequestsRestoreWithoutReplay();
  void uninitializedStorageCannotCreateTable();
  void duplicateResetsRuntimeUsage();
  void guideAndAgentRemovalPersist();
  void everyStateHasNextAction();
  void completedArtifactGuidance();
};

void ControllerTests::completedArtifactGuidance() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Artifact guidance"));
  auto state = controller.currentHandle();
  state->phase = Phase::Completed;
  const QString guidance = "Your final result is ready. Tap Team, then choose Artifacts to read it.";
  QVERIFY(!controller.currentTable().value("actionHint").toString().contains(guidance));
  state->artifacts.append(ArtifactVersion{});
  QVERIFY(controller.currentTable().value("actionHint").toString().startsWith(guidance));
  QCOMPARE(controller.currentTable().value("actionLabel").toString(), QString("Run again"));
  LogEvent error;
  error.type = LogEventType::ProviderCallFailed;
  error.summary = "Provider rejected the request";
  state->log.append(error);
  QVERIFY(controller.currentTable().value("actionHint").toString().startsWith(guidance));
  QVERIFY(controller.currentTable().value("actionHint").toString().contains("provider failure"));
  for (const auto phase : {Phase::Stopped, Phase::Failed, Phase::Research}) {
    state->phase = phase;
    QVERIFY(!controller.currentTable().value("actionHint").toString().contains(guidance));
  }
  state->phase = Phase::Completed;
  state->artifacts.clear();
  QVERIFY(!controller.currentTable().value("actionHint").toString().contains(guidance));
}

void ControllerTests::everyStateHasNextAction() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Actions"));
  auto state = controller.currentHandle();
  struct Case { Phase phase; const char *action; const char *label; };
  for (const auto &c : {Case{Phase::Idle, "start", "Start task"},
       Case{Phase::Research, "pause", "Pause"}, Case{Phase::Planning, "pause", "Pause"},
       Case{Phase::Execution, "pause", "Pause"}, Case{Phase::QualityControl, "pause", "Pause"},
       Case{Phase::Present, "pause", "Pause"}, Case{Phase::Completed, "restart", "Run again"},
       Case{Phase::Stopped, "restart", "Run again"}, Case{Phase::Failed, "restart", "Run again"}}) {
    state->phase = c.phase;
    QCOMPARE(controller.currentTable().value("nextAction").toString(), QString(c.action));
    QCOMPARE(controller.currentTable().value("actionLabel").toString(), QString(c.label));
    QVERIFY(!controller.currentTable().value("actionHint").toString().isEmpty());
  }
  state->phase = Phase::Paused; state->paused = true;
  state->continuationCommand.commandType = RunnerCommandType::RequestSeatTurn;
  QCOMPARE(controller.currentTable().value("nextAction").toString(), QString("resume"));
  state->continuationPending = true;
  QCOMPARE(controller.currentTable().value("nextAction").toString(), QString("continue"));
  state->continuationPending = false;
  state->continuationCommand.payload.insert("responseRejected", true);
  QCOMPARE(controller.currentTable().value("nextAction").toString(), QString("retry"));
  state->continuationCommand.payload.insert("outcomeUnknown", true);
  QCOMPARE(controller.currentTable().value("nextAction").toString(), QString("fresh"));
  QVERIFY(!controller.currentTable().value("canSubmit").toBool());
  QVERIFY(!controller.submitTask("Must retain draft in UI"));
  QVERIFY(!controller.sendMessage("Must not mutate transcript"));
  QVERIFY(!controller.addAttachmentForTable(state->tableId, QUrl::fromLocalFile("missing")));
  QVERIFY(!controller.runOrResume());
  QVERIFY(controller.transcript().isEmpty());
  QVERIFY(controller.flushCurrentSession());
  QVERIFY(controller.duplicateCurrentTable());
  QCOMPARE(controller.currentTable().value("nextAction").toString(), QString("start"));
  QVERIFY(state->continuationCommand.payload.value("outcomeUnknown").toBool());
  controller.selectTable(state->tableId);
  state->continuationCommand = {}; state->phase = Phase::Stopped; state->paused = false;
  LogEvent error; error.type = LogEventType::ProviderCallFailed; error.summary = "Gemini authentication rejected";
  state->log.append(error);
  QCOMPARE(controller.currentTable().value("nextAction").toString(), QString("restart"));
  QVERIFY(controller.currentTable().value("actionHint").toString().contains("provider failure"));
  state->phase = Phase::Research;
  QVERIFY(controller.currentTable().value("actionHint").toString().contains("continuing"));
  state->phase = Phase::Paused;
  QCOMPARE(controller.currentTable().value("nextAction").toString(), QString("fresh"));
  QVERIFY(!controller.runOrResume());
}

void ControllerTests::guideAndAgentRemovalPersist() {
  QSettings settings;
  settings.remove("ui/quickGuideSeen");
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(!controller.settings().value("quickGuideSeen").toBool());
  QVERIFY(controller.acknowledgeQuickGuide());
  QCOMPARE(settings.value("ui/quickGuideSeen").toBool(), true);
  QVERIFY(controller.createTable("Removal fixture"));
  QVERIFY(controller.saveSeat(0, true, "Decision", 0, "gpt-4.1-mini", 0, 1, "#16866c"));
  QVERIFY(controller.saveSeat(1, true, "Planner", 0, "gpt-4.1-mini", 0, 0, "#16866c"));
  QVERIFY(controller.sendMessage("Keep this history"));
  const auto stableId = controller.currentHandle()->seats[1].seatId;
  QVERIFY(!controller.removeAgent(0)); // Cannot remove the only decision maker with other active agents.
  QVERIFY(controller.removeAgent(1));
  QCOMPARE(controller.currentHandle()->seats[1].seatId, stableId);
  QVERIFY(!controller.currentHandle()->seats[1].occupied);
  QCOMPARE(controller.transcript().size(), 1);
  QVERIFY(controller.saveSeat(1, true, "Replacement", 0, "gpt-4.1-mini", 0, 0, "#16866c"));
  QCOMPARE(controller.currentHandle()->seats.size(), 2);
  QCOMPARE(controller.currentHandle()->seats[1].seatId, stableId);
  QVERIFY(controller.flushCurrentSession());
  MobileAppController restored;
  QVERIFY(restored.initialize());
  QVERIFY(restored.settings().value("quickGuideSeen").toBool());
  restored.selectTable(controller.currentTableId());
  QCOMPARE(restored.currentHandle()->seats.size(), 2);
  QCOMPARE(restored.transcript().size(), 1);
}

void ControllerTests::duplicateResetsRuntimeUsage() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Original"));
  auto original = controller.currentHandle();
  original->usedTokens = 123;
  original->seatUsage.append(SeatUsageTally{});
  original->usageEstimateUsed = true;
  original->costEstimateComplete = false;
  original->pendingResearchResponses = 2;
  original->arbitrationSatisfied = true;
  original->continuationCommand.payload.insert("outcomeUnknown", true);
  QVERIFY(controller.duplicateCurrentTable());
  const auto copy = controller.currentHandle();
  QVERIFY(copy->tableId != original->tableId);
  QCOMPARE(copy->usedTokens, 0);
  QVERIFY(copy->seatUsage.isEmpty());
  QVERIFY(!copy->usageEstimateUsed);
  QVERIFY(copy->costEstimateComplete);
  QCOMPARE(copy->pendingResearchResponses, 0);
  QVERIFY(!copy->arbitrationSatisfied);
  QVERIFY(copy->continuationCommand.payload.isEmpty());
  QCOMPARE(original->usedTokens, 123);
  QVERIFY(original->continuationCommand.payload.value("outcomeUnknown").toBool());
}

void ControllerTests::uninitializedStorageCannotCreateTable() {
  MobileAppController controller;
  QVERIFY(!controller.createTable("Must not save"));
  QVERIFY(controller.tables().isEmpty());
}

void ControllerTests::invalidComposerTaskDoesNotModifySession() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Invalid task"));
  QVERIFY(!controller.submitTask("Keep this draft"));
  QVERIFY(controller.transcript().isEmpty());
  QCOMPARE(controller.currentTable().value("phase").toString(), QString("Idle"));
}

void ControllerTests::validComposerStartsAndPausesBeforeNetwork() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Start task"));
  QVERIFY(controller.saveSeat(0, true, "Decision", 0, "gpt-4.1-mini", 0, 1, "#16866c"));
  QVERIFY(controller.saveSeat(1, true, "Planner", 0, "gpt-4.1-mini", 0, 0, "#16866c"));
  QVERIFY(controller.saveApiKey(0, "fixture-not-a-real-key"));
  // Exhaust the safety reserve before dispatch: this test cannot make provider calls.
  controller.currentHandle()->useBudgetOverrides = true;
  controller.currentHandle()->budgetOverrides.maxTotalTokens = 1;
  controller.currentHandle()->budgetOverrides.maxTokensPerPhase = 1;
  QVERIFY(controller.submitTask("Plan a synthetic example"));
  QCOMPARE(controller.transcript().size(), 1);
  QTRY_VERIFY(controller.currentTable().value("continuationPending").toBool());
  QCOMPARE(controller.currentTable().value("phase").toString(), QString("Needs continuation"));
  QVERIFY(controller.saveApiKey(0, ""));
}

void ControllerTests::backgroundFlushSavesOriginatingTables() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Origin"));
  const auto origin = controller.currentHandle();
  const QString originId = origin->tableId;
  QVERIFY(controller.createTable("Foreground"));
  origin->title = "Background result";
  controller.schedulePersistence(originId);
  QVERIFY(controller.flushCurrentSession());
  QVERIFY(!controller.m_pendingSaveIds.contains(originId));
  DatabaseManager database(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ai_meeting_table.db", "flush-verification");
  QVERIFY(database.initialize());
  const auto restored = database.loadTables();
  QVERIFY(std::any_of(restored.cbegin(), restored.cend(), [&](const SessionState &state) {
    return state.tableId == originId && state.title == "Background result";
  }));
}

void ControllerTests::presentationRedactsPrivateContent() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Redaction"));
  QVERIFY(controller.saveApiKey(0, "fixture-private-credential"));
  QVERIFY(controller.sendMessage("Visible\nAuthorization: Bearer secret\nfixture-private-credential\n<thinking>hidden</thinking>\nDone"));
  const auto copied = controller.fullTranscriptText();
  QVERIFY(copied.contains("Visible"));
  QVERIFY(copied.contains("Done"));
  QVERIFY(!copied.contains("fixture-private-credential"));
  QVERIFY(!copied.contains("Bearer"));
  QVERIFY(!copied.contains("hidden"));
  QVERIFY(!controller.transcript().first().toMap().value("content").toString().contains("hidden"));
  QCOMPARE(visibleText("{\"output\":[{\"type\":\"reasoning\"}]}"), QString("[Provider payload omitted]"));
  QCOMPARE(visibleText("<signature>private</signature>"), QString("[Private content omitted]"));
  QVERIFY(controller.sendMessage("{\"output\":[{\"text\":\"raw-private-body\"}]}"));
  QVERIFY(!controller.fullTranscriptText().contains("raw-private-body"));
  auto state = controller.currentHandle();
  LogEvent event;
  event.summary = "```json\n{\"signature\":\"private-signature\"}\n```";
  event.actorName = "fixture-private-credential";
  state->log.append(event);
  const auto activity = controller.logs().last().toMap();
  QVERIFY(!activity.value("summary").toString().contains("private-signature"));
  QVERIFY(!activity.value("actor").toString().contains("fixture-private-credential"));
  QCOMPARE(visibleText("partial {\"reasoning_content\":\"secret"), QString("[Provider payload omitted]"));
  QVERIFY(controller.saveApiKey(0, ""));
}

void ControllerTests::outcomeUnknownCannotResumeAfterRestore() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Unknown result"));
  auto state = controller.currentHandle();
  state->phase = Phase::Paused;
  state->paused = true;
  state->continuationCommand.payload.insert("outcomeUnknown", true);
  QVERIFY(controller.flushCurrentSession());
  QVERIFY(!controller.runOrResume());
  QCOMPARE(controller.currentTable().value("canResume").toBool(), false);
  DatabaseManager database(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ai_meeting_table.db", "unknown-verification");
  QVERIFY(database.initialize());
  const auto stored = database.loadTables();
  QVERIFY(std::any_of(stored.cbegin(), stored.cend(), [&](const SessionState &row) {
    return row.tableId == state->tableId && row.continuationCommand.payload.value("outcomeUnknown").toBool();
  }));
}

void ControllerTests::interruptedRequestsRestoreWithoutReplay() {
  QString interruptedId;
  QString pendingId;
  {
    MobileAppController controller;
    QVERIFY(controller.initialize());
    QVERIFY(controller.createTable("Interrupted request"));
    auto state = controller.currentHandle();
    interruptedId = state->tableId;
    state->phase = Phase::Planning;
    state->continuationCommand.payload.insert("requestInFlight", true);
    QVERIFY(controller.m_context.saveExisting(interruptedId));
    QVERIFY(controller.createTable("Safe delayed operation"));
    state = controller.currentHandle();
    pendingId = state->tableId;
    state->phase = Phase::Planning;
    state->continuationCommand = {RunnerCommandType::RequestSeatTurn, pendingId,
                                 Phase::Planning, "seat-next", {}};
    QVERIFY(controller.m_context.saveExisting(pendingId));
  }
  MobileAppController restored;
  QVERIFY(restored.initialize());
  restored.selectTable(interruptedId);
  QCOMPARE(restored.currentTable().value("phase").toString(), QString("Paused"));
  QVERIFY(!restored.currentTable().value("canResume").toBool());
  QVERIFY(!restored.runOrResume());
  restored.selectTable(pendingId);
  QCOMPARE(restored.currentTable().value("phase").toString(), QString("Paused"));
  QVERIFY(restored.currentTable().value("canResume").toBool());
  QCOMPARE(restored.currentHandle()->continuationCommand.targetSeatId, QString("seat-next"));
}

void ControllerTests::initTestCase() {
  // Fail closed if a fixture ever accidentally reaches the network transport.
  QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 9));
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName("Synsemble Tests");
  QCoreApplication::setApplicationName("Controller Tests");
  QDir testData(
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
  if (testData.exists()) {
    QVERIFY(testData.removeRecursively());
  }
  QSettings().clear();
}

void ControllerTests::newTablesStartEmptyAndPersist() {
  QString emptyTableId;
  QString existingTableId;
  {
    MobileAppController controller;
    QVERIFY(controller.initialize());
    QVERIFY(controller.tables().isEmpty());
    QVERIFY(controller.currentTableId().isEmpty());
    QVERIFY(controller.createTable("Existing table"));
    existingTableId = controller.currentTableId();
    const auto existing = controller.m_context.tableHandle(existingTableId);
    QVERIFY(existing);
    QVERIFY(existing->seats.isEmpty());

    QVERIFY(controller.saveSeat(0, true, "Existing decision maker", 0, "",
                                0, 1, "#4f86c6"));
    QCOMPARE(existing->seats.size(), 1);

    QVERIFY(controller.createTable("Empty persisted table"));
    emptyTableId = controller.currentTableId();
    QVERIFY(emptyTableId != existingTableId);
    const auto empty = controller.m_context.tableHandle(emptyTableId);
    QVERIFY(empty);
    QVERIFY(empty->seats.isEmpty());
    QVERIFY(controller.seats().isEmpty());
    QCOMPARE(existing->seats.size(), 1);
  }

  MobileAppController restored;
  QVERIFY(restored.initialize());
  QCOMPARE(restored.currentTableId(), emptyTableId);
  const auto empty = restored.m_context.tableHandle(emptyTableId);
  const auto existing = restored.m_context.tableHandle(existingTableId);
  QVERIFY(empty);
  QVERIFY(existing);
  QVERIFY(empty->seats.isEmpty());
  QCOMPARE(existing->seats.size(), 1);

  QVERIFY(restored.saveSeat(0, true, "New decision maker", 0, "", 0, 1,
                            "#2f9eaa"));
  QCOMPARE(empty->seats.size(), 1);
  QCOMPARE(restored.seats().size(), 1);
  QCOMPARE(existing->seats.size(), 1);
}

void ControllerTests::granularSignalsOnlyRefreshChangedCollections() {
  MobileAppController controller;
  QVERIFY(controller.initialize());

  QSignalSpy stateChanged(&controller, &MobileAppController::stateChanged);
  QSignalSpy tablesChanged(&controller, &MobileAppController::tablesChanged);
  QSignalSpy seatsChanged(&controller, &MobileAppController::seatsChanged);
  QSignalSpy transcriptChanged(&controller,
                               &MobileAppController::transcriptChanged);
  QSignalSpy artifactsChanged(&controller,
                              &MobileAppController::artifactsChanged);
  QSignalSpy logsChanged(&controller, &MobileAppController::logsChanged);

  QVERIFY(controller.sendMessage("Test objective"));
  QCOMPARE(stateChanged.count(), 1);
  QCOMPARE(tablesChanged.count(), 0);
  QCOMPARE(seatsChanged.count(), 0);
  QCOMPARE(transcriptChanged.count(), 1);
  QCOMPARE(artifactsChanged.count(), 0);
  QCOMPARE(logsChanged.count(), 1);

  QVERIFY(controller.renameCurrentTable("Renamed"));
  QCOMPARE(stateChanged.count(), 2);
  QCOMPARE(tablesChanged.count(), 1);
  QCOMPARE(transcriptChanged.count(), 1);
  QCOMPARE(logsChanged.count(), 1);

  QVERIFY(controller.saveSeat(0, false, "Seat 1", 0, "", 0, 0, "#49bd99"));
  QCOMPARE(stateChanged.count(), 3);
  QCOMPARE(seatsChanged.count(), 1);
  QCOMPARE(transcriptChanged.count(), 2);
  QCOMPARE(artifactsChanged.count(), 0);
  QCOMPARE(logsChanged.count(), 1);
}

void ControllerTests::fullTranscriptTextPreservesOrderAndMetadata() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Transcript copy"));
  QCOMPARE(controller.fullTranscriptText(), QString{});
  QVERIFY(controller.sendMessage("First copied message"));
  QVERIFY(controller.sendMessage("Second copied message"));

  const QString transcript = controller.fullTranscriptText();
  const qsizetype firstIndex = transcript.indexOf("First copied message");
  const qsizetype secondIndex = transcript.indexOf("Second copied message");
  QVERIFY(firstIndex >= 0);
  QVERIFY(secondIndex > firstIndex);
  QVERIFY(transcript.contains("You | Idle | Round 1"));
  QVERIFY(transcript.contains("First copied message\n\n["));
}

void ControllerTests::budgetValidationRejectsInvalidRelationships() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(!controller.saveGlobalBudget(1000, 999, 1, 1, 10, 20));
  QVERIFY(controller.lastError().contains("total tokens"));
  QVERIFY(!controller.saveGlobalBudget(1000, 2000, 1, 1, 30, 20));
  QVERIFY(controller.lastError().contains("session seconds"));
  QVERIFY(!controller.saveGlobalBudget(0, 2000, 1, 1, 10, 20));
  QVERIFY(controller.lastError().contains("positive"));
}

void ControllerTests::legacyTokenBreakdownIsNotReportedAsZero() {
  MobileAppController controller;

  SessionState legacy;
  legacy.usedTokens = 12085;
  QVERIFY(!controller.tableSummary(legacy).value("tokenBreakdownKnown").toBool());

  SeatUsageTally usage;
  usage.inputTokens = 9000;
  usage.outputTokens = 3085;
  legacy.seatUsage.append(usage);
  const QVariantMap current = controller.tableSummary(legacy);
  QVERIFY(current.value("tokenBreakdownKnown").toBool());
  QCOMPARE(current.value("inputTokens").toInt(), 9000);
  QCOMPARE(current.value("outputTokens").toInt(), 3085);
}

void ControllerTests::credentialStatusExposesNoStoredSecret() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  const QString secret = "synthetic-controller-secret-fragment";

  QCOMPARE(controller.metaObject()->indexOfMethod("apiKey(int)"), -1);
  QCOMPARE(controller.metaObject()->indexOfMethod("apiKeyStatus(int)"), -1);
  QVERIFY(controller.saveApiKey(0, secret));
  QVERIFY(controller.hasCredential(0));
  const QVariantMap settings = controller.settings();
  for (auto it = settings.cbegin(); it != settings.cend(); ++it) {
    QVERIFY(!it.value().toString().contains(secret));
  }
  for (const QVariant &entry : controller.logs()) {
    QVERIFY(!entry.toMap().value("summary").toString().contains(secret));
  }

  QVERIFY(controller.saveApiKey(0, ""));
  QVERIFY(!controller.hasCredential(0));
}

void ControllerTests::appearanceAndSeatColorAreExposed() {
  MobileAppController controller;
  QVERIFY(controller.initialize());

  QVERIFY(controller.saveAppearance("Dark", "Calm Workspace", "Console"));
  const QVariantMap settings = controller.settings();
  QCOMPARE(settings.value("appearance").toString(), QString("Dark"));
  QCOMPARE(settings.value("colorTheme").toString(), QString("Calm Workspace"));
  QCOMPARE(settings.value("fontStyle").toString(), QString("Console"));

  QVERIFY(controller.saveSeat(0, true, "Decision seat", 0, "", 0, 1,
                              "#ABCDEF"));
  const QVariantList seats = controller.seats();
  QCOMPARE(seats.first().toMap().value("color").toString(),
           QString("#abcdef"));

  QVERIFY(controller.saveSeat(0, true, "Decision seat", 0, "", 0, 1,
                              "#4f86c6"));
  QCOMPARE(controller.seats().first().toMap().value("color").toString(),
           QString("#4f86c6"));

  const QVariantMap safeguards = controller.attachmentSafeguards();
  QCOMPARE(safeguards.value("maximumAttachmentMiB").toLongLong(), qint64(25));
  QCOMPARE(safeguards.value("freeSpaceReserveMiB").toLongLong(), qint64(64));
  QCOMPARE(safeguards.value("noProgressTimeoutSeconds").toInt(), 60);
}

void ControllerTests::missingAttachmentCannotBeOpened() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Missing attachment"));
  const auto handle = controller.m_context.tableHandle(controller.currentTableId());
  QVERIFY(handle);

  AttachmentRecord missing;
  missing.attachmentId = "missing-attachment";
  missing.displayName = "missing notes.txt";
  missing.filePath = QDir::temp().filePath("amt-file-that-does-not-exist.txt");
  QFile::remove(missing.filePath);
  handle->attachments.append(missing);

  QVERIFY(!controller.openAttachment(missing.attachmentId));
  QVERIFY(controller.lastError().contains("no longer exists"));
  QVERIFY(!controller.openAttachment("unknown-attachment"));
  QVERIFY(controller.lastError().contains("no longer available"));
}

void ControllerTests::attachmentCleanupIsReferenceAndPathSafe() {
  ApplicationContext context;
  QVERIFY(context.initialize());
  QVERIFY(!context.tables().isEmpty());

  const QString attachmentRoot =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/attachments";
  QVERIFY(QDir().mkpath(attachmentRoot));
  const QString managedPath = attachmentRoot + "/managed-test.txt";
  {
    QFile file(managedPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("managed"), qint64(7));
  }

  AttachmentRecord attachment;
  attachment.attachmentId = "shared";
  attachment.filePath = managedPath;
  context.tables().first()->attachments.append(attachment);

  auto secondTable =
      std::make_shared<SessionState>(context.createSampleTable());
  secondTable->attachments.append(attachment);
  context.tables().append(secondTable);
  QVERIFY(!context.cleanupAttachmentFileIfUnreferenced(managedPath));
  QVERIFY(QFile::exists(managedPath));

  context.tables().first()->attachments.clear();
  QVERIFY(!context.cleanupAttachmentFileIfUnreferenced(managedPath));
  QVERIFY(QFile::exists(managedPath));

  secondTable->attachments.clear();
  QVERIFY(context.cleanupAttachmentFileIfUnreferenced(managedPath));
  QVERIFY(!QFile::exists(managedPath));

  QTemporaryDir externalDirectory;
  QVERIFY(externalDirectory.isValid());
  const QString externalPath = externalDirectory.filePath("external.txt");
  {
    QFile file(externalPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("external"), qint64(8));
  }
  QVERIFY(!context.cleanupAttachmentFileIfUnreferenced(externalPath));
  QVERIFY(QFile::exists(externalPath));
}

void ControllerTests::attachmentMetadataAddedOnlyAfterSuccess() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Async attachment"));
  const QString tableId = controller.currentTableId();
  const auto handle = controller.m_context.tableHandle(tableId);
  QVERIFY(handle);
  const qsizetype initialCount = handle->attachments.size();

  QTemporaryDir sourceDirectory;
  QVERIFY(sourceDirectory.isValid());
  const QString sourcePath = sourceDirectory.filePath("small-benign.txt");
  const QByteArray content("bounded attachment content");
  {
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(content), qint64(content.size()));
  }

  QSignalSpy importChanged(&controller,
                           &MobileAppController::attachmentImportChanged);
  QVERIFY(controller.createTable("Switched while picker was open"));
  QVERIFY(controller.addAttachmentForTable(tableId, QUrl::fromLocalFile(sourcePath)));
  QVERIFY(controller.attachmentImportInProgress());
  QCOMPARE(handle->attachments.size(), initialCount);
  QVERIFY(!controller.addAttachment(QUrl::fromLocalFile(sourcePath)));
  QVERIFY(controller.lastError().contains("already in progress"));

  QTRY_VERIFY_WITH_TIMEOUT(!controller.attachmentImportInProgress(), 5000);
  QVERIFY(importChanged.count() >= 2);
  QCOMPARE(handle->attachments.size(), initialCount + 1);
  const AttachmentRecord imported = handle->attachments.last();
  QCOMPARE(imported.fileHash,
           QString::fromLatin1(
               QCryptographicHash::hash(content, QCryptographicHash::Sha256)
                   .toHex()));
  QCOMPARE(QFileInfo(imported.filePath).size(), qint64(content.size()));
  QVERIFY(controller.attachments().isEmpty());
  controller.selectTable(tableId);
  QCOMPARE(controller.attachments().size(), initialCount + 1);
  QVERIFY(controller.removeAttachment(imported.attachmentId));
  QVERIFY(!QFile::exists(imported.filePath));
  QVERIFY(QFile::exists(sourcePath));
}

void ControllerTests::tableDeletionDuringImportRemovesCompletedResult() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  QVERIFY(controller.createTable("Delete during import"));
  const QString tableId = controller.currentTableId();
  const QString attachmentRoot =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/attachments";
  QVERIFY(QDir().mkpath(attachmentRoot));
  const QString completedPath = attachmentRoot + "/deleted-table-result.txt";
  {
    QFile file(completedPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("result"), qint64(6));
  }

  controller.m_attachmentImportOperationId = "delete-operation";
  controller.m_attachmentImportTableId = tableId;
  controller.m_attachmentImportInProgress = true;
  QVERIFY(controller.deleteCurrentTable());
  QVERIFY(!controller.m_context.tableHandle(tableId));

  AttachmentImportResult result;
  result.operationId = "delete-operation";
  result.status = AttachmentImportStatus::Success;
  result.finalPath = completedPath;
  result.byteCount = 6;
  result.sha256 = QString(64, 'a');
  controller.handleAttachmentImportFinished(result);
  QVERIFY(!QFile::exists(completedPath));
  QVERIFY(!controller.attachmentImportInProgress());
}

void ControllerTests::staleAttachmentCompletionIsIgnored() {
  MobileAppController controller;
  QVERIFY(controller.initialize());
  if (controller.currentTableId().isEmpty()) {
    QVERIFY(controller.createTable("Stale import"));
  }
  const auto handle = controller.m_context.tableHandle(controller.currentTableId());
  QVERIFY(handle);
  const qsizetype initialCount = handle->attachments.size();

  const QString attachmentRoot =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/attachments";
  QVERIFY(QDir().mkpath(attachmentRoot));
  const QString stalePath = attachmentRoot + "/stale-result.txt";
  {
    QFile file(stalePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("stale"), qint64(5));
  }

  controller.m_attachmentImportOperationId = "active-operation";
  controller.m_attachmentImportTableId = controller.currentTableId();
  controller.m_attachmentImportInProgress = true;
  AttachmentImportResult stale;
  stale.operationId = "stale-operation";
  stale.status = AttachmentImportStatus::Success;
  stale.finalPath = stalePath;
  stale.byteCount = 5;
  stale.sha256 = QString(64, 'b');
  controller.handleAttachmentImportFinished(stale);

  QVERIFY(!QFile::exists(stalePath));
  QVERIFY(controller.attachmentImportInProgress());
  QCOMPARE(handle->attachments.size(), initialCount);
  controller.m_attachmentImportOperationId.clear();
  controller.m_attachmentImportTableId.clear();
  controller.m_attachmentImportInProgress = false;
}

void ControllerTests::startupCleanupPreservesOwnedAttachments() {
  ApplicationContext context;
  QVERIFY(context.initialize());
  QVERIFY(!context.tables().isEmpty());
  auto table = context.tables().first();
  const QString attachmentRoot =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/attachments";
  QVERIFY(QDir().mkpath(attachmentRoot));

  const QString ownedPath = attachmentRoot + "/owned-existing.part";
  const QString orphanPath = attachmentRoot + "/completed-unowned.txt";
  const QString partialPath = attachmentRoot + "/interrupted.part";
  for (const QString &path : {ownedPath, orphanPath, partialPath}) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("data"), qint64(4));
  }

  AttachmentRecord owned;
  owned.attachmentId = "owned-existing";
  owned.displayName = "owned-existing.txt";
  owned.filePath = ownedPath;
  owned.fileHash = QString(64, 'c');
  owned.addedAt = QDateTime::currentDateTimeUtc();
  table->attachments.append(owned);
  QVERIFY(context.save(*table));

  ApplicationContext restored;
  QVERIFY(restored.initialize());
  QVERIFY(QFile::exists(ownedPath));
  QVERIFY(!QFile::exists(orphanPath));
  QVERIFY(!QFile::exists(partialPath));

  const auto restoredTable = restored.tableHandle(table->tableId);
  QVERIFY(restoredTable);
  restoredTable->attachments.erase(
      std::remove_if(restoredTable->attachments.begin(),
                     restoredTable->attachments.end(),
                     [](const AttachmentRecord &attachment) {
                       return attachment.attachmentId == "owned-existing";
                     }),
      restoredTable->attachments.end());
  QVERIFY(restored.save(*restoredTable));
  QVERIFY(restored.cleanupAttachmentFileIfUnreferenced(ownedPath));
}

QTEST_GUILESS_MAIN(ControllerTests)

#include "test_controller.moc"
