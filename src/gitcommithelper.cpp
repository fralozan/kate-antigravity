#include "gitcommithelper.h"

#include <QProcess>
#include <KLocalizedString>

namespace GitCommitHelper {

namespace {

// Run git read-only and return {ok, stdout}. Never throws; times out safely.
struct GitRun {
    bool ok = false;
    QString out;
    QString err;
};

GitRun runGit(const QString &dir, const QStringList &args, int timeoutMs = 5000)
{
    GitRun result;
    if (dir.isEmpty()) {
        return result;
    }
    QProcess proc;
    proc.setWorkingDirectory(dir);
    // Arguments are passed as a list (argv), so values can't be interpreted by a
    // shell — safe against injection from a model-authored commit message.
    proc.start(QStringLiteral("git"), args);
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(500);
        result.err = i18n("git timed out");
        return result;
    }
    result.out = QString::fromUtf8(proc.readAllStandardOutput());
    result.err = QString::fromUtf8(proc.readAllStandardError());
    result.ok = (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0);
    return result;
}

} // namespace

bool isGitRepository(const QString &dir)
{
    const GitRun r = runGit(dir, {QStringLiteral("rev-parse"), QStringLiteral("--is-inside-work-tree")});
    return r.ok && r.out.trimmed() == QLatin1String("true");
}

QString stagedDiff(const QString &dir)
{
    const GitRun r = runGit(dir, {QStringLiteral("diff"), QStringLiteral("--staged")});
    return r.ok ? r.out : QString();
}

QString stagedSummary(const QString &dir)
{
    // --porcelain: staged status is the first column (index status).
    const GitRun r = runGit(dir, {QStringLiteral("status"), QStringLiteral("--porcelain")});
    if (!r.ok) {
        return QString();
    }
    QStringList staged;
    const auto lines = r.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.size() < 3) {
            continue;
        }
        const QChar indexStatus = line.at(0);
        if (indexStatus != QLatin1Char(' ') && indexStatus != QLatin1Char('?')) {
            staged << line;
        }
    }
    return staged.join(QLatin1Char('\n'));
}

CommitResult commit(const QString &dir, const QString &message)
{
    CommitResult res;

    if (!isGitRepository(dir)) {
        res.message = i18n("Not a git repository.");
        return res;
    }
    if (message.trimmed().isEmpty()) {
        res.message = i18n("Empty commit message.");
        return res;
    }
    if (stagedDiff(dir).trimmed().isEmpty()) {
        res.message = i18n("Nothing staged to commit. Stage changes first (git add).");
        return res;
    }

    // Commit ONLY what is staged: no -a, no --amend, no --no-verify, no push.
    const GitRun r = runGit(dir, {QStringLiteral("commit"), QStringLiteral("-m"), message}, 15000);
    if (r.ok) {
        res.success = true;
        res.message = r.out.trimmed();
    } else {
        // Surface git/hook output so the user understands a rejection.
        const QString detail = r.err.trimmed().isEmpty() ? r.out.trimmed() : r.err.trimmed();
        res.message = detail.isEmpty() ? i18n("git commit failed.") : detail;
    }
    return res;
}

} // namespace GitCommitHelper
