#include <QtTest>

#include <algorithm>

#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "persistence/database_manager.h"

#include "core/event_bus.h"
#include "core/session_runner.h"
#include "core/workflow_engine.h"
#include "providers/provider_gateway.h"
#include "services/artifact_manager.h"
#include "services/budget_manager.h"

using namespace amt;

namespace {

class FakeProviderGateway final : public ProviderGateway {
public:
  using ProviderGateway::ProviderGateway;

  void sendAsync(const ProviderRequest &request) override {
    requests.append(request);
  }

  QVector<ProviderRequest> requests;
};

std::shared_ptr<SessionState> makeSession(Phase phase) {
  auto state = std::make_shared<SessionState>();
  state->tableId = "session";
  state->updatedAt = QDateTime::currentDateTimeUtc();
  state->phase = phase;
  state->round = 1;

  SeatConfig participant;
  participant.seatId = "seat-participant";
  participant.displayName = "Participant";
  participant.occupied = true;
  participant.enabled = true;
  state->seats.append(participant);

  SeatConfig decisionMaker = participant;
  decisionMaker.seatId = "seat-fdm";
  decisionMaker.displayName = "Decision Maker";
  decisionMaker.role = Role::FinalDecisionMaker;
  state->seats.append(decisionMaker);
  state->finalDecisionMakerSeatId = decisionMaker.seatId;
  return state;
}

ProviderResponse responseFor(const ProviderRequest &request,
                             const QString &outcome, const QString &content) {
  ProviderResponse response;
  response.requestId = request.requestId;
  response.sessionId = request.sessionId;
  response.seatId = request.seatId;
  response.success = true;
  response.content = content;
  response.decisionOutcome = outcome;
  response.usageReported = true;
  response.runGeneration = request.runGeneration;
  return response;
}

} // namespace

class AsyncTests final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void unknownAndGenerationStaleResponsesAreRejected();
  void duplicateArbitrationResponseIsConsumedOnce();
  void phaseAndRoundStaleResponsesAreRejected();
  void qualityControlRevisionReturnsToExecutionWithSafeDiagnostic();
  void presentProceedAliasCompletesOnce();
  void outcomeUnknownDoesNotRecordConfirmedUsage();
  void malformedOutputPausesAndPreservesSession();
  void rejectedResearchStopsWithoutUnknownLock();
  void supportedHardStopsPauseBeforeDispatch_data();
  void supportedHardStopsPauseBeforeDispatch();
  void hardStopBeforeDispatchResumesOnce();
  void postResponseOvershootPausesNextOperation();
  void restoredContinuationResumesWithoutReplay();
  void checkpointPreventsUnrecordedDispatch();
  void manualPauseRestoresExactPendingCommand();
  void convergencePromptsPreserveArtifactAuthority();
  void convergenceFixtureCompletesAfterOneTargetedRevision();
};

void AsyncTests::rejectedResearchStopsWithoutUnknownLock() {
  auto state = makeSession(Phase::Research);
  EventBus bus; WorkflowEngine workflow; FakeProviderGateway gateway;
  BudgetManager budget; ArtifactManager artifacts;
  SessionRunner runner(&bus, &workflow, &gateway, &budget, &artifacts,
      [state](const QString &id) { return id == state->tableId ? state : nullptr; });
  runner.executeCommand(*state, {RunnerCommandType::RunResearchBatch, state->tableId, Phase::Research, {}, {}});
  QVERIFY(!gateway.requests.isEmpty());
  const auto requests = gateway.requests;
  for (const auto &request : requests) {
    auto failure = responseFor(request, {}, {});
    failure.success = false; failure.deliveryOutcome = ProviderDeliveryOutcome::DefiniteFailure;
    failure.errorMessage = "Authentication rejected (HTTP 401).";
    gateway.responseReady(failure);
  }
  QCOMPARE(int(state->phase), int(Phase::Stopped));
  QVERIFY(!state->continuationCommand.payload.value("outcomeUnknown").toBool());
  QCOMPARE(gateway.requests.size(), requests.size());
  QVERIFY(std::any_of(state->log.cbegin(), state->log.cend(), [](const LogEvent &event) {
    return !event.actorName.isEmpty() && event.summary.contains("OpenAI") && event.summary.contains("401");
  }));
}

void AsyncTests::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName("Synsemble Tests");
  QCoreApplication::setApplicationName("Async Tests");
}

