#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>

#include "domain/models.h"

namespace amt {

class DatabaseManager final : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(QObject *parent = nullptr);
    DatabaseManager(const QString &databasePathOverride,
                    const QString &connectionNameOverride,
                    QObject *parent = nullptr);
    ~DatabaseManager() override;

    bool initialize();
    QVector<SessionState> loadTables(bool *success = nullptr);
    bool saveTable(const SessionState &state);
    bool deleteTable(const QString &tableId);

private:
    bool createSchema();
    bool meetingTablesColumnExists(const QString &columnName) const;
    bool transcriptEntriesColumnExists(const QString &columnName) const;
    bool tableRowExists(const QString &tableId) const;
    int rowCount(const QString &tableName, const QString &tableId = {}) const;
    void logDatabaseFileState(const QString &context) const;
    void logTableRowCounts(const QString &context, const QString &tableId) const;
    bool loadTranscript(SessionState &state) const;
    bool loadLog(SessionState &state) const;
    bool loadArtifacts(SessionState &state) const;
    QString databasePath() const;
    QString connectionName() const;

    struct PersistedChildIds {
        QSet<QString> transcript;
        QSet<QString> log;
        QSet<QString> artifacts;
    };

    QSqlDatabase m_db;
    QHash<QString, PersistedChildIds> m_persistedChildIds;
    QString m_databasePathOverride;
    QString m_connectionNameOverride;
};

} // namespace amt
