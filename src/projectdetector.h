#ifndef PROJECTDETECTOR_H
#define PROJECTDETECTOR_H

#include <QString>

namespace KTextEditor {
class Document;
}

struct ProjectInfo {
    QString rootPath;       // Canonical path to project root
    QString name;           // Project folder name or "Global"
    QString gitBranch;      // Git branch name or empty
    bool isGit = false;
    bool available = true;  // false if the root path no longer exists on disk

    bool isValid() const { return !rootPath.isEmpty(); }
    bool isGlobal() const { return rootPath.isEmpty(); }
    bool isGeneralFallback() const { return isGlobal(); }

    QString displayName() const;

    static ProjectInfo createGeneral();
};

class ProjectDetector
{
public:
    /**
     * Detects project information given an absolute or relative file path.
     * Looks for .kateproject, .git, and common root markers (composer.json, package.json, etc.).
     * If filePath is empty or no project is found, returns a Global/fallback ProjectInfo.
     */
    static ProjectInfo detectProject(const QString &filePath);
    static ProjectInfo detectForPath(const QString &filePath);
    static ProjectInfo detectForDocument(KTextEditor::Document *doc);

    /**
     * Reads the current Git branch for a .git directory or worktree file.
     */
    static QString readGitBranch(const QString &gitEntryPath);
};

#endif // PROJECTDETECTOR_H
