#include "agymodels.h"

namespace AgyModels {

QStringList commonModels()
{
    return {
        QStringLiteral("gemini-3.8-flash-low"),
        QStringLiteral("gemini-3.8-flash-medium"),
        QStringLiteral("gemini-3.7-flash-low"),
        QStringLiteral("gemini-3.6-flash-low"),
        QStringLiteral("gemini-2.5-pro"),
        QStringLiteral("gemini-2.5-flash"),
        QStringLiteral("claude-sonnet-4-6"),
        QStringLiteral("claude-opus-4-6-thinking"),
        QStringLiteral("claude-3-7-sonnet"),
        QStringLiteral("claude-3-5-sonnet"),
        QStringLiteral("gpt-4o"),
    };
}

QString defaultModel()
{
    return QStringLiteral("gemini-3.8-flash-low");
}

QString restApiModelFor(const QString &model)
{
    const QString m = model.trimmed().toLower();

    // Non-Gemini models (Claude, GPT, ...) are only reachable through the
    // Antigravity CLI; route them to a sensible Gemini default for REST.
    if (!m.startsWith(QLatin1String("gemini"))) {
        return QStringLiteral("gemini-2.5-flash");
    }

    // Public REST models we can address directly.
    if (m.startsWith(QLatin1String("gemini-2.5-pro"))) {
        return QStringLiteral("gemini-2.5-pro");
    }
    if (m.startsWith(QLatin1String("gemini-2.5-flash"))) {
        return QStringLiteral("gemini-2.5-flash");
    }
    if (m.startsWith(QLatin1String("gemini-1.5-pro"))) {
        return QStringLiteral("gemini-1.5-pro");
    }
    if (m.startsWith(QLatin1String("gemini-1.5-flash"))) {
        return QStringLiteral("gemini-1.5-flash");
    }

    // Newer/internal Gemini variants (e.g. gemini-3.x-*) that the public REST
    // endpoint does not expose: map by tier to the closest supported model.
    if (m.contains(QLatin1String("pro"))) {
        return QStringLiteral("gemini-2.5-pro");
    }
    return QStringLiteral("gemini-2.5-flash");
}

} // namespace AgyModels
