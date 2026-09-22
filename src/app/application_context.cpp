#include "app/application_context.h"

#include <algorithm>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#include "core/logging.h"
#include "core/startup_timeline.h"

#ifdef QT_WIDGETS_LIB
#include <QApplication>
#endif

namespace amt {

ApplicationContext::ApplicationContext(QObject *parent)
    : QObject(parent),
      m_modelCatalogManager(&m_credentialStore, this),
      m_sessionRunner(&m_eventBus,
                      &m_workflowEngine,
                      &m_providerGateway,
                      &m_budgetManager,
                      &m_artifactManager,
                      [this](const QString &tableId) { return tableHandle(tableId); },
                      this)
{
    m_providerGateway.setCredentialStore(&m_credentialStore);
    m_sessionRunner.setCheckpoint([this](const SessionState &state) {
        return m_databaseManager.saveTable(state);
    });
    StartupTimeline::instance().mark(StartupStage::ApplicationContextConstruction);
}

bool ApplicationContext::initialize()
{
    QSettings settings;
    m_appSettings.globalBudgetDefaults.maxTokensPerPhase = settings.value("budget/maxTokensPerPhase", m_appSettings.globalBudgetDefaults.maxTokensPerPhase).toInt();
    m_appSettings.globalBudgetDefaults.maxTotalTokens = settings.value("budget/maxTotalTokens", m_appSettings.globalBudgetDefaults.maxTotalTokens).toInt();
    m_appSettings.globalBudgetDefaults.maxTotalCost = settings.value("budget/maxTotalCost", m_appSettings.globalBudgetDefaults.maxTotalCost).toDouble();
    m_appSettings.globalBudgetDefaults.maxRounds = settings.value("budget/maxRounds", m_appSettings.globalBudgetDefaults.maxRounds).toInt();
    m_appSettings.globalBudgetDefaults.maxExecQcLoops = settings.value("budget/maxExecQcLoops", m_appSettings.globalBudgetDefaults.maxExecQcLoops).toInt();
    m_appSettings.globalBudgetDefaults.maxPhaseSeconds = settings.value("budget/maxPhaseSeconds", m_appSettings.globalBudgetDefaults.maxPhaseSeconds).toInt();
    m_appSettings.globalBudgetDefaults.maxSessionSeconds = settings.value("budget/maxSessionSeconds", m_appSettings.globalBudgetDefaults.maxSessionSeconds).toInt();
    m_appSettings.theme = themeModeFromString(settings.value("appearance/theme", toString(m_appSettings.theme)).toString());
    m_appSettings.colorTheme = settings.value("appearance/colorTheme", m_appSettings.colorTheme).toString();
    m_appSettings.fontStyle = settings.value("appearance/fontStyle", m_appSettings.fontStyle).toString();

    if (!m_databaseManager.initialize()) {
        return false;
    }

    bool restored = false;
    const auto loadedTables = m_databaseManager.loadTables(&restored);
    if (!restored) {
        // Never treat a failed restore as an empty database or clean up its files.
        return false;
    }
    m_tables.clear();
    for (const auto &table : loadedTables) {
        auto handle = std::make_shared<SessionState>(table);
        applyEffectiveBudgetPolicy(*handle);
        bool restoreWarningAdded = validateRestoredArtifacts(*handle);
        const bool interruptedRequest = handle->continuationCommand.payload.value("requestInFlight").toBool();
        if (interruptedRequest || isRunningPhase(handle->phase)) {
            const bool safePendingCommand = !interruptedRequest
                && handle->continuationCommand.commandType != RunnerCommandType::None
                && handle->continuationCommand.sessionId == handle->tableId;
            handle->pausedResumePhase = handle->phase;
            handle->phase = Phase::Paused;
            handle->paused = true;
            handle->waitingForNextTurn = false;
            handle->pendingResearchResponses = 0;
            if (safePendingCommand) {
                handle->continuationReason = "The app restarted. Resume the pending operation when ready.";
            } else {
                handle->continuationCommand = {};
                handle->continuationCommand.payload.insert("outcomeUnknown", true);
                handle->continuationPending = false;
                handle->continuationReason = "The app closed during provider work. Its outcome is unknown and cannot be replayed.";
            }
            restoreWarningAdded = true;
        }
        m_tables.append(handle);
        if (restoreWarningAdded) {
            save(*handle);
        }
    }
    cleanupUnownedAttachmentFiles();
    qCDebug(diagnosticsLog).noquote() << QString("Persistence restore: loaded tables=%1").arg(m_tables.size());
    applyTheme();
    return true;
}

QVector<ApplicationContext::SessionHandle> &ApplicationContext::tables()
{
    return m_tables;
}

const QVector<ApplicationContext::SessionHandle> &ApplicationContext::tables() const
{
    return m_tables;
}

SessionState ApplicationContext::createSampleTable() const
{
    SessionState state;
    state.tableId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state.title = "New table";
    state.updatedAt = QDateTime::currentDateTimeUtc();
    state.phase = Phase::Idle;
    state.round = 1;
    state.logVisible = true;
    state.budgetOverrides = m_appSettings.globalBudgetDefaults;
    state.budgetPolicy = m_appSettings.globalBudgetDefaults;
    state.finalDecisionMakerSeatId.clear();
    return state;
}

bool ApplicationContext::save(const SessionState &state)
{
    SessionState updated = state;
    updated.updatedAt = QDateTime::currentDateTimeUtc();
    const bool saved = m_databaseManager.saveTable(updated);
    if (!saved) {
        qWarning() << "Persistence save failed";
        return false;
    }

    bool found = false;
    for (auto &table : m_tables) {
        if (table && table->tableId == updated.tableId) {
            *table = updated;
            found = true;
            break;
        }
    }
    if (!found) {
        m_tables.append(std::make_shared<SessionState>(updated));
    }
    return true;
}

bool ApplicationContext::saveExisting(const QString &tableId)
{
    const auto handle = tableHandle(tableId);
    if (!handle) {
        return false;
    }

    const QDateTime previousUpdatedAt = handle->updatedAt;
    handle->updatedAt = QDateTime::currentDateTimeUtc();
    if (m_databaseManager.saveTable(*handle)) {
        return true;
    }

    handle->updatedAt = previousUpdatedAt;
    qWarning() << "Persistence save failed";
    return false;
}

bool ApplicationContext::removeTable(const QString &tableId)
{
    for (const auto &table : m_tables) {
        if (table && table->tableId == tableId) {
            QStringList attachmentPaths;
            for (const auto &attachment : table->attachments) {
                attachmentPaths.append(attachment.filePath);
            }
            if (!m_databaseManager.deleteTable(tableId)) {
                return false;
            }
            const auto it = std::remove_if(m_tables.begin(), m_tables.end(), [&tableId](const SessionHandle &state) {
                return state && state->tableId == tableId;
            });
            m_tables.erase(it, m_tables.end());
            for (const auto &filePath : attachmentPaths) {
                cleanupAttachmentFileIfUnreferenced(filePath);
            }
            return true;
        }
    }
    return false;
}

bool ApplicationContext::cleanupAttachmentFileIfUnreferenced(const QString &filePath) const
{
    const QFileInfo fileInfo(filePath);
    const QString canonicalFilePath = fileInfo.canonicalFilePath();
    const QString attachmentRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/attachments";
    const QString canonicalRoot = QFileInfo(attachmentRoot).canonicalFilePath();
    if (canonicalFilePath.isEmpty() || canonicalRoot.isEmpty()) {
        return false;
    }

    const QString relativePath = QDir(canonicalRoot).relativeFilePath(canonicalFilePath);
    if (relativePath == ".."
        || relativePath.startsWith("../")
        || QDir::isAbsolutePath(relativePath)) {
        return false;
    }

    for (const auto &table : m_tables) {
        if (!table) {
            continue;
        }
        for (const auto &attachment : table->attachments) {
            if (QFileInfo(attachment.filePath).canonicalFilePath() == canonicalFilePath) {
                return false;
            }
        }
    }
    return QFile::remove(canonicalFilePath);
}

void ApplicationContext::cleanupUnownedAttachmentFiles() const
{
    const QString attachmentRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/attachments";
    QDir root(attachmentRoot);
    if (!root.exists()) {
        return;
    }

    const QString canonicalRoot = QFileInfo(attachmentRoot).canonicalFilePath();
    if (canonicalRoot.isEmpty()) {
        return;
    }

    QSet<QString> referenced;
    for (const auto &table : m_tables) {
        if (!table) {
            continue;
        }
        for (const auto &attachment : table->attachments) {
            const QString canonicalPath = QFileInfo(attachment.filePath).canonicalFilePath();
            if (!canonicalPath.isEmpty()) {
                referenced.insert(canonicalPath);
            }
        }
    }

    int cleanupFailures = 0;
    const QFileInfoList entries = root.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString canonicalPath = entry.canonicalFilePath();
        const bool isOwned = !canonicalPath.isEmpty() && referenced.contains(canonicalPath);
        if (isOwned) {
            continue;
        }
        if (!QFile::remove(entry.absoluteFilePath())) {
            cleanupFailures += 1;
        }
    }
    if (cleanupFailures > 0) {
        qWarning().noquote() << QString("Attachment startup cleanup failures=%1").arg(cleanupFailures);
    }
}