void AsyncTests::checkpointPreventsUnrecordedDispatch() {
  auto state = makeSession(Phase::Planning);
  EventBus bus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&bus, &workflow, &gateway, &budget, &artifacts,
      [state](const QString &id) { return id == state->tableId ? state : nullptr; });
  bool durable = false;
  SessionState checkpoint;
  runner.setCheckpoint([&](const SessionState &value) { checkpoint = value; return durable; });
  WorkflowCommand command{RunnerCommandType::RequestSeatTurn, state->tableId,
                          Phase::Planning, "seat-participant", {}};
  runner.executeCommand(*state, command);
  QVERIFY(gateway.requests.isEmpty());
  QVERIFY(state->paused);
  QCOMPARE(state->continuationCommand.targetSeatId, command.targetSeatId);
  durable = true;
  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 1);
  QVERIFY(checkpoint.continuationCommand.payload.value("requestInFlight").toBool());
  auto response = responseFor(gateway.requests.first(), {}, "Visible fixture response");
  auto misrouted = response;
  misrouted.sessionId = "another-table";
  gateway.responseReady(misrouted);
  QVERIFY(state->transcript.isEmpty());
  gateway.responseReady(response);
  QCOMPARE(state->transcript.size(), 1);
  gateway.responseReady(response);
  QCOMPARE(state->transcript.size(), 1);
}

void AsyncTests::manualPauseRestoresExactPendingCommand() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  DatabaseManager database(directory.filePath("pause.db"), "pause-checkpoint");
  QVERIFY(database.initialize());
  auto state = makeSession(Phase::Planning);
  EventBus bus;
  WorkflowEngine workflow;
  BudgetManager budget;
  ArtifactManager artifacts;
  QString pendingSeat;
  RunnerCommandType pendingType;
  {
    FakeProviderGateway gateway;
    SessionRunner runner(&bus, &workflow, &gateway, &budget, &artifacts,
        [state](const QString &id) { return id == state->tableId ? state : nullptr; });
    WorkflowCommand command{RunnerCommandType::RequestSeatTurn, state->tableId,
                            Phase::Planning, "seat-participant", {}};
    runner.executeCommand(*state, command);
    QCOMPARE(gateway.requests.size(), 1);
    gateway.responseReady(responseFor(gateway.requests.first(), {}, "Completed exactly once"));
    runner.requestPause(*state);
    QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Paused));
    QVERIFY(!state->continuationPending);
    pendingType = state->continuationCommand.commandType;
    pendingSeat = state->continuationCommand.targetSeatId;
    QVERIFY(pendingType != RunnerCommandType::None);
    QVERIFY(database.saveTable(*state));
  }
  const auto restored = database.loadTables();
  QCOMPARE(restored.size(), 1);
  state = std::make_shared<SessionState>(restored.first());
  FakeProviderGateway gateway;
  SessionRunner runner(&bus, &workflow, &gateway, &budget, &artifacts,
      [state](const QString &id) { return id == state->tableId ? state : nullptr; });
  QCOMPARE(state->continuationCommand.commandType, pendingType);
  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 1);
  QCOMPARE(gateway.requests.first().seatId, pendingSeat);
  QCOMPARE(state->transcript.size(), 1);
  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 1);
}

void AsyncTests::unknownAndGenerationStaleResponsesAreRejected() {
  auto state = makeSession(Phase::Research);
  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  ProviderResponse unknown;
  unknown.requestId = "unknown";
  unknown.sessionId = state->tableId;
  unknown.seatId = "seat-participant";
  unknown.success = true;
  unknown.content = "Unknown content";
  unknown.runGeneration = 0;
  gateway.responseReady(unknown);
  QCOMPARE(state->transcript.size(), 0);
  QCOMPARE(state->usedTokens, 0);

  WorkflowCommand turn;
  turn.commandType = RunnerCommandType::RequestSeatTurn;
  turn.sessionId = state->tableId;
  turn.targetPhase = state->phase;
  turn.targetSeatId = "seat-participant";
  runner.executeCommand(*state, turn);
  QCOMPARE(gateway.requests.size(), 1);

  ProviderResponse wrongSession =
      responseFor(gateway.requests.last(), {}, "Wrong session");
  wrongSession.sessionId = "other-session";
  gateway.responseReady(wrongSession);
  QCOMPARE(state->transcript.size(), 0);

  runner.executeCommand(*state, turn);
  QCOMPARE(gateway.requests.size(), 2);
  ProviderResponse wrongSeat =
      responseFor(gateway.requests.last(), {}, "Wrong seat");
  wrongSeat.seatId = "seat-fdm";
  gateway.responseReady(wrongSeat);
  QCOMPARE(state->transcript.size(), 0);

  runner.executeCommand(*state, turn);
  QCOMPARE(gateway.requests.size(), 3);
  ProviderResponse stale = responseFor(gateway.requests.last(), {}, "Stale");
  stale.runGeneration += 1;
  gateway.responseReady(stale);
  QCOMPARE(state->transcript.size(), 0);

  runner.executeCommand(*state, turn);
  const ProviderResponse previousRun =
      responseFor(gateway.requests.last(), {}, "Previous run");
  runner.startSession(*state);
  gateway.responseReady(previousRun);
  QCOMPARE(state->transcript.size(), 0);
}

