#pragma once

#include <functional>
#include <memory>
#include <QObject>
#include <QHash>
#include <QPointer>

#include "core/event_bus.h"
#include "core/workflow_engine.h"
#include "providers/provider_gateway.h"
#include "services/artifact_manager.h"
#include "services/budget_manager.h"

class QTimer;

namespace amt {

class SessionRunner final : public QObject
{
    Q_OBJECT

public:
    struct ContinuationAllowance {
        BudgetLimitKind kind = BudgetLimitKind::None;
    };

    using SessionResolver = std::function<std::shared_ptr<SessionState>(const QString &)>;

    SessionRunner(EventBus *eventBus,
                  WorkflowEngine *engine,
                  ProviderGateway *providerGateway,
                  BudgetManager *budgetManager,
                  ArtifactManager *artifactManager,
                  SessionResolver sessionResolver,
                  QObject *parent = nullptr);

    void startSession(SessionState &state);
    void setCheckpoint(std::function<bool(const SessionState &)> checkpoint) { m_checkpoint = std::move(checkpoint); }
    void requestPause(SessionState &state);
    void resumeSession(SessionState &state);
    void stopSession(SessionState &state, const QString &reason = {});
    void grantContinuation(SessionState &state, BudgetLimitKind kind);
    void discardSession(const QString &sessionId);
    void executeCommand(SessionState &state, const WorkflowCommand &command);

signals:
    void sessionStateChanged(const amt::SessionState &state);
    void continuationRequested(const QString &tableId, const QString &reason, int limitKind);

private:
    struct PendingRequestContext {
        QString sessionId;
        QString seatId;
        QString mode;
        Phase phase = Phase::Idle;
        int round = 0;
        int reservedTokens = 0;
        quint64 runGeneration = 0;
    };

    void handleElapsedTick();
    void updateElapsedTimerState();
    void onProviderResponse(const ProviderResponse &response);
    WorkflowEvent makeEvent(const SessionState &state, EventType type, const QJsonObject &payload = {}) const;
    SeatConfig seatById(const SessionState &state, const QString &seatId) const;
    bool dispatchProviderRequest(SessionState &state,
                                 const SeatConfig &seat,
                                 const QJsonObject &prompt,
                                 const QString &blockedSummary,
                                 const WorkflowCommand *resumeCommand = nullptr,
                                 bool enforceBudget = true);
    void queueNextCommand(SessionState &state, const WorkflowCommand &command);
    QString latestUserPrompt(const SessionState &state) const;
    int reservedTokensInFlight(const QString &sessionId) const;
    void removePendingRequests(const QString &sessionId);
    void appendLog(SessionState &state, LogEventType type, const QString &actorSeatId, const QString &actorName, const QString &summary);
    void appendTranscript(SessionState &state, const QString &seatId, const QString &actorName, const QString &content, bool isDecision = false, bool isUser = false);
    void emitAndHandle(SessionState &state, const WorkflowEvent &event);
    void finalizeResearchBatchIfReady(SessionState &state);
    void markContinuationPending(SessionState &state, const BudgetStatus &status);
    void enterContinuationPause(SessionState &state, const WorkflowCommand &resumeCommand);
    void clearContinuationState(SessionState &state);
    BudgetStatus budgetStatusFor(const SessionState &state, int reservedTokensInFlight = 0) const;

    EventBus *m_eventBus;
    WorkflowEngine *m_engine;
    ProviderGateway *m_providerGateway;
    BudgetManager *m_budgetManager;
    ArtifactManager *m_artifactManager;
    SessionResolver m_sessionResolver;
    std::function<bool(const SessionState &)> m_checkpoint;
    QHash<QString, PendingRequestContext> m_pendingRequests;
    QHash<QString, int> m_researchRequestsBySession;
    QHash<QString, int> m_researchFailuresBySession;
    QHash<QString, WorkflowCommand> m_delayedCommands;
    QHash<QString, QPointer<QTimer>> m_delayTimers;
    QHash<QString, int> m_lastPersistedElapsedSeconds;
    QHash<QString, ContinuationAllowance> m_continuationAllowances;
    QHash<QString, quint64> m_runGenerations;
    QTimer *m_elapsedTimer = nullptr;
};

} // namespace amt