ApplicationContext::SessionHandle ApplicationContext::tableHandle(const QString &tableId) const
{
    for (const auto &table : m_tables) {
        if (table && table->tableId == tableId) {
            return table;
        }
    }
    return {};
}

bool ApplicationContext::validateRestoredArtifacts(SessionState &state) const
{
    bool addedWarning = false;
    for (const auto &artifact : state.artifacts) {
        if (artifact.filePath.isEmpty() || QFileInfo::exists(artifact.filePath)) {
            continue;
        }

        const QString warning = QString("Artifact file is missing after restore. Artifact metadata remains available. Version: %1")
                                    .arg(artifact.versionId);
        const bool alreadyLogged = std::any_of(state.log.cbegin(), state.log.cend(), [&](const LogEvent &event) {
            return event.summary == warning;
        });
        if (alreadyLogged) {
            continue;
        }

        LogEvent event;
        event.logId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        event.tableId = state.tableId;
        event.type = LogEventType::ProviderCallFailed;
        event.phase = state.phase;
        event.round = state.round;
        event.timestamp = QDateTime::currentDateTimeUtc();
        event.summary = warning;
        state.log.append(event);
        addedWarning = true;
        qWarning() << "Persistence restore found missing artifact content";
    }
    return addedWarning;
}

void ApplicationContext::saveAppSettings() const
{
    QSettings settings;
    settings.setValue("budget/maxTokensPerPhase", m_appSettings.globalBudgetDefaults.maxTokensPerPhase);
    settings.setValue("budget/maxTotalTokens", m_appSettings.globalBudgetDefaults.maxTotalTokens);
    settings.setValue("budget/maxTotalCost", m_appSettings.globalBudgetDefaults.maxTotalCost);
    settings.setValue("budget/maxRounds", m_appSettings.globalBudgetDefaults.maxRounds);
    settings.setValue("budget/maxExecQcLoops", m_appSettings.globalBudgetDefaults.maxExecQcLoops);
    settings.setValue("budget/maxPhaseSeconds", m_appSettings.globalBudgetDefaults.maxPhaseSeconds);
    settings.setValue("budget/maxSessionSeconds", m_appSettings.globalBudgetDefaults.maxSessionSeconds);
    settings.setValue("appearance/theme", toString(m_appSettings.theme));
    settings.setValue("appearance/colorTheme", m_appSettings.colorTheme);
    settings.setValue("appearance/fontStyle", m_appSettings.fontStyle);
}

