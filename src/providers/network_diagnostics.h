#pragma once

#include <QSslError>
#include <QStringList>

namespace amt {
// Fixed categories only. Never expose certificate contents or transport error strings.
inline QString tlsFailureCategory(const QList<QSslError> &errors)
{
    QStringList categories;
    for (const auto &error : errors) {
        QString category;
        switch (error.error()) {
        case QSslError::UnableToGetIssuerCertificate:
        case QSslError::UnableToGetLocalIssuerCertificate: category = "missing issuer certificate"; break;
        case QSslError::CertificateUntrusted:
        case QSslError::UnableToVerifyFirstCertificate: category = "untrusted certificate chain"; break;
        case QSslError::SelfSignedCertificate:
        case QSslError::SelfSignedCertificateInChain: category = "self-signed certificate"; break;
        case QSslError::CertificateExpired: category = "certificate expired"; break;
        case QSslError::CertificateNotYetValid: category = "certificate not yet valid"; break;
        case QSslError::HostNameMismatch: category = "certificate hostname mismatch"; break;
        default: category = "certificate verification failure"; break;
        }
        if (!categories.contains(category)) categories.append(category);
    }
    return categories.join(", ");
}
}
