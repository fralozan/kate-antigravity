#ifndef AGY_CHATSESSIONMANAGER_H
#define AGY_CHATSESSIONMANAGER_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QString>
#include "projectdetector.h"

class ChatSession;

class ChatSessionManager : public QObject
{
    Q_OBJECT

public:
    static ChatSessionManager *instance();

    explicit ChatSessionManager(QObject *parent = nullptr);
    ~ChatSessionManager() override;

    ChatSession *sessionForProject(const ProjectInfo &info);
    ChatSession *sessionForPath(const QString &rootPath, const QString &name = QString());

    QList<ProjectInfo> knownProjects() const;
    void addKnownProject(const ProjectInfo &info);

    // Remove a workspace from the known list and delete its persisted session
    // (both from memory and disk). The Global workspace cannot be removed.
    void removeProject(const QString &rootPath);

    // Rename the display name of a known workspace and persist it.
    void renameProject(const QString &rootPath, const QString &newName);

    void saveSession(ChatSession *session);
    void saveAllSessions();
    void clearSession(const QString &rootPath);

    QString cacheDirectoryPath() const;
    void setCacheDirectoryPath(const QString &path);

    static QString projectKey(const QString &rootPath);

Q_SIGNALS:
    void projectListChanged();
    void sessionSaved(const QString &projectKey);

private:
    void loadKnownProjectsFromCache();
    QString sessionFilePath(const QString &key) const;

    QString m_cacheDir;
    QMap<QString, ChatSession *> m_sessions;
    QList<ProjectInfo> m_knownProjects;
};

#endif // AGY_CHATSESSIONMANAGER_H