void ApplicationContext::applyEffectiveBudgetPolicy(SessionState &state) const
{
    state.budgetPolicy = state.useBudgetOverrides ? state.budgetOverrides : m_appSettings.globalBudgetDefaults;
}

SessionRunner *ApplicationContext::sessionRunner()
{
    return &m_sessionRunner;
}

BudgetManager *ApplicationContext::budgetManager()
{
    return &m_budgetManager;
}

const BudgetManager *ApplicationContext::budgetManager() const
{
    return &m_budgetManager;
}

CredentialStore *ApplicationContext::credentialStore()
{
    return &m_credentialStore;
}

const CredentialStore *ApplicationContext::credentialStore() const
{
    return &m_credentialStore;
}

UploadManager *ApplicationContext::uploadManager()
{
    return &m_uploadManager;
}

const UploadManager *ApplicationContext::uploadManager() const
{
    return &m_uploadManager;
}

ModelCatalogManager *ApplicationContext::modelCatalogManager()
{
    return &m_modelCatalogManager;
}

const ModelCatalogManager *ApplicationContext::modelCatalogManager() const
{
    return &m_modelCatalogManager;
}

AppSettings &ApplicationContext::appSettings()
{
    return m_appSettings;
}

const AppSettings &ApplicationContext::appSettings() const
{
    return m_appSettings;
}

