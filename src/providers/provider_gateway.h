#pragma once

#include <QObject>

#ifdef AMT_TESTING
#include <functional>

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QUrl>
#include <QSslError>
#endif

#include "domain/models.h"

namespace amt {

class CredentialStore;

enum class ProviderDeliveryOutcome {
    Succeeded,
    DefiniteFailure,
    OutcomeUnknown,
    Cancelled
};

struct ProviderRequest {
    QString requestId;
    QString sessionId;
    QString seatId;
    ProviderKind provider = ProviderKind::OpenAI;
    QString model;
    Phase phase = Phase::Idle;
    QJsonObject prompt;
    QString apiKey;
    quint64 runGeneration = 0;
};

struct ProviderResponse {
    QString requestId;
    QString sessionId;
    QString seatId;
    bool success = true;
    ProviderDeliveryOutcome deliveryOutcome = ProviderDeliveryOutcome::Succeeded;
    QString content;
    bool skipped = false;
    QString decisionOutcome;
    bool multipleDecisionRulings = false;
    QString modelUsed;
    int inputTokens = 0;
    int outputTokens = 0;
    int cachedTokens = 0;
    int reasoningTokens = 0;
    int usedTokens = 0;
    bool usageReported = false;
    bool usageEstimated = false;
    QString errorMessage;
    QJsonObject attachmentProviderHandles;
    quint64 runGeneration = 0;
};

#ifdef AMT_TESTING
struct ProviderTestRequest {
    QUrl url;
    QByteArray method;
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> headers;
};

struct ProviderTestNetworkResult {
    int statusCode = 0;
    QByteArray body;
    QString transportError;
    int networkErrorCode = 0;
    int sslErrorCount = 0;
    bool cancelled = false;
    bool requestSent = true;
    QList<QSslError> tlsErrors;
    QList<QPair<QByteArray, QByteArray>> headers;
};

using ProviderTestTransport = std::function<ProviderTestNetworkResult(const ProviderTestRequest &)>;
#endif

class ProviderGateway : public QObject
{
    Q_OBJECT

public:
    explicit ProviderGateway(QObject *parent = nullptr);
    ~ProviderGateway() override;
    void setCredentialStore(CredentialStore *credentialStore);

    virtual void sendAsync(const ProviderRequest &request);

#ifdef AMT_TESTING
    static ProviderResponse processForTesting(const ProviderRequest &request,
                                               const ProviderTestTransport &transport);
    static ProviderResponse localRequestForTesting(const QUrl &url);
    static bool shouldRetryForTesting(const ProviderResponse &response);
#endif

signals:
    void responseReady(const amt::ProviderResponse &response);

private:
    CredentialStore *m_credentialStore = nullptr;
};

} // namespace amt

Q_DECLARE_METATYPE(amt::ProviderRequest)
Q_DECLARE_METATYPE(amt::ProviderResponse)
