#include <QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QDir>

#include "gitcommithelper.h"

// These tests operate entirely inside a throwaway QTemporaryDir git repo, so
// they never touch the user's repositories.
class TestGitCommit : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    bool m_gitAvailable = false;

    bool git(const QStringList &args)
    {
        QProcess p;
        p.setWorkingDirectory(m_dir.path());
        p.start(QStringLiteral("git"), args);
        if (!p.waitForFinished(5000)) {
            p.kill();
            return false;
        }
        return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    }

    void writeFile(const QString &name, const QString &content)
    {
        QFile f(QDir(m_dir.path()).filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << content;
        f.close();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        // Skip the whole suite gracefully if git isn't installed.
        QProcess p;
        p.start(QStringLiteral("git"), {QStringLiteral("--version")});
        m_gitAvailable = p.waitForFinished(5000) && p.exitCode() == 0;
        if (!m_gitAvailable) {
            QSKIP("git not available");
        }
        QVERIFY(git({QStringLiteral("init")}));
        QVERIFY(git({QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.com")}));
        QVERIFY(git({QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")}));
        // Avoid picking up repo hooks from a parent environment.
        git({QStringLiteral("config"), QStringLiteral("core.hooksPath"), QStringLiteral("/dev/null")});
    }

    void testIsRepository()
    {
        QVERIFY(GitCommitHelper::isGitRepository(m_dir.path()));
        QVERIFY(!GitCommitHelper::isGitRepository(QDir::tempPath() + QStringLiteral("/definitely-not-a-repo-xyz")));
    }

    void testNothingStaged()
    {
        // Fresh repo, nothing staged.
        QVERIFY(GitCommitHelper::stagedDiff(m_dir.path()).trimmed().isEmpty());
        const auto res = GitCommitHelper::commit(m_dir.path(), QStringLiteral("should fail"));
        QVERIFY(!res.success);
    }

    void testStagedDiffAndCommit()
    {
        writeFile(QStringLiteral("hello.txt"), QStringLiteral("hello world\n"));
        QVERIFY(git({QStringLiteral("add"), QStringLiteral("hello.txt")}));

        const QString diff = GitCommitHelper::stagedDiff(m_dir.path());
        QVERIFY(diff.contains(QStringLiteral("hello.txt")));
        QVERIFY(diff.contains(QStringLiteral("hello world")));

        const QString summary = GitCommitHelper::stagedSummary(m_dir.path());
        QVERIFY(summary.contains(QStringLiteral("hello.txt")));

        const auto res = GitCommitHelper::commit(m_dir.path(),
            QStringLiteral("feat: add hello file\n\nInitial content."));
        QVERIFY2(res.success, qPrintable(res.message));

        // After committing, nothing should remain staged.
        QVERIFY(GitCommitHelper::stagedDiff(m_dir.path()).trimmed().isEmpty());
    }

    void testEmptyMessageRejected()
    {
        writeFile(QStringLiteral("second.txt"), QStringLiteral("data\n"));
        QVERIFY(git({QStringLiteral("add"), QStringLiteral("second.txt")}));
        const auto res = GitCommitHelper::commit(m_dir.path(), QStringLiteral("   "));
        QVERIFY(!res.success);
    }
};

QTEST_MAIN(TestGitCommit)
#include "test_gitcommit.moc"
