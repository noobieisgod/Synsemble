#include "services/artifact_manager.h"

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

#include "core/logging.h"

namespace amt {

ArtifactManager::ArtifactManager(QObject *parent)
    : QObject(parent)
{
}

ArtifactVersion ArtifactManager::createVersion(SessionState &state, Phase phase, int round, const QString &summary, const QString &content)
{
    m_lastError.clear();

    ArtifactVersion version;
    version.versionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    version.parentVersionId = state.currentArtifactVersionId;
    version.createdByPhase = phase;
    version.createdByRound = round;
    version.createdAt = QDateTime::currentDateTimeUtc();
    version.summary = summary;
    version.filePath = artifactContentPath(version.versionId);

    QDir().mkpath(QFileInfo(version.filePath).absolutePath());
    QSaveFile file(version.filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_lastError = "Failed to create the local artifact file.";
        return {};
    }

    const QByteArray bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        m_lastError = "Failed to write the local artifact file.";
        return {};
    }

    state.currentArtifactVersionId = version.versionId;
    state.artifacts.append(version);
    const QFileInfo info(version.filePath);
    qCDebug(diagnosticsLog).noquote() << QString("Artifact file saved: exists=%1 size=%2")
                             .arg(info.exists() ? "true" : "false",
                                  QString::number(info.exists() ? info.size() : 0));
    return version;
}

QString ArtifactManager::artifactContentPath(const QString &versionId) const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/artifacts/" + versionId + ".md";
}

QString ArtifactManager::lastError() const
{
    return m_lastError;
}

}