void ApplicationContext::applyTheme() const
{
#ifndef QT_WIDGETS_LIB
    return;
#else
    if (!qApp) {
        return;
    }

    if (m_appSettings.theme == ThemeMode::Dark) {
        qApp->setStyleSheet(
            "QMainWindow, QDialog, QWidget { background-color: #1d2128; color: #e6ebf2; }"
            "QLabel { color: #e6ebf2; }"
            "QLineEdit, QPlainTextEdit, QTextEdit, QListWidget, QComboBox, QSpinBox, QDoubleSpinBox, QTabWidget::pane {"
            "  background-color: #252b34; color: #eef3f8; border: 1px solid #48515d; }"
            "QPushButton { background-color: #2d3641; color: #eef3f8; border: 1px solid #54606d; padding: 5px 10px; }"
            "QPushButton:hover { background-color: #384453; }"
            "QCheckBox { color: #e6ebf2; }"
            "QHeaderView::section { background-color: #252b34; color: #eef3f8; border: 1px solid #48515d; }"
            "#leftSidebar, #rightSidebar { border: 1px solid #48515d; background-color: #20252d; }"
            "#centerWorkspace { border: 1px solid #48515d; background-color: #1b2027; }"
            "#topStatusCard { background-color: #252b34; border: 1px solid #48515d; border-radius: 10px; }"
            "#topStatusTitle { color: #97a6b8; font-size: 11px; }"
            "#topStatusValue { color: #eef3f8; font-size: 15px; font-weight: 600; }"
            "#meetingTableWidget { border: 1px solid #48515d; border-radius: 12px; background-color: #212731; }");
        return;
    }

    qApp->setStyleSheet(
        "QMainWindow, QDialog, QWidget { background-color: #f8f5ef; color: #342a1d; }"
            "QLineEdit, QPlainTextEdit, QTextEdit, QListWidget, QComboBox, QSpinBox, QDoubleSpinBox, QTabWidget::pane {"
        "  background-color: #fbf8f2; color: #342a1d; border: 1px solid #d8cdbc; }"
        "QPushButton { background-color: #f1e7d8; color: #342a1d; border: 1px solid #cfbea7; padding: 5px 10px; }"
        "QPushButton:hover { background-color: #eadcc8; }"
        "#leftSidebar, #rightSidebar { border: 1px solid #d8cdbc; background-color: #fbf7f1; }"
        "#centerWorkspace { border: 1px solid #ddd0bd; background-color: #f8f3ea; }"
        "#topStatusCard { background-color: #fbf8f2; border: 1px solid #d8cdbc; border-radius: 10px; }"
        "#topStatusTitle { color: #7c6a55; font-size: 11px; }"
        "#topStatusValue { color: #342a1d; font-size: 15px; font-weight: 600; }"
        "#meetingTableWidget { border: 1px solid #d9ccb8; border-radius: 12px; background-color: #f6f0e5; }");
#endif
}

}
