#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "app/application_context.h"
#include "services/attachment_import_manager.h"

#ifdef AMT_TESTING
class ControllerTests;
#endif

namespace amt {

class MobileAppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentTableId READ currentTableId NOTIFY stateChanged)
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool attachmentImportInProgress READ attachmentImportInProgress NOTIFY attachmentImportChanged)
    Q_PROPERTY(QString attachmentImportStatus READ attachmentImportStatus NOTIFY attachmentImportChanged)

public:
    explicit MobileAppController(QObject *parent = nullptr);

    bool initialized() const;
    bool running() const;
    QString currentTableId() const;
    bool attachmentImportInProgress() const;
    QString attachmentImportStatus() const;

    Q_INVOKABLE bool initialize();
    Q_INVOKABLE void startupInitialRefreshStarted();
    Q_INVOKABLE void startupInitialRefreshCompleted();
    Q_INVOKABLE void startupTranscriptVisualStable();
    Q_INVOKABLE void startupPrimaryControlsReady();
    Q_INVOKABLE QVariantList tables() const;
    Q_INVOKABLE QVariantMap currentTable() const;
    Q_INVOKABLE QVariantList seats() const;
    Q_INVOKABLE QVariantList transcript() const;
    Q_INVOKABLE QVariantList attachments() const;
    QString fullTranscriptText() const;
    Q_INVOKABLE bool copyFullTranscript();
    Q_INVOKABLE QVariantList artifacts() const;
    Q_INVOKABLE QString artifactContent(const QString &versionId) const;
    Q_INVOKABLE QVariantList logs() const;
    Q_INVOKABLE QVariantList modelsForProvider(int providerIndex) const;
    Q_INVOKABLE QVariantList modelRefreshStatuses() const;
    Q_INVOKABLE QVariantMap settings() const;
    Q_INVOKABLE QVariantMap attachmentSafeguards() const;
    Q_INVOKABLE bool hasCredential(int providerIndex) const;
    Q_INVOKABLE QString lastError() const;

    Q_INVOKABLE void selectTable(const QString &tableId);
    Q_INVOKABLE bool createTable(const QString &title);
    Q_INVOKABLE bool duplicateCurrentTable();
    Q_INVOKABLE bool renameCurrentTable(const QString &title);
    Q_INVOKABLE bool deleteCurrentTable();
    Q_INVOKABLE bool togglePinCurrentTable();
    Q_INVOKABLE bool saveSeat(int seatIndex,
                              bool occupied,
                              const QString &displayName,
                              int providerIndex,
                              const QString &modelId,
                              int effortIndex,
                              int roleIndex,
                              const QString &color);
    Q_INVOKABLE bool sendMessage(const QString &message);
    Q_INVOKABLE bool submitTask(const QString &message);
    Q_INVOKABLE bool runOrResume();
    Q_INVOKABLE bool pauseSession();
    Q_INVOKABLE bool stopSession();
    Q_INVOKABLE bool addAttachment(const QUrl &url);
    Q_INVOKABLE bool addAttachmentForTable(const QString &tableId, const QUrl &url);
    Q_INVOKABLE bool cancelAttachmentImport();
    Q_INVOKABLE bool removeAttachment(const QString &attachmentId);
    Q_INVOKABLE bool openAttachment(const QString &attachmentId);
    Q_INVOKABLE bool saveApiKey(int providerIndex, const QString &apiKey);
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void refreshProviderModels(int providerIndex);
    Q_INVOKABLE bool acknowledgeQuickGuide();
    Q_INVOKABLE bool removeAgent(int seatIndex);
    Q_INVOKABLE void setTheme(const QString &theme);
    Q_INVOKABLE bool saveAppearance(const QString &appearance,
                                    const QString &colorTheme,
                                    const QString &fontStyle);
    Q_INVOKABLE bool saveGlobalBudget(int maxTokensPerPhase,
                                      int maxTotalTokens,
                                      int maxRounds,
                                      int maxExecQcLoops,
                                      int maxPhaseSeconds,
                                      int maxSessionSeconds);
    bool flushCurrentSession();

signals:
    void initializedChanged();
    void stateChanged();
    void tablesChanged();
    void seatsChanged();
    void transcriptChanged();
    void attachmentsChanged();
    void artifactsChanged();
    void logsChanged();
    void settingsChanged();
    void attachmentImportChanged();
    void attachmentImportFailed();
    void continuationRequested(const QString &reason);

private:
    SessionState *currentState() const;
    ApplicationContext::SessionHandle currentHandle() const;
    void selectFirstTableIfNeeded();
    bool saveAndNotify(const SessionState &state, bool tablesChanged = false);
    void schedulePersistence(const QString &tableId);
    void persistScheduledSessions();
    void notifyStateChange(const SessionState &state, bool tableListChanged);
    bool validateRunnable(const SessionState &state);
    ProviderKind providerFromIndex(int providerIndex) const;
    QVariantMap tableSummary(const SessionState &state) const;
    QVariantMap seatSummary(const SeatConfig &seat, int index) const;
    QVariantMap transcriptSummary(const TranscriptEntry &entry) const;
    QVariantMap attachmentSummary(const AttachmentRecord &attachment) const;
    QVariantMap artifactSummary(const ArtifactVersion &artifact) const;
    QVariantMap logSummary(const LogEvent &event) const;
    void handleAttachmentImportFinished(const AttachmentImportResult &result);
    void setError(const QString &error) const;
    QStringList presentationSecrets() const;
    QVariantList sanitizedRows(QVariantList rows) const;

    struct UiSnapshot {
        qsizetype transcriptCount = 0;
        qsizetype attachmentCount = 0;
        qsizetype artifactCount = 0;
        qsizetype logCount = 0;
        QString activeSeatId;
        QByteArray seatConfiguration;
    };
    UiSnapshot uiSnapshot(const SessionState &state) const;

    ApplicationContext m_context;
    AttachmentImportManager m_attachmentImportManager;
    QHash<QString, UiSnapshot> m_uiSnapshots;
    QSet<QString> m_pendingSaveIds;
    QString m_currentTableId;
    QString m_attachmentImportOperationId;
    QString m_attachmentImportTableId;
    QString m_attachmentImportStatus;
    mutable QString m_lastError;
    bool m_initialized = false;
    bool m_persistenceScheduled = false;
    bool m_attachmentImportInProgress = false;
    bool m_suppressAttachmentCancellationError = false;

#ifdef AMT_TESTING
    friend class ::ControllerTests;
#endif
};

} // namespace amt
