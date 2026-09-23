#include "agyaccount.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QStandardPaths>
#include <QProcess>
#include <QRegularExpression>
#include <QMutex>
#include <QMutexLocker>

namespace {

QString oauthTokenPath()
{
    return QDir::homePath() + QStringLiteral("/.gemini/antigravity-cli/antigravity-oauth-token");
}

AgyAccountInfo computeCurrentAccount();

} // namespace

AgyAccountInfo AgyAccount::currentAccount()
{
    // currentAccount() is called frequently (e.g. once per rendered user
    // message), and it reads + base64-decodes a JWT from disk. Cache the result
    // and invalidate only when the token file's mtime/size or the configured API
    // key changes.
    static QMutex s_mutex;
    static bool s_valid = false;
    static AgyAccountInfo s_cached;
    static qint64 s_tokenMtimeMs = 0;
    static qint64 s_tokenSize = -1;
    static QString s_apiKeyFingerprint;

    const QFileInfo tokenFi{oauthTokenPath()};
    const qint64 mtimeMs = tokenFi.exists() ? tokenFi.lastModified().toMSecsSinceEpoch() : 0;
    const qint64 size = tokenFi.exists() ? tokenFi.size() : -1;

    AgySettings *settings = AgySettings::instance();
    const QString apiKeyFingerprint = (settings && !settings->apiKey.isEmpty())
        ? QString::number(settings->apiKey.size())
        : QString();

    QMutexLocker locker(&s_mutex);
    if (s_valid && mtimeMs == s_tokenMtimeMs && size == s_tokenSize
        && apiKeyFingerprint == s_apiKeyFingerprint) {
        return s_cached;
    }

    s_cached = computeCurrentAccount();
    s_tokenMtimeMs = mtimeMs;
    s_tokenSize = size;
    s_apiKeyFingerprint = apiKeyFingerprint;
    s_valid = true;
    return s_cached;
}

namespace {

AgyAccountInfo computeCurrentAccount()
{
    AgyAccountInfo info;

    // 1. Check Antigravity CLI OAuth token
    const QString tokenPath = oauthTokenPath();
    QFile file(tokenPath);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        if (doc.isObject()) {
            const QJsonObject rootObj = doc.object();
            info.authMethod = rootObj.value(QStringLiteral("auth_method")).toString();
            if (info.authMethod.isEmpty()) {
                info.authMethod = QStringLiteral("OAuth (Antigravity)");
            }

            const QString idToken = rootObj.value(QStringLiteral("id_token")).toString();
            const QStringList parts = idToken.split(QLatin1Char('.'));
            if (parts.size() >= 2) {
                QString payloadPart = parts.at(1);
                while (payloadPart.length() % 4 != 0) {
                    payloadPart.append(QLatin1Char('='));
                }

                const QByteArray payloadBytes = QByteArray::fromBase64(
                    payloadPart.toLatin1(),
                    QByteArray::Base64UrlEncoding
                );

                const QJsonDocument payloadDoc = QJsonDocument::fromJson(payloadBytes);
                if (payloadDoc.isObject()) {
                    const QJsonObject payloadObj = payloadDoc.object();
                    info.email = payloadObj.value(QStringLiteral("email")).toString();
                    info.name = payloadObj.value(QStringLiteral("name")).toString();
                    if (!info.email.isEmpty()) {
                        info.isAuthenticated = true;
                        return info;
                    }
                }
            }
        }
    }

    // 2. Direct API Mode fallback
    auto *settings = AgySettings::instance();
    if (settings && !settings->apiKey.isEmpty()) {
        info.authMethod = QStringLiteral("Direct Gemini API");
        info.email = QStringLiteral("Gemini API Key");
        info.name = QStringLiteral("Developer Account");
        info.isAuthenticated = true;
        return info;
    }

    info.authMethod = QStringLiteral("Not logged in");
    info.isAuthenticated = false;
    return info;
}

} // namespace

QString AgyAccount::findAgyExecutable()
{
    QString agyPath = QStandardPaths::findExecutable(QStringLiteral("agy"));
    if (agyPath.isEmpty()) {
        const QString localBin = QDir::homePath() + QStringLiteral("/.local/bin/agy");
        if (QFile::exists(localBin)) {
            agyPath = localBin;
        }
    }
    return agyPath;
}

QList<AgyQuotaEntry> AgyAccount::fetchQuotaLimits()
{
    static QDateTime s_lastFetch;
    static QList<AgyQuotaEntry> s_cachedQuota;
    static QMutex s_quotaMutex;

    QMutexLocker locker(&s_quotaMutex);
    if (s_lastFetch.isValid() && s_lastFetch.secsTo(QDateTime::currentDateTime()) < 20 && !s_cachedQuota.isEmpty()) {
        return s_cachedQuota;
    }

    const QString agy = findAgyExecutable();
    if (agy.isEmpty()) {
        return s_cachedQuota;
    }

    QProcess proc;
    proc.start(agy, {QStringLiteral("--print"), QStringLiteral("/usage")});
    if (!proc.waitForFinished(3500)) {
        proc.kill();
        return s_cachedQuota;
    }

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'));

    QList<AgyQuotaEntry> entries;
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("Quota:"))) {
            continue;
        }

        // Output format is tab-separated:
        // Gemini Models\tWeekly Limit Remaining\t41%\t2026-09-24T14:51:39Z
        QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() < 4) {
            // Fallback split by 2 or more spaces
            parts = line.split(QRegularExpression(QStringLiteral("\\s{2,}")));
        }

        if (parts.size() >= 4) {
            AgyQuotaEntry entry;
            entry.modelGroup = parts.at(0).trimmed();
            entry.limitName = parts.at(1).trimmed();

            QString pctStr = parts.at(2).trimmed();
            pctStr.remove(QLatin1Char('%'));
            entry.remainingPercent = pctStr.toInt();

            entry.rawResetTime = parts.at(3).trimmed();
            entry.resetTime = QDateTime::fromString(entry.rawResetTime, Qt::ISODate);
            entries.append(entry);
        }
    }

    if (!entries.isEmpty()) {
        s_cachedQuota = entries;
        s_lastFetch = QDateTime::currentDateTime();
    }

    return s_cachedQuota;
}
