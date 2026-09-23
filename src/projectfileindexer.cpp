#include "projectfileindexer.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QQueue>
#include <QPair>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QThread>
#include <KTextEditor/Editor>
#include <KTextEditor/Document>
#include <KTextEditor/MainWindow>
#include <algorithm>

ProjectFileIndexer *ProjectFileIndexer::instance()
{
    static ProjectFileIndexer s_instance;
    return &s_instance;
}

void ProjectFileIndexer::clearCache()
{
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_indexingProjects.clear();
}

void ProjectFileIndexer::startIndexing(const QString &projectPath)
{
    if (projectPath.isEmpty()) {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_indexingProjects.contains(projectPath)) {
            return; // Already running
        }
        m_indexingProjects.insert(projectPath);
    }

    QThread *thread = QThread::create([this, projectPath]() {
        scanProjectInternal(projectPath);
        // Deliver the signal on the thread the indexer lives in (the GUI thread),
        // never from the worker thread, so downstream GUI slots run safely.
        QMetaObject::invokeMethod(this, [this, projectPath]() {
            Q_EMIT indexingFinished(projectPath);
        }, Qt::QueuedConnection);
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ProjectFileIndexer::scanProjectSync(const QString &projectPath)
{
    scanProjectInternal(projectPath);
}

void ProjectFileIndexer::scanProjectInternal(const QString &projectPath)
{
    if (projectPath.isEmpty()) {
        QMutexLocker locker(&m_mutex);
        m_indexingProjects.remove(projectPath);
        return;
    }

    const QDir rootDir(projectPath);
    if (!rootDir.exists()) {
        QMutexLocker locker(&m_mutex);
        m_indexingProjects.remove(projectPath);
        return;
    }

    static const QSet<QString> s_ignoredDirNames = {
        QStringLiteral(".git"),
        QStringLiteral("node_modules"),
        QStringLiteral("vendor"),
        QStringLiteral("build"),
        QStringLiteral("dist"),
        QStringLiteral(".cache"),
        QStringLiteral("cache"),
        QStringLiteral("__pycache__"),
        QStringLiteral(".idea"),
        QStringLiteral(".vscode"),
        QStringLiteral("target"),
        QStringLiteral("bin"),
        QStringLiteral("obj"),
        QStringLiteral("venv"),
        QStringLiteral(".venv"),
        QStringLiteral(".svn"),
        QStringLiteral(".hg"),
        QStringLiteral("tmp"),
        QStringLiteral("temp")
    };

    static const QSet<QString> s_ignoredExts = {
        QStringLiteral("so"), QStringLiteral("dll"), QStringLiteral("exe"),
        QStringLiteral("a"), QStringLiteral("o"), QStringLiteral("pyc"),
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("ico"), QStringLiteral("svg"),
        QStringLiteral("mp4"), QStringLiteral("zip"), QStringLiteral("tar"),
        QStringLiteral("gz"), QStringLiteral("pdf"), QStringLiteral("bin"),
        QStringLiteral("woff"), QStringLiteral("woff2"), QStringLiteral("ttf")
    };

    QList<ProjectFileItem> items;
    QQueue<QPair<QString, int>> queue; // pair of (directoryPath, depth)
    queue.enqueue({projectPath, 0});

    const int maxDepth = 4;
    const int maxEntries = 2000;
    QElapsedTimer timer;
    timer.start();

    while (!queue.isEmpty() && items.size() < maxEntries) {
        // Guard against slow network mounts (max 2 seconds in background)
        if (timer.elapsed() > 2000) {
            break;
        }

        const auto current = queue.dequeue();
        const QString dirPath = current.first;
        const int depth = current.second;

        QDir dir(dirPath);
        const auto entryList = dir.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
            QDir::NoSort
        );

        for (const QFileInfo &fi : entryList) {
            if (fi.isDir()) {
                const QString dirName = fi.fileName().toLower();
                // Strictly prune ignored directories — NEVER descend into them
                if (s_ignoredDirNames.contains(dirName) || dirName.startsWith(QLatin1Char('.'))) {
                    continue;
                }

                ProjectFileItem item;
                item.fullPath = fi.filePath();
                item.fileName = fi.fileName() + QLatin1Char('/');
                item.relativePath = rootDir.relativeFilePath(fi.filePath()) + QLatin1Char('/');
                item.isDirectory = true;
                items.append(item);

                if (depth < maxDepth && items.size() < maxEntries) {
                    queue.enqueue({fi.filePath(), depth + 1});
                }
            } else {
                const QString ext = fi.suffix().toLower();
                if (s_ignoredExts.contains(ext)) {
                    continue;
                }

                ProjectFileItem item;
                item.fullPath = fi.filePath();
                item.fileName = fi.fileName();
                item.relativePath = rootDir.relativeFilePath(fi.filePath());
                item.isDirectory = false;
                items.append(item);

                if (items.size() >= maxEntries) {
                    break;
                }
            }
        }
    }

    {
        QMutexLocker locker(&m_mutex);
        CachedProject cached;
        cached.lastIndexed = QDateTime::currentDateTime();
        cached.files = items;
        m_cache.insert(projectPath, cached);
        m_indexingProjects.remove(projectPath);
    }
}

QList<ProjectFileItem> ProjectFileIndexer::searchFiles(const QString &projectPath,
                                                     const QString &filterQuery,
                                                     KTextEditor::MainWindow *mainWindow,
                                                     int maxResults)
{
    Q_UNUSED(mainWindow);

    QList<ProjectFileItem> openEditorFiles;
    QSet<QString> openPaths;

    // 1. Gather open documents in Kate editor (always instant & in-memory)
    if (KTextEditor::Editor::instance()) {
        const auto docs = KTextEditor::Editor::instance()->documents();
        for (auto *doc : docs) {
            if (!doc || !doc->url().isLocalFile()) {
                continue;
            }
            const QString localPath = doc->url().toLocalFile();
            openPaths.insert(localPath);

            QFileInfo fi(localPath);
            ProjectFileItem item;
            item.fullPath = localPath;
            item.fileName = fi.fileName();
            if (!projectPath.isEmpty() && localPath.startsWith(projectPath)) {
                item.relativePath = QDir(projectPath).relativeFilePath(localPath);
            } else {
                item.relativePath = fi.fileName();
            }
            item.isOpenInEditor = true;
            item.isDirectory = false;
            item.lineCount = doc->lines();
            openEditorFiles.append(item);
        }
    }

    // 2. Check cache or trigger asynchronous background indexing
    QList<ProjectFileItem> cachedFiles;
    if (!projectPath.isEmpty()) {
        QMutexLocker locker(&m_mutex);
        if (m_cache.contains(projectPath)) {
            const auto &cached = m_cache.value(projectPath);
            cachedFiles = cached.files;
            if (cached.lastIndexed.secsTo(QDateTime::currentDateTime()) > 60) {
                // Refresh asynchronously in the background
                locker.unlock();
                startIndexing(projectPath);
            }
        } else {
            // First time: trigger background indexing without blocking GUI
            locker.unlock();
            startIndexing(projectPath);
        }
    }

    QList<ProjectFileItem> results;
    const QString query = filterQuery.trimmed().toLower();

    // Check open files first (highest priority)
    for (const auto &openItem : openEditorFiles) {
        if (query.isEmpty()
            || openItem.fileName.toLower().contains(query)
            || openItem.relativePath.toLower().contains(query)) {
            results.append(openItem);
            if (results.size() >= maxResults) {
                return results;
            }
        }
    }

    // Then add cached files
    for (const auto &item : cachedFiles) {
        if (openPaths.contains(item.fullPath)) {
            continue;
        }

        if (query.isEmpty()
            || item.fileName.toLower().contains(query)
            || item.relativePath.toLower().contains(query)) {
            results.append(item);
            if (results.size() >= maxResults) {
                break;
            }
        }
    }

    // Sort: open files first, then directories, then shorter paths
    std::stable_sort(results.begin(), results.end(), [](const ProjectFileItem &a, const ProjectFileItem &b) {
        if (a.isOpenInEditor != b.isOpenInEditor) {
            return a.isOpenInEditor;
        }
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory;
        }
        return a.relativePath.length() < b.relativePath.length();
    });

    if (results.size() > maxResults) {
        results = results.mid(0, maxResults);
    }

    return results;
}