void AsyncTests::duplicateArbitrationResponseIsConsumedOnce() {
  auto state = makeSession(Phase::Planning);
  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  WorkflowCommand decision;
  decision.commandType = RunnerCommandType::RequestDecision;
  decision.sessionId = state->tableId;
  decision.targetPhase = state->phase;
  decision.targetSeatId = state->finalDecisionMakerSeatId;
  decision.payload.insert("mode", "arbitration");
  runner.executeCommand(*state, decision);
  QCOMPARE(gateway.requests.size(), 1);
  QVERIFY(gateway.requests.first()
              .prompt.value("instruction")
              .toString()
              .contains("Planning to Execution"));

  const ProviderResponse proceed =
      responseFor(gateway.requests.first(), "Proceed",
                  "Explanation: move forward.\nFINAL_RULING: PROCEED");
  gateway.responseReady(proceed);
  QCOMPARE(state->transcript.size(), 1);
  QVERIFY(state->transcript.first().content.startsWith(
      "Planning arbitration: Proceed to Execution"));
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Execution));

  gateway.responseReady(proceed);
  QCOMPARE(state->transcript.size(), 1);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Execution));
}

void AsyncTests::phaseAndRoundStaleResponsesAreRejected() {
  auto state = makeSession(Phase::QualityControl);
  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  WorkflowCommand decision;
  decision.commandType = RunnerCommandType::RequestDecision;
  decision.sessionId = state->tableId;
  decision.targetPhase = state->phase;
  decision.targetSeatId = state->finalDecisionMakerSeatId;
  decision.payload.insert("mode", "arbitration");
  runner.executeCommand(*state, decision);
  QCOMPARE(gateway.requests.size(), 1);
  state->round += 1;
  gateway.responseReady(responseFor(gateway.requests.first(), "Proceed",
                                    "FINAL_RULING: PROCEED"));
  QCOMPARE(state->transcript.size(), 0);

  state->round = 1;
  runner.executeCommand(*state, decision);
  QCOMPARE(gateway.requests.size(), 2);
  state->phase = Phase::Execution;
  gateway.responseReady(
      responseFor(gateway.requests.last(), "Revise", "FINAL_RULING: REVISE"));
  QCOMPARE(state->transcript.size(), 0);
}

void AsyncTests::qualityControlRevisionReturnsToExecutionWithSafeDiagnostic() {
  auto state = makeSession(Phase::QualityControl);
  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  WorkflowCommand decision;
  decision.commandType = RunnerCommandType::RequestDecision;
  decision.sessionId = state->tableId;
  decision.targetPhase = state->phase;
  decision.targetSeatId = state->finalDecisionMakerSeatId;
  decision.payload.insert("mode", "arbitration");
  runner.executeCommand(*state, decision);

  ProviderResponse revise =
      responseFor(gateway.requests.first(), "Revise",
                  "private response content\nFINAL_RULING: REVISE");
  revise.multipleDecisionRulings = true;
  gateway.responseReady(revise);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Execution));
  QCOMPARE(state->transcript.size(), 1);
  QVERIFY(state->transcript.first().content.startsWith(
      "Quality Control arbitration: Revise via Execution"));
  QVERIFY(std::any_of(state->log.cbegin(), state->log.cend(),
                      [](const LogEvent &event) {
                        return event.summary.contains(
                            "Multiple explicit final decision ruling lines");
                      }));
  QVERIFY(std::none_of(
      state->log.cbegin(), state->log.cend(), [](const LogEvent &event) {
        return event.summary.contains("private response content");
      }));
}

void AsyncTests::presentProceedAliasCompletesOnce() {
  auto state = makeSession(Phase::Present);
  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  WorkflowCommand decision;
  decision.commandType = RunnerCommandType::RequestDecision;
  decision.sessionId = state->tableId;
  decision.targetPhase = state->phase;
  decision.targetSeatId = state->finalDecisionMakerSeatId;
  runner.executeCommand(*state, decision);

  const ProviderResponse approve = responseFor(
      gateway.requests.first(), "Approve",
      "Explanation: ready.\nFINAL_RULING: PROCEED");
  gateway.responseReady(approve);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Completed));
  QCOMPARE(state->transcript.size(), 1);
  QVERIFY(state->transcript.first().content.startsWith(
      "Final decision: Approved and complete"));

  gateway.responseReady(approve);
  QCOMPARE(state->transcript.size(), 1);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Completed));
}

