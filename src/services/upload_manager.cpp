#include "services/upload_manager.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

namespace amt {

UploadManager::UploadManager(QObject *parent)
    : QObject(parent)
{
}

AttachmentRecord UploadManager::createAttachment(const QString &filePath, QString *error) const
{
    const QFileInfo info(filePath);
    const QString hash = computeFileHash(info.absoluteFilePath(), error);
    if (hash.isEmpty()) {
        return {};
    }
    return createAttachment(info.absoluteFilePath(), hash, info.size(), error);
}

AttachmentRecord UploadManager::createAttachment(const QString &filePath,
                                                  const QString &sha256,
                                                  qint64 byteCount,
                                                  QString *error) const
{
    const QFileInfo info(filePath);
    static const QRegularExpression sha256Pattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!info.exists() || !info.isFile() || byteCount < 0 || info.size() != byteCount
        || !sha256Pattern.match(sha256).hasMatch()) {
        if (error) {
            *error = "Imported attachment verification failed.";
        }
        return {};
    }

    AttachmentRecord attachment;
    attachment.attachmentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    attachment.displayName = info.fileName();
    attachment.filePath = info.absoluteFilePath();
    attachment.fileHash = sha256;
    attachment.addedAt = QDateTime::currentDateTimeUtc();
    return attachment;
}

QString UploadManager::computeFileHash(const QString &filePath, QString *error) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QString("Failed to read attachment: %1").arg(filePath);
        }
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (error) *error = "Failed to read the complete attachment.";
        return {};
    }
    return QString::fromUtf8(hash.result().toHex());
}

} // namespace amt
