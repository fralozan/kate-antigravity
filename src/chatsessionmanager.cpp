#include "chatsessionmanager.h"
#include "chatsession.h"

#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <KLocalizedString>

ChatSessionManager *ChatSessionManager::instance()
{
    static ChatSessionManager s_instance;
    return &s_instance;
}

ChatSessionManager::ChatSessionManager(QObject *parent)
    : QObject(parent)
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/kateantigravity/sessions");

    QDir().mkpath(m_cacheDir);

    // Ensure General project is always present
    m_knownProjects.append(ProjectInfo::createGeneral());

    loadKnownProjectsFromCache();
}

ChatSessionManager::~ChatSessionManager()
{
    saveAllSessions();
}

QString ChatSessionManager::cacheDirectoryPath() const
{
    return m_cacheDir;
}

void ChatSessionManager::setCacheDirectoryPath(const QString &path)
{
    m_cacheDir = path;
    QDir().mkpath(m_cacheDir);
    m_knownProjects.clear();
    m_knownProjects.append(ProjectInfo::createGeneral());
    loadKnownProjectsFromCache();
}

QString ChatSessionManager::projectKey(const QString &rootPath)
{
    if (rootPath.trimmed().isEmpty()) {
        return QStringLiteral("_global_");
    }
    const QString canonical = QDir::cleanPath(rootPath.trimmed());
    return QString::fromLatin1(QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString ChatSessionManager::sessionFilePath(const QString &key) const
{
    return QDir(m_cacheDir).filePath(key + QStringLiteral(".json"));
}

QList<ProjectInfo> ChatSessionManager::knownProjects() const
{
    return m_knownProjects;
}

void ChatSessionManager::addKnownProject(const ProjectInfo &info)
{
    const QString key = projectKey(info.rootPath);
    for (int i = 0; i < m_knownProjects.size(); ++i) {
        if (projectKey(m_knownProjects.at(i).rootPath) == key) {
            // Update metadata if needed (e.g. branch or name)
            m_knownProjects[i] = info;
            Q_EMIT projectListChanged();
            return;
        }
    }

    m_knownProjects.append(info);
    Q_EMIT projectListChanged();
}

void ChatSessionManager::removeProject(const QString &rootPath)
{
    // The Global workspace (empty rootPath) is permanent.
    if (rootPath.trimmed().isEmpty()) {
        return;
    }

    const QString key = projectKey(rootPath);

    // Drop the in-memory session, if any.
    if (m_sessions.contains(key)) {
        ChatSession *session = m_sessions.take(key);
        if (session) {
            session->deleteLater();
        }
    }

    // Delete the persisted session file.
    QFile::remove(sessionFilePath(key));

    // Remove from the known list.
    bool removed = false;
    for (int i = 0; i < m_knownProjects.size(); ++i) {
        if (projectKey(m_knownProjects.at(i).rootPath) == key) {
            m_knownProjects.removeAt(i);
            removed = true;
            break;
        }
    }

    if (removed) {
        Q_EMIT projectListChanged();
    }
}

void ChatSessionManager::renameProject(const QString &rootPath, const QString &newName)
{
    if (rootPath.trimmed().isEmpty() || newName.trimmed().isEmpty()) {
        return;
    }

    const QString key = projectKey(rootPath);
    bool changed = false;

    for (int i = 0; i < m_knownProjects.size(); ++i) {
        if (projectKey(m_knownProjects.at(i).rootPath) == key) {
            m_knownProjects[i].name = newName.trimmed();
            changed = true;
            break;
        }
    }

    // Update and persist the session's stored name so it survives restarts.
    if (m_sessions.contains(key)) {
        ChatSession *session = m_sessions.value(key);
        session->setProject(session->projectPath(), newName.trimmed());
        saveSession(session);
    }

    if (changed) {
        Q_EMIT projectListChanged();
    }
}

ChatSession *ChatSessionManager::sessionForPath(const QString &rootPath, const QString &name)
{
    ProjectInfo info;
    info.rootPath = rootPath;
    info.name = name.isEmpty() ? (rootPath.isEmpty() ? i18n("General") : QFileInfo(rootPath).fileName()) : name;
    return sessionForProject(info);
}

ChatSession *ChatSessionManager::sessionForProject(const ProjectInfo &info)
{
    const QString key = projectKey(info.rootPath);

    if (m_sessions.contains(key)) {
        ChatSession *session = m_sessions.value(key);
        // Ensure name/path are up to date
        session->setProject(info.rootPath, info.name);
        return session;
    }

    auto *session = new ChatSession(this);
    session->setProject(info.rootPath, info.name);

    // Try loading persisted messages from disk
    const QString filePath = sessionFilePath(key);
    QFile file(filePath);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
        if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
            session->fromJson(doc.object());
        }
        file.close();
    }

    // Auto-save on updates
    connect(session, &ChatSession::sessionUpdated, this, [this, session]() {
        saveSession(session);
    });

    m_sessions.insert(key, session);
    addKnownProject(info);

    return session;
}

void ChatSessionManager::saveSession(ChatSession *session)
{
    if (!session) {
        return;
    }

    const QString key = projectKey(session->projectPath());
    const QString filePath = sessionFilePath(key);

    const QJsonObject root = session->toJson();
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);

    QSaveFile saveFile(filePath);
    if (saveFile.open(QIODevice::WriteOnly)) {
        saveFile.write(data);
        if (saveFile.commit()) {
            Q_EMIT sessionSaved(key);
        }
    }
}

void ChatSessionManager::saveAllSessions()
{
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        saveSession(it.value());
    }
}

void ChatSessionManager::clearSession(const QString &rootPath)
{
    const QString key = projectKey(rootPath);
    if (m_sessions.contains(key)) {
        m_sessions.value(key)->clearHistory();
    }

    const QString filePath = sessionFilePath(key);
    QFile::remove(filePath);
}

void ChatSessionManager::loadKnownProjectsFromCache()
{
    QDir dir(m_cacheDir);
    if (!dir.exists()) {
        return;
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QString &fileName : files) {
        QFile f(dir.filePath(fileName));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        f.close();

        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        const QJsonObject obj = doc.object();
        const QString path = obj.value(QStringLiteral("projectPath")).toString();
        const QString name = obj.value(QStringLiteral("projectName")).toString();

        if (path.isEmpty()) {
            continue; // General already added
        }

        ProjectInfo info;
        const bool dirExists = QDir(path).exists();
        if (dirExists) {
            info = ProjectDetector::detectForPath(path);
        } else {
            // Keep the workspace listed but flagged as unavailable, rather than
            // silently dropping it — the user can still see and remove it.
            info.rootPath = path;
            info.available = false;
        }
        if (!name.isEmpty() && info.name.isEmpty()) {
            info.name = name;
        }

        // Add without duplicating
        const QString key = projectKey(info.rootPath);
        bool exists = false;
        for (const auto &p : m_knownProjects) {
            if (projectKey(p.rootPath) == key) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_knownProjects.append(info);
        }
    }
}