void AsyncTests::outcomeUnknownDoesNotRecordConfirmedUsage() {
  auto state = makeSession(Phase::Planning);
  state->usedTokens = 11;
  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  WorkflowCommand turn;
  turn.commandType = RunnerCommandType::RequestSeatTurn;
  turn.sessionId = state->tableId;
  turn.targetPhase = state->phase;
  turn.targetSeatId = "seat-participant";
  runner.executeCommand(*state, turn);
  QCOMPARE(gateway.requests.size(), 1);

  turn.targetSeatId = "seat-fdm";
  runner.executeCommand(*state, turn);
  QCOMPARE(gateway.requests.size(), 2);

  ProviderResponse unknown;
  unknown.requestId = gateway.requests.first().requestId;
  unknown.sessionId = state->tableId;
  unknown.seatId = "seat-participant";
  unknown.success = false;
  unknown.deliveryOutcome = ProviderDeliveryOutcome::OutcomeUnknown;
  unknown.usedTokens = 9000;
  unknown.errorMessage =
      "The provider may have completed the request, but the app did not receive a confirmed result. Trying again could duplicate provider work or usage.";
  unknown.runGeneration = gateway.requests.first().runGeneration;
  gateway.responseReady(unknown);

  QCOMPARE(state->usedTokens, 11);
  QVERIFY(state->paused);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Paused));
  QVERIFY(std::any_of(state->log.cbegin(), state->log.cend(),
                      [](const LogEvent &event) {
                        return event.summary.contains(
                            "could duplicate provider work or usage");
                      }));

  const auto transcriptCount = state->transcript.size();
  ProviderResponse late;
  late.requestId = gateway.requests.last().requestId;
  late.sessionId = state->tableId;
  late.seatId = "seat-fdm";
  late.runGeneration = gateway.requests.last().runGeneration;
  late.content = "Late response must not mutate the paused session";
  late.usedTokens = 100;
  gateway.responseReady(late);
  QCOMPARE(state->transcript.size(), transcriptCount);
  QCOMPARE(state->usedTokens, 11);

  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 2);
  QVERIFY(state->continuationCommand.payload.value("outcomeUnknown").toBool());
  runner.startSession(*state);
  QCOMPARE(gateway.requests.size(), 2);
  QCOMPARE(state->usedTokens, 11);
  runner.stopSession(*state);
  runner.startSession(*state);
  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 2);
  QVERIFY(state->continuationCommand.payload.value("outcomeUnknown").toBool());
}

void AsyncTests::malformedOutputPausesAndPreservesSession() {
  auto state = makeSession(Phase::Planning);
  TranscriptEntry completed;
  completed.entryId = "completed";
  completed.tableId = state->tableId;
  completed.speakerSeatId = "seat-fdm";
  completed.speakerName = "Decision Maker";
  completed.content = "Valid earlier response";
  state->transcript.append(completed);

  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  WorkflowCommand turn;
  turn.commandType = RunnerCommandType::RequestSeatTurn;
  turn.sessionId = state->tableId;
  turn.targetPhase = state->phase;
  turn.targetSeatId = "seat-participant";
  runner.executeCommand(*state, turn);
  QCOMPARE(gateway.requests.size(), 1);

  ProviderResponse malformed;
  malformed.requestId = gateway.requests.first().requestId;
  malformed.sessionId = state->tableId;
  malformed.seatId = "seat-participant";
  malformed.success = false;
  malformed.deliveryOutcome = ProviderDeliveryOutcome::DefiniteFailure;
  malformed.errorMessage = "OpenAI returned no user-visible assistant text.";
  malformed.usedTokens = 12;
  malformed.usageReported = true;
  malformed.runGeneration = gateway.requests.first().runGeneration;
  gateway.responseReady(malformed);

  QCOMPARE(state->transcript.size(), 1);
  QCOMPARE(state->transcript.first().content, QString("Valid earlier response"));
  QCOMPARE(state->usedTokens, 12);
  QVERIFY(state->paused);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Paused));
  QVERIFY(std::any_of(state->log.cbegin(), state->log.cend(),
                      [](const LogEvent &event) {
                        return event.summary.contains("paused for recovery");
                      }));

  gateway.responseReady(malformed);
  QCOMPARE(state->usedTokens, 12);
  QCOMPARE(state->transcript.size(), 1);

  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 2);
  QCOMPARE(gateway.requests.last().seatId, QString("seat-participant"));
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Planning));
}

