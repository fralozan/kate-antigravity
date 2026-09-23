#ifndef PROJECTFILEINDEXER_H
#define PROJECTFILEINDEXER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QMutex>

namespace KTextEditor {
class MainWindow;
}

struct ProjectFileItem {
    QString relativePath;
    QString fileName;
    QString fullPath;
    bool isOpenInEditor = false;
    bool isDirectory = false;
    int lineCount = 0;
};

class ProjectFileIndexer : public QObject
{
    Q_OBJECT

public:
    static ProjectFileIndexer *instance();

    QList<ProjectFileItem> searchFiles(const QString &projectPath,
                                       const QString &filterQuery,
                                       KTextEditor::MainWindow *mainWindow = nullptr,
                                       int maxResults = 25);

    void startIndexing(const QString &projectPath);
    void clearCache();

    // Helper to scan synchronously if needed (e.g. unit tests)
    void scanProjectSync(const QString &projectPath);

Q_SIGNALS:
    void indexingFinished(const QString &projectPath);

private:
    explicit ProjectFileIndexer(QObject *parent = nullptr) : QObject(parent) {}

    void scanProjectInternal(const QString &projectPath);

    struct CachedProject {
        QDateTime lastIndexed;
        QList<ProjectFileItem> files;
    };

    mutable QMutex m_mutex;
    QMap<QString, CachedProject> m_cache;
    QSet<QString> m_indexingProjects;
};

#endif // PROJECTFILEINDEXER_H
