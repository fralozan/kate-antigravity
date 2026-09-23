#include "projectrules.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>

namespace ProjectRules {

namespace {

struct CacheEntry {
    qint64 mtimeMs = 0;
    qint64 size = -1;   // -1 means "no rules file present"
    QString content;
};

QMutex g_mutex;
QHash<QString, CacheEntry> g_cache; // project root -> cached rules

// Hard cap so a pathological rules file can't blow up the prompt.
constexpr qint64 kMaxRulesBytes = 16 * 1024;

} // namespace

QStringList candidateFileNames()
{
    return {
        QStringLiteral(".antigravity"),
        QStringLiteral(".antigravityrules"),
    };
}

QString rulesForProject(const QString &projectRoot)
{
    if (projectRoot.trimmed().isEmpty()) {
        return QString();
    }

    const QDir root(projectRoot);
    QFileInfo found;
    for (const QString &name : candidateFileNames()) {
        const QString candidate = root.filePath(name);
        QFileInfo fi(candidate);
        if (fi.exists() && fi.isFile()) {
            found = fi;
            break;
        }
    }

    QMutexLocker locker(&g_mutex);

    if (!found.exists()) {
        // Remember the "absent" state so we don't stat repeatedly on every build.
        CacheEntry absent;
        absent.size = -1;
        g_cache.insert(projectRoot, absent);
        return QString();
    }

    const qint64 mtimeMs = found.lastModified().toMSecsSinceEpoch();
    const qint64 size = found.size();

    const auto it = g_cache.constFind(projectRoot);
    if (it != g_cache.constEnd() && it->size == size && it->mtimeMs == mtimeMs) {
        return it->content;
    }

    QString content;
    QFile file(found.absoluteFilePath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray raw = file.read(kMaxRulesBytes);
        content = QString::fromUtf8(raw).trimmed();
        file.close();
    }

    CacheEntry entry;
    entry.mtimeMs = mtimeMs;
    entry.size = size;
    entry.content = content;
    g_cache.insert(projectRoot, entry);

    return content;
}

void clearCache()
{
    QMutexLocker locker(&g_mutex);
    g_cache.clear();
}

} // namespace ProjectRules
