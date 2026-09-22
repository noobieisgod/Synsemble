#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace amt {
// Presentation boundary only: never rewrite the stored transcript or provider context.
inline QString visibleText(QString text, const QStringList &secrets = {})
{
    if (text.contains(QLatin1Char('{')) || text.contains(QLatin1Char('['))) {
        // Also reject fenced, embedded, or malformed provider envelopes before display/copy.
        static const QRegularExpression privateField(
            R"json("(?:output|candidates|choices|thinking|reasoning|reasoning_content|signature|thoughtSignature|authorization|api_key)"\s*:)json",
            QRegularExpression::CaseInsensitiveOption);
        if (privateField.match(text).hasMatch()) return QStringLiteral("[Provider payload omitted]");
        const auto document = QJsonDocument::fromJson(text.toUtf8());
        const auto object = document.object();
        for (const auto &field : {"output", "candidates", "thinking", "reasoning", "signature", "authorization"}) {
            if (object.contains(QLatin1String(field))) return QStringLiteral("[Provider payload omitted]");
        }
        if (object.value("content").isArray()) return QStringLiteral("[Provider payload omitted]");
    }
    static const QRegularExpression hidden(
        R"(<(think|thinking|reasoning|signature)\b[^>]*>[\s\S]*?(?:</\1\s*>|$))",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression authentication(
        R"((?:authorization|x-api-key|api[_-]?key)\s*[:=]\s*[^\r\n]+)",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tokens(R"(\b(?:sk-[A-Za-z0-9_-]{8,}|AIza[A-Za-z0-9_-]{20,}|Bearer\s+\S+))",
        QRegularExpression::CaseInsensitiveOption);
    if (text.contains(QLatin1Char('<'))) text.replace(hidden, "[Private content omitted]");
    if (text.contains(QLatin1Char(':')) || text.contains(QLatin1Char('=')))
        text.replace(authentication, "[Credential omitted]");
    if (text.contains("sk-", Qt::CaseInsensitive) || text.contains("AIza", Qt::CaseInsensitive)
        || text.contains("Bearer", Qt::CaseInsensitive)) text.replace(tokens, "[Credential omitted]");
    for (const auto &secret : secrets) {
        if (!secret.isEmpty()) text.replace(secret, "[Credential omitted]");
    }
    return text;
}
}
