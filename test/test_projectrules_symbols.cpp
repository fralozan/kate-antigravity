#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QDir>

#include "projectrules.h"
#include "symbolindex.h"
#include "mentionresolver.h"

class TestProjectRulesAndSymbols : public QObject
{
    Q_OBJECT

private:
    static void writeFile(const QString &path, const QString &content)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << content;
        f.close();
    }

private Q_SLOTS:
    void init()
    {
        ProjectRules::clearCache();
    }

    // --- ProjectRules --------------------------------------------------------
    void testNoRulesFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(ProjectRules::rulesForProject(dir.path()).isEmpty());
    }

    void testReadsRulesFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(QDir(dir.path()).filePath(QStringLiteral(".antigravity")),
                  QStringLiteral("Always respond in Spanish.\nUse tabs.\n"));

        const QString rules = ProjectRules::rulesForProject(dir.path());
        QVERIFY(rules.contains(QStringLiteral("Always respond in Spanish.")));
        QVERIFY(rules.contains(QStringLiteral("Use tabs.")));
    }

    void testEmptyRootReturnsEmpty()
    {
        QVERIFY(ProjectRules::rulesForProject(QString()).isEmpty());
    }

    // --- SymbolIndex ---------------------------------------------------------
    void testFindsCppFunction()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(QDir(dir.path()).filePath(QStringLiteral("math.cpp")),
                  QStringLiteral("int addNumbers(int a, int b) {\n    return a + b;\n}\n"));

        const auto matches = SymbolIndex::findDefinitions(dir.path(), QStringLiteral("addNumbers"), 5);
        QVERIFY(!matches.isEmpty());
        QCOMPARE(matches.first().name, QStringLiteral("addNumbers"));
        QVERIFY(matches.first().snippet.contains(QStringLiteral("return a + b;")));
    }

    void testFindsPythonClass()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(QDir(dir.path()).filePath(QStringLiteral("widget.py")),
                  QStringLiteral("class Widget:\n    def __init__(self):\n        self.x = 1\n"));

        const auto matches = SymbolIndex::findDefinitions(dir.path(), QStringLiteral("Widget"), 5);
        QVERIFY(!matches.isEmpty());
        QVERIFY(matches.first().snippet.contains(QStringLiteral("class Widget")));
    }

    void testUnknownSymbolReturnsEmpty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(QDir(dir.path()).filePath(QStringLiteral("a.cpp")),
                  QStringLiteral("int main() { return 0; }\n"));
        QVERIFY(SymbolIndex::findDefinitions(dir.path(), QStringLiteral("DoesNotExist"), 5).isEmpty());
    }

    // --- MentionResolver: new behaviours -------------------------------------
    void testPathContainmentBlocksEscape()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(QDir(dir.path()).filePath(QStringLiteral("inside.txt")),
                  QStringLiteral("hello"));

        // A path that escapes the project root must be skipped with a warning.
        MentionResolution res = MentionResolver::resolveMentions(
            QStringLiteral("look at @../../../../etc/hosts please"), dir.path(), nullptr);
        QVERIFY(res.resolvedFiles.isEmpty());
        QVERIFY(!res.warnings.isEmpty());
    }

    void testLineRangeMention()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(QDir(dir.path()).filePath(QStringLiteral("lines.txt")),
                  QStringLiteral("one\ntwo\nthree\nfour\nfive\n"));

        MentionResolution res = MentionResolver::resolveMentions(
            QStringLiteral("see @lines.txt:2-3"), dir.path(), nullptr);
        QCOMPARE(res.resolvedFiles.size(), 1);
        QVERIFY(res.assembledContext.contains(QStringLiteral("(lines 2-3)")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("two")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("three")));
        // Out-of-range lines must not be included.
        QVERIFY(!res.assembledContext.contains(QStringLiteral("five")));
    }

    void testSymbolMention()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(QDir(dir.path()).filePath(QStringLiteral("svc.cpp")),
                  QStringLiteral("void startService(int port) {\n    doWork();\n}\n"));

        MentionResolution res = MentionResolver::resolveMentions(
            QStringLiteral("explain @symbol:startService"), dir.path(), nullptr);
        QCOMPARE(res.resolvedFiles.size(), 1);
        QVERIFY(res.resolvedFiles.first().contains(QStringLiteral("@symbol:startService")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("Symbol Definitions: startService")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("doWork();")));
    }
};

QTEST_MAIN(TestProjectRulesAndSymbols)
#include "test_projectrules_symbols.moc"