void AsyncTests::supportedHardStopsPauseBeforeDispatch_data() {
  QTest::addColumn<int>("limitKind");
  QTest::newRow("phase tokens")
      << static_cast<int>(BudgetLimitKind::MaxPhaseTokens);
  QTest::newRow("total tokens")
      << static_cast<int>(BudgetLimitKind::MaxTotalTokens);
  QTest::newRow("rounds")
      << static_cast<int>(BudgetLimitKind::MaxRoundsPerPhase);
  QTest::newRow("execution quality loops")
      << static_cast<int>(BudgetLimitKind::MaxExecQcLoops);
  QTest::newRow("phase duration")
      << static_cast<int>(BudgetLimitKind::MaxPhaseSeconds);
  QTest::newRow("session duration")
      << static_cast<int>(BudgetLimitKind::MaxSessionSeconds);
}

void AsyncTests::supportedHardStopsPauseBeforeDispatch() {
  QFETCH(int, limitKind);
  const auto kind = static_cast<BudgetLimitKind>(limitKind);
  auto state = makeSession(Phase::Planning);
  switch (kind) {
  case BudgetLimitKind::MaxPhaseTokens:
    state->phaseUsedTokens = state->budgetPolicy.maxTokensPerPhase;
    break;
  case BudgetLimitKind::MaxTotalTokens:
    state->usedTokens = state->budgetPolicy.maxTotalTokens;
    break;
  case BudgetLimitKind::MaxRoundsPerPhase:
    state->round = state->budgetPolicy.maxRounds + 1;
    break;
  case BudgetLimitKind::MaxExecQcLoops:
    state->execQcLoopCount = state->budgetPolicy.maxExecQcLoops + 1;
    break;
  case BudgetLimitKind::MaxPhaseSeconds:
    state->phaseElapsedSeconds = state->budgetPolicy.maxPhaseSeconds;
    break;
  case BudgetLimitKind::MaxSessionSeconds:
    state->elapsedSeconds = state->budgetPolicy.maxSessionSeconds;
    break;
  case BudgetLimitKind::None:
  case BudgetLimitKind::MaxTotalCost:
  case BudgetLimitKind::SafetyReserve:
    QFAIL("The test row does not represent a supported workflow hard stop.");
  }

  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  WorkflowCommand turn;
  turn.commandType = RunnerCommandType::RequestSeatTurn;
  turn.sessionId = state->tableId;
  turn.targetPhase = Phase::Planning;
  turn.targetSeatId = "seat-participant";
  runner.executeCommand(*state, turn);

  QCOMPARE(gateway.requests.size(), 0);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Paused));
  QCOMPARE(state->continuationLimitKind, limitKind);
  QCOMPARE(state->continuationCommand.targetSeatId,
           QString("seat-participant"));
  QVERIFY(std::none_of(state->transcript.cbegin(), state->transcript.cend(),
                       [](const TranscriptEntry &entry) {
                         return entry.content.contains("Skipped turn");
                       }));

  const int usedTokens = state->usedTokens;
  runner.grantContinuation(*state, kind);
  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 1);
  QCOMPARE(gateway.requests.first().seatId, QString("seat-participant"));
  QCOMPARE(state->usedTokens, usedTokens);
}

void AsyncTests::hardStopBeforeDispatchResumesOnce() {
  auto state = makeSession(Phase::Planning);
  state->budgetPolicy.maxTokensPerPhase = 1000;
  state->budgetPolicy.maxTotalTokens = 100;
  state->usedTokens = 100;
  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  WorkflowCommand turn;
  turn.commandType = RunnerCommandType::RequestSeatTurn;
  turn.sessionId = state->tableId;
  turn.targetPhase = Phase::Planning;
  turn.targetSeatId = "seat-participant";
  runner.executeCommand(*state, turn);

  QCOMPARE(gateway.requests.size(), 0);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Paused));
  QVERIFY(state->paused);
  QVERIFY(state->continuationPending);
  QCOMPARE(state->continuationLimitKind,
           static_cast<int>(BudgetLimitKind::MaxTotalTokens));
  QVERIFY(state->continuationReason.contains("total token limit"));
  QCOMPARE(static_cast<int>(state->pausedResumePhase),
           static_cast<int>(Phase::Planning));
  QCOMPARE(state->activeSeatId, QString("seat-participant"));
  QCOMPARE(static_cast<int>(state->continuationCommand.commandType),
           static_cast<int>(RunnerCommandType::RequestSeatTurn));
  QCOMPARE(state->continuationCommand.targetSeatId,
           QString("seat-participant"));
  QVERIFY(std::none_of(state->transcript.cbegin(), state->transcript.cend(),
                       [](const TranscriptEntry &entry) {
                         return entry.content.contains("Skipped turn");
                       }));

  const int tokensBeforeContinue = state->usedTokens;
  runner.grantContinuation(*state, BudgetLimitKind::MaxTotalTokens);
  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 1);
  QCOMPARE(gateway.requests.first().seatId, QString("seat-participant"));
  QCOMPARE(state->usedTokens, tokensBeforeContinue);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Planning));

  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 1);
  runner.stopSession(*state, "Stopped by test");
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Stopped));
  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 1);
}

