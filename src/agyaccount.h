#ifndef AGYACCOUNT_H
#define AGYACCOUNT_H

#include <QString>
#include <QList>
#include <QDateTime>

struct AgyAccountInfo {
    QString email;
    QString name;
    QString authMethod;
    bool isAuthenticated = false;

    QString displayName() const
    {
        if (!email.isEmpty()) {
            return email;
        }
        if (!name.isEmpty()) {
            return name;
        }
        return QStringLiteral("Google AI User");
    }
};

struct AgyQuotaEntry {
    QString modelGroup;
    QString limitName;
    int remainingPercent = 0;
    QDateTime resetTime;
    QString rawResetTime;
};

class AgyAccount
{
public:
    static AgyAccountInfo currentAccount();
    static QString findAgyExecutable();
    static QList<AgyQuotaEntry> fetchQuotaLimits();
};

#endif // AGYACCOUNT_H
