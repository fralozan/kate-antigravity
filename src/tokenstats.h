#ifndef AGY_TOKENSTATS_H
#define AGY_TOKENSTATS_H

#include <QObject>
#include <QString>
#include <QDate>

// Persistent, cross-session token accounting. ChatSession reports per-turn
// usage here; TokenStats accumulates all-time totals plus a rolling per-day
// history so a stats panel can show trends and estimated cost.
class TokenStats : public QObject
{
    Q_OBJECT

public:
    static TokenStats *instance();

    struct DayEntry {
        QDate date;
        qint64 inputTokens = 0;
        qint64 outputTokens = 0;
        qint64 totalTokens = 0;
    };

    // Record one turn's usage (adds to all-time totals and today's bucket).
    void recordUsage(int inputTokens, int outputTokens, int totalTokens);

    qint64 allTimeInput() const { return m_allTimeInput; }
    qint64 allTimeOutput() const { return m_allTimeOutput; }
    qint64 allTimeTotal() const { return m_allTimeTotal; }
    qint64 turnsRecorded() const { return m_turns; }

    // Most recent days first, capped to `maxDays`.
    QList<DayEntry> recentDays(int maxDays = 14) const;

    void load();
    void save();
    void reset();

Q_SIGNALS:
    void statsChanged();

private:
    explicit TokenStats(QObject *parent = nullptr);

    QString storagePath() const;

    qint64 m_allTimeInput = 0;
    qint64 m_allTimeOutput = 0;
    qint64 m_allTimeTotal = 0;
    qint64 m_turns = 0;
    QList<DayEntry> m_days; // oldest first
    static constexpr int kMaxDaysKept = 90;
};

#endif // AGY_TOKENSTATS_H