void AsyncTests::postResponseOvershootPausesNextOperation() {
  auto state = makeSession(Phase::Planning);
  SeatConfig second = state->seats.first();
  second.seatId = "seat-second";
  second.displayName = "Second participant";
  state->seats.insert(1, second);
  state->budgetPolicy.maxTokensPerPhase = 5000;
  state->budgetPolicy.maxTotalTokens = 2000;
  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  WorkflowCommand turn;
  turn.commandType = RunnerCommandType::RequestSeatTurn;
  turn.sessionId = state->tableId;
  turn.targetPhase = Phase::Planning;
  turn.targetSeatId = "seat-participant";
  runner.executeCommand(*state, turn);
  QCOMPARE(gateway.requests.size(), 1);

  ProviderResponse first =
      responseFor(gateway.requests.first(), {}, "Completed first response");
  first.inputTokens = 2000;
  first.outputTokens = 500;
  first.usedTokens = 2500;
  gateway.responseReady(first);

  QCOMPARE(state->transcript.size(), 1);
  QCOMPARE(state->transcript.first().content,
           QString("Completed first response"));
  QCOMPARE(state->usedTokens, 2500);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Paused));
  QCOMPARE(state->activeSeatId, QString("seat-participant"));
  QCOMPARE(static_cast<int>(state->continuationCommand.commandType),
           static_cast<int>(RunnerCommandType::HandleWorkflowEvent));
  QCOMPARE(gateway.requests.size(), 1);
  QVERIFY(std::none_of(state->transcript.cbegin(), state->transcript.cend(),
                       [](const TranscriptEntry &entry) {
                         return entry.content.contains("Skipped turn");
                       }));

  runner.grantContinuation(*state, BudgetLimitKind::MaxTotalTokens);
  runner.resumeSession(*state);
  QCOMPARE(state->activeSeatId, QString("seat-second"));
  QCOMPARE(gateway.requests.size(), 1);

  // Consume the normal inter-turn delay without waiting five seconds.
  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 2);
  QCOMPARE(gateway.requests.first().seatId, QString("seat-participant"));
  QCOMPARE(gateway.requests.last().seatId, QString("seat-second"));
  QCOMPARE(state->transcript.size(), 1);
  QCOMPARE(state->usedTokens, 2500);

  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 2);

  ProviderResponse secondResponse =
      responseFor(gateway.requests.last(), {}, "Completed second response");
  secondResponse.inputTokens = 75;
  secondResponse.outputTokens = 25;
  secondResponse.usedTokens = 100;
  gateway.responseReady(secondResponse);

  QCOMPARE(state->transcript.size(), 2);
  QCOMPARE(state->usedTokens, 2600);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Paused));
  QVERIFY(state->continuationPending);
  QCOMPARE(state->activeSeatId, QString("seat-second"));
}

void AsyncTests::restoredContinuationResumesWithoutReplay() {
  auto state = makeSession(Phase::Paused);
  state->pausedResumePhase = Phase::Planning;
  state->paused = true;
  state->continuationPending = true;
  state->continuationLimitKind =
      static_cast<int>(BudgetLimitKind::MaxTotalTokens);
  state->continuationReason = "The maximum total token limit has been reached.";
  state->budgetPolicy.maxTokensPerPhase = 1000;
  state->budgetPolicy.maxTotalTokens = 100;
  state->usedTokens = 100;
  state->activeSeatId = "seat-participant";
  state->continuationCommand.commandType =
      RunnerCommandType::RequestSeatTurn;
  state->continuationCommand.sessionId = state->tableId;
  state->continuationCommand.targetPhase = Phase::Planning;
  state->continuationCommand.targetSeatId = "seat-participant";

  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  runner.grantContinuation(*state, BudgetLimitKind::MaxTotalTokens);
  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 1);
  QCOMPARE(gateway.requests.first().seatId, QString("seat-participant"));
  QCOMPARE(state->usedTokens, 100);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Planning));
  QCOMPARE(static_cast<int>(state->continuationCommand.commandType),
           static_cast<int>(RunnerCommandType::None));

  runner.resumeSession(*state);
  QCOMPARE(gateway.requests.size(), 1);
}

