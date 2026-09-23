#ifndef AGY_GITCOMMITHELPER_H
#define AGY_GITCOMMITHELPER_H

#include <QString>

// Thin, read-mostly wrapper around git for the /commit workflow.
//
// Reads are always safe. The single write operation (commit) is deliberately
// conservative: it commits only what is already staged, never runs `git add`,
// never pushes, never amends, and never skips hooks.
namespace GitCommitHelper {

struct CommitResult {
    bool success = false;
    QString message; // human-readable outcome or error
};

// True if `dir` is inside a git working tree.
bool isGitRepository(const QString &dir);

// Return `git diff --staged` for `dir` (empty if nothing staged or on error).
QString stagedDiff(const QString &dir);

// Short `git status --porcelain` summary of staged entries (for display).
QString stagedSummary(const QString &dir);

// Create a commit with the given message using only already-staged changes.
// Respects hooks (no --no-verify) and never amends. `message` is passed via
// argv (no shell), so it is safe against injection.
CommitResult commit(const QString &dir, const QString &message);

} // namespace GitCommitHelper

#endif // AGY_GITCOMMITHELPER_H
