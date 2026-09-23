#include "tokenstats.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

TokenStats *TokenStats::instance()
{
    static TokenStats s_instance;
    return &s_instance;
}

TokenStats::TokenStats(QObject *parent)
    : QObject(parent)
{
    load();
}

QString TokenStats::storagePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/kateantigravity");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/tokenstats.json");
}

void TokenStats::recordUsage(int inputTokens, int outputTokens, int totalTokens)
{
    if (inputTokens <= 0 && outputTokens <= 0 && totalTokens <= 0) {
        return;
    }

    m_allTimeInput += inputTokens;
    m_allTimeOutput += outputTokens;
    m_allTimeTotal += totalTokens;
    m_turns += 1;

    const QDate today = QDate::currentDate();
    if (!m_days.isEmpty() && m_days.last().date == today) {
        m_days.last().inputTokens += inputTokens;
        m_days.last().outputTokens += outputTokens;
        m_days.last().totalTokens += totalTokens;
    } else {
        DayEntry e;
        e.date = today;
        e.inputTokens = inputTokens;
        e.outputTokens = outputTokens;
        e.totalTokens = totalTokens;
        m_days.append(e);
        while (m_days.size() > kMaxDaysKept) {
            m_days.removeFirst();
        }
    }

    save();
    Q_EMIT statsChanged();
}

QList<TokenStats::DayEntry> TokenStats::recentDays(int maxDays) const
{
    QList<DayEntry> out;
    for (int i = m_days.size() - 1; i >= 0 && out.size() < maxDays; --i) {
        out.append(m_days.at(i));
    }
    return out;
}

void TokenStats::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject root = doc.object();
    m_allTimeInput = root.value(QStringLiteral("allTimeInput")).toVariant().toLongLong();
    m_allTimeOutput = root.value(QStringLiteral("allTimeOutput")).toVariant().toLongLong();
    m_allTimeTotal = root.value(QStringLiteral("allTimeTotal")).toVariant().toLongLong();
    m_turns = root.value(QStringLiteral("turns")).toVariant().toLongLong();

    m_days.clear();
    const QJsonArray days = root.value(QStringLiteral("days")).toArray();
    for (const auto &v : days) {
        const QJsonObject o = v.toObject();
        DayEntry e;
        e.date = QDate::fromString(o.value(QStringLiteral("date")).toString(), Qt::ISODate);
        e.inputTokens = o.value(QStringLiteral("input")).toVariant().toLongLong();
        e.outputTokens = o.value(QStringLiteral("output")).toVariant().toLongLong();
        e.totalTokens = o.value(QStringLiteral("total")).toVariant().toLongLong();
        if (e.date.isValid()) {
            m_days.append(e);
        }
    }
}

void TokenStats::save()
{
    QJsonObject root;
    root.insert(QStringLiteral("allTimeInput"), QString::number(m_allTimeInput));
    root.insert(QStringLiteral("allTimeOutput"), QString::number(m_allTimeOutput));
    root.insert(QStringLiteral("allTimeTotal"), QString::number(m_allTimeTotal));
    root.insert(QStringLiteral("turns"), QString::number(m_turns));

    QJsonArray days;
    for (const auto &e : m_days) {
        QJsonObject o;
        o.insert(QStringLiteral("date"), e.date.toString(Qt::ISODate));
        o.insert(QStringLiteral("input"), QString::number(e.inputTokens));
        o.insert(QStringLiteral("output"), QString::number(e.outputTokens));
        o.insert(QStringLiteral("total"), QString::number(e.totalTokens));
        days.append(o);
    }
    root.insert(QStringLiteral("days"), days);

    QSaveFile file(storagePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

void TokenStats::reset()
{
    m_allTimeInput = 0;
    m_allTimeOutput = 0;
    m_allTimeTotal = 0;
    m_turns = 0;
    m_days.clear();
    save();
    Q_EMIT statsChanged();
}