void AsyncTests::convergencePromptsPreserveArtifactAuthority() {
  auto state = makeSession(Phase::Execution);
  SeatConfig leadExecution = state->seats.first();
  leadExecution.seatId = "seat-execution";
  leadExecution.displayName = "Execution";
  leadExecution.role = Role::LeadExecutioner;
  state->seats.append(leadExecution);
  SeatConfig leadQuality = leadExecution;
  leadQuality.seatId = "seat-quality";
  leadQuality.displayName = "Quality Control";
  leadQuality.role = Role::LeadQualityControl;
  state->seats.append(leadQuality);

  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  WorkflowCommand turn;
  turn.commandType = RunnerCommandType::RequestSeatTurn;
  turn.sessionId = state->tableId;
  turn.targetPhase = state->phase;
  turn.targetSeatId = "seat-participant";
  runner.executeCommand(*state, turn);
  QVERIFY(gateway.requests.last().prompt.value("instruction").toString().contains(
      "Do not present an authoritative artifact"));
  gateway.responseReady(
      responseFor(gateway.requests.last(), {}, "One new constraint only."));
  QVERIFY(state->artifacts.isEmpty());
  runner.discardSession(state->tableId);

  turn.targetSeatId = leadExecution.seatId;
  runner.executeCommand(*state, turn);
  const QString executionPrompt =
      gateway.requests.last().prompt.value("instruction").toString();
  QVERIFY(executionPrompt.contains("patch only the unresolved corrections"));
  QVERIFY(executionPrompt.contains("exact numbered-step"));

  state->phase = Phase::QualityControl;
  turn.targetPhase = state->phase;
  turn.targetSeatId = leadQuality.seatId;
  runner.executeCommand(*state, turn);
  const QString qualityPrompt =
      gateway.requests.last().prompt.value("instruction").toString();
  QVERIFY(qualityPrompt.contains("Blocking correctness issues:"));
  QVERIFY(qualityPrompt.contains("Resolved findings:"));
  QVERIFY(qualityPrompt.contains("Optional wording or style improvements"));

  WorkflowCommand decision;
  decision.commandType = RunnerCommandType::RequestDecision;
  decision.sessionId = state->tableId;
  decision.targetPhase = state->phase;
  decision.targetSeatId = state->finalDecisionMakerSeatId;
  decision.payload.insert("mode", "arbitration");
  runner.executeCommand(*state, decision);
  const QString decisionPrompt =
      gateway.requests.last().prompt.value("instruction").toString();
  QVERIFY(decisionPrompt.contains("list only those unresolved corrections"));
  QVERIFY(decisionPrompt.contains("Optional wording or style improvements must not trigger another loop"));
  runner.discardSession(state->tableId);
}

