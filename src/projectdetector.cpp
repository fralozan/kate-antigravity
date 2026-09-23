#include "projectdetector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <KLocalizedString>
#include <KTextEditor/Document>

ProjectInfo ProjectInfo::createGeneral()
{
    ProjectInfo info;
    info.name = i18n("Global / General");
    return info;
}

QString ProjectInfo::displayName() const
{
    if (isGlobal() || rootPath.isEmpty()) {
        return i18n("Global / General");
    }

    QString base = (isGit && !gitBranch.isEmpty())
        ? QStringLiteral("%1 (%2)").arg(name, gitBranch)
        : name;

    if (!available) {
        base += QStringLiteral(" — ") + i18n("unavailable");
    }
    return base;
}

QString ProjectDetector::readGitBranch(const QString &gitEntryPath)
{
    QFileInfo fi(gitEntryPath);
    QString headPath;

    if (fi.isDir()) {
        headPath = gitEntryPath + QStringLiteral("/HEAD");
    } else if (fi.isFile()) {
        QFile f(gitEntryPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(f.readAll()).trimmed();
            if (content.startsWith(QLatin1String("gitdir:"))) {
                const QString rel = content.mid(7).trimmed();
                const QDir baseDir = fi.dir();
                headPath = baseDir.cleanPath(baseDir.filePath(rel)) + QStringLiteral("/HEAD");
            }
        }
    }

    if (headPath.isEmpty()) {
        return QString();
    }

    QFile headFile(headPath);
    if (!headFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    const QString content = QString::fromUtf8(headFile.readAll()).trimmed();
    const QString refPrefix = QStringLiteral("ref: refs/heads/");
    if (content.startsWith(refPrefix)) {
        return content.mid(refPrefix.length());
    }

    // Detached HEAD: return short commit hash
    if (content.length() >= 7) {
        return content.left(7);
    }

    return QString();
}

ProjectInfo ProjectDetector::detectProject(const QString &filePath)
{
    if (filePath.isEmpty()) {
        ProjectInfo globalInfo;
        globalInfo.name = i18n("Global / General");
        return globalInfo;
    }

    QFileInfo fi(filePath);
    QDir dir = fi.isDir() ? QDir(filePath) : fi.dir();

    if (!dir.exists()) {
        ProjectInfo globalInfo;
        globalInfo.name = i18n("Global / General");
        return globalInfo;
    }

    static const QStringList projectMarkers = {
        QStringLiteral(".kateproject"),
        QStringLiteral("composer.json"),
        QStringLiteral("package.json"),
        QStringLiteral("Cargo.toml"),
        QStringLiteral("pyproject.toml"),
        QStringLiteral("CMakeLists.txt"),
        QStringLiteral("go.mod"),
        QStringLiteral("pom.xml"),
        QStringLiteral("build.gradle"),
        QStringLiteral("setup.py"),
        QStringLiteral("Makefile")
    };

    QString candidateMarkerRoot;

    // Traverse upward to root
    while (true) {
        // Check for Git repository first (strongest marker)
        if (dir.exists(QStringLiteral(".git"))) {
            ProjectInfo info;
            info.rootPath = dir.canonicalPath();
            info.name = dir.dirName();
            info.isGit = true;
            info.gitBranch = readGitBranch(dir.filePath(QStringLiteral(".git")));
            return info;
        }

        // Check for common project markers
        if (candidateMarkerRoot.isEmpty()) {
            for (const QString &marker : projectMarkers) {
                if (dir.exists(marker)) {
                    candidateMarkerRoot = dir.canonicalPath();
                    break;
                }
            }
        }

        if (!dir.cdUp()) {
            break;
        }
    }

    // If marker found without .git
    if (!candidateMarkerRoot.isEmpty()) {
        QDir markerDir(candidateMarkerRoot);
        ProjectInfo info;
        info.rootPath = candidateMarkerRoot;
        info.name = markerDir.dirName();
        info.isGit = false;
        return info;
    }

    // Fallback: directory containing the file
    QDir fileDir = fi.isDir() ? QDir(filePath) : fi.dir();
    ProjectInfo fallback;
    fallback.rootPath = fileDir.canonicalPath();
    fallback.name = fileDir.dirName();
    fallback.isGit = false;
    return fallback;
}

ProjectInfo ProjectDetector::detectForPath(const QString &filePath)
{
    return detectProject(filePath);
}

ProjectInfo ProjectDetector::detectForDocument(KTextEditor::Document *doc)
{
    if (!doc) {
        return ProjectInfo::createGeneral();
    }

    const QUrl url = doc->url();
    if (url.isLocalFile()) {
        return detectProject(url.toLocalFile());
    }

    return ProjectInfo::createGeneral();
}