void AsyncTests::convergenceFixtureCompletesAfterOneTargetedRevision() {
  auto state = makeSession(Phase::Planning);
  state->title = "Three-step school supply plan";

  SeatConfig planner = state->seats.first();
  planner.seatId = "seat-planner";
  planner.displayName = "Planner";
  planner.role = Role::LeadPlanner;
  SeatConfig execution = planner;
  execution.seatId = "seat-execution";
  execution.displayName = "Execution";
  execution.role = Role::LeadExecutioner;
  SeatConfig quality = planner;
  quality.seatId = "seat-quality";
  quality.displayName = "Quality Control";
  quality.role = Role::LeadQualityControl;
  state->seats.insert(1, planner);
  state->seats.insert(2, execution);
  state->seats.insert(3, quality);

  TranscriptEntry objective;
  objective.entryId = "objective";
  objective.tableId = state->tableId;
  objective.phase = Phase::Idle;
  objective.round = 1;
  objective.isUser = true;
  objective.speakerName = "You";
  objective.content = "Produce a plan with headings Steps, Risks, and Acceptance checks; exactly 3 steps, 2 risks, and 2 acceptance checks; no more than 90 words; do not claim guaranteed outcomes.";
  state->transcript.append(objective);

  EventBus eventBus;
  WorkflowEngine workflow;
  FakeProviderGateway gateway;
  BudgetManager budget;
  ArtifactManager artifacts;
  SessionRunner runner(&eventBus, &workflow, &gateway, &budget, &artifacts,
                       [state](const QString &tableId) {
                         return tableId == state->tableId
                                    ? state
                                    : std::shared_ptr<SessionState>{};
                       });

  const auto answerPending = [&](const QString &content,
                                 const QString &outcome = QString{}) {
    const qsizetype previousRequestCount = gateway.requests.size();
    runner.resumeSession(*state);
    QCOMPARE(gateway.requests.size(), previousRequestCount + 1);
    gateway.responseReady(responseFor(gateway.requests.last(), outcome, content));
  };

  WorkflowCommand startExecution;
  startExecution.commandType = RunnerCommandType::StartPhase;
  startExecution.sessionId = state->tableId;
  startExecution.targetPhase = Phase::Execution;
  runner.executeCommand(*state, startExecution);

  answerPending("Use simple verbs and avoid guarantees.");
  answerPending("Keep the result under 90 words.");
  answerPending("Check every requested count together.");
  QCOMPARE(gateway.requests.last().seatId, QString("seat-quality"));
  QVERIFY(state->artifacts.isEmpty());

  const QString draft =
      "# Steps\n1. List supplies.\n2. Pack supplies.\n"
      "# Risks\n1. A needed item may be unavailable.\n2. A label may detach.\n"
      "# Acceptance checks\n1. Confirm the bag is labeled.";
  answerPending(draft);
  QCOMPARE(gateway.requests.last().seatId, QString("seat-execution"));
  QCOMPARE(static_cast<int>(gateway.requests.last().phase),
           static_cast<int>(Phase::Execution));
  QCOMPARE(state->transcript.last().content, draft);
  QCOMPARE(state->artifacts.size(), 1);
  const QString firstVersionId = state->currentArtifactVersionId;

  answerPending("The draft is missing one step and one acceptance check.");
  answerPending("No additional blocking issue.");
  answerPending("The draft can be patched without replacement.");
  answerPending(
      "Blocking correctness issues:\n"
      "1. Steps has 2 items instead of 3.\n"
      "2. Acceptance checks has 1 item instead of 2.\n"
      "Optional improvements: None.\n"
      "Open findings: item counts.\n"
      "Resolved findings: headings and risks are correct.");
  answerPending("Patch only the two item-count findings.\nFINAL_RULING: REVISE",
                "Revise");
  QCOMPARE(state->execQcLoopCount, 1);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Execution));

  answerPending("No new constraint.");
  answerPending("Preserve the existing draft and patch the counts.");
  answerPending("No new blocking issue.");
  const QString revised =
      "# Steps\n"
      "1. List required supplies.\n"
      "2. Pack each item.\n"
      "3. Label the bag.\n"
      "# Risks\n"
      "1. A needed item may be unavailable.\n"
      "2. A label may detach.\n"
      "# Acceptance checks\n"
      "1. Confirm all listed items are packed.\n"
      "2. Confirm the owner can read the label.";
  answerPending(revised);
  QCOMPARE(state->artifacts.size(), 2);
  QCOMPARE(state->artifacts.last().parentVersionId, firstVersionId);

  answerPending("Optional improvement: shorten one verb.");
  answerPending("No new blocking issue.");
  answerPending("The targeted count corrections are present.");
  const QString acceptedReview =
      "Blocking correctness issues: None\n"
      "Optional improvements: shorten one verb if desired.\n"
      "Open findings: None\n"
      "Resolved findings: headings, counts, word limit, and prohibited claims checked.";
  answerPending(acceptedReview);
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Present));
  QCOMPARE(state->execQcLoopCount, 1);

  answerPending("The corrected artifact satisfies every blocking requirement.\nFINAL_RULING: APPROVE",
                "Approve");
  QCOMPARE(static_cast<int>(state->phase), static_cast<int>(Phase::Completed));
  QCOMPARE(state->execQcLoopCount, 1);
  QCOMPARE(gateway.requests.size(), 18);

  QFile artifactFile(state->artifacts.last().filePath);
  QVERIFY(artifactFile.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString finalArtifact = QString::fromUtf8(artifactFile.readAll());
  QCOMPARE(finalArtifact, revised);
  QVERIFY(finalArtifact.contains("# Steps"));
  QVERIFY(finalArtifact.contains("# Risks"));
  QVERIFY(finalArtifact.contains("# Acceptance checks"));
  QVERIFY(!finalArtifact.contains("guarantee", Qt::CaseInsensitive));

  const auto numberedCount = [&](const QString &heading,
                                 const QString &nextHeading) {
    const qsizetype start = finalArtifact.indexOf(heading) + heading.size();
    const qsizetype end = nextHeading.isEmpty()
        ? finalArtifact.size()
        : finalArtifact.indexOf(nextHeading, start);
    const QString section = finalArtifact.mid(start, end - start);
    return section.count(QRegularExpression("(?m)^\\d+\\."));
  };
  QCOMPARE(numberedCount("# Steps", "# Risks"), 3);
  QCOMPARE(numberedCount("# Risks", "# Acceptance checks"), 2);
  QCOMPARE(numberedCount("# Acceptance checks", {}), 2);
  const int wordCount = finalArtifact.split(
      QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
  QVERIFY(wordCount <= 90);

  for (const auto &artifact : state->artifacts) {
    QFile::remove(artifact.filePath);
  }
  runner.discardSession(state->tableId);
}

QTEST_GUILESS_MAIN(AsyncTests)

#include "test_async.moc"
