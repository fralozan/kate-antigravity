#include <QTest>
#include "editblockparser.h"
#include "editblockapplier.h"
#include "symbolindex.h"

class TestEditBlocks : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // --- EditBlockParser -----------------------------------------------------
    void testParsesSingleBlock()
    {
        const QString msg = QStringLiteral(
            "Here is the change:\n"
            "```agy-edit\n"
            "file: src/main.cpp\n"
            "<<<<<<< SEARCH\n"
            "int x = 1;\n"
            "=======\n"
            "int x = 2;\n"
            ">>>>>>> REPLACE\n"
            "```\n");
        const auto blocks = EditBlockParser::parse(msg);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks.first().filePath, QStringLiteral("src/main.cpp"));
        QCOMPARE(blocks.first().searchText, QStringLiteral("int x = 1;"));
        QCOMPARE(blocks.first().replaceText, QStringLiteral("int x = 2;"));
    }

    void testParsesMultipleBlocksAndFiles()
    {
        const QString msg = QStringLiteral(
            "file: a.cpp\n"
            "<<<<<<< SEARCH\n"
            "foo();\n"
            "=======\n"
            "bar();\n"
            ">>>>>>> REPLACE\n"
            "file: b.cpp\n"
            "<<<<<<< SEARCH\n"
            "old();\n"
            "=======\n"
            "new();\n"
            ">>>>>>> REPLACE\n");
        const auto blocks = EditBlockParser::parse(msg);
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks.at(0).filePath, QStringLiteral("a.cpp"));
        QCOMPARE(blocks.at(1).filePath, QStringLiteral("b.cpp"));
        QCOMPARE(blocks.at(1).replaceText, QStringLiteral("new();"));
    }

    void testContainsEditBlock()
    {
        QVERIFY(!EditBlockParser::containsEditBlock(QStringLiteral("just prose")));
        QVERIFY(EditBlockParser::containsEditBlock(QStringLiteral(
            "<<<<<<< SEARCH\nx\n=======\ny\n>>>>>>> REPLACE\n")));
    }

    void testMalformedBlockIgnored()
    {
        // Missing REPLACE marker -> no blocks.
        const QString msg = QStringLiteral("<<<<<<< SEARCH\nx\n=======\ny\n");
        QVERIFY(EditBlockParser::parse(msg).isEmpty());
    }

    // --- EditBlockApplier::locate -------------------------------------------
    void testLocateExact()
    {
        const QString doc = QStringLiteral("line1\nint x = 1;\nline3\n");
        EditBlock b;
        b.searchText = QStringLiteral("int x = 1;");
        b.replaceText = QStringLiteral("int x = 2;");
        const auto loc = EditBlockApplier::locate(doc, b);
        QCOMPARE(loc.status, EditBlockApplier::MatchStatus::Exact);
        QCOMPARE(loc.startLine, 1);
        QCOMPARE(loc.endLine, 1);
    }

    void testLocateWhitespaceInsensitive()
    {
        const QString doc = QStringLiteral("void f() {\n        doThing();\n}\n");
        EditBlock b;
        b.searchText = QStringLiteral("doThing();"); // no indentation in search
        b.replaceText = QStringLiteral("doOtherThing();");
        const auto loc = EditBlockApplier::locate(doc, b);
        QCOMPARE(loc.status, EditBlockApplier::MatchStatus::Whitespace);
        QCOMPARE(loc.startLine, 1);
    }

    void testLocateNotFound()
    {
        const QString doc = QStringLiteral("nothing to see here\n");
        EditBlock b;
        b.searchText = QStringLiteral("missing line");
        const auto loc = EditBlockApplier::locate(doc, b);
        QCOMPARE(loc.status, EditBlockApplier::MatchStatus::NotFound);
    }

    void testLocateInsertion()
    {
        EditBlock b;
        b.searchText = QString();
        b.replaceText = QStringLiteral("// header\n");
        const auto loc = EditBlockApplier::locate(QStringLiteral("code\n"), b);
        QCOMPARE(loc.status, EditBlockApplier::MatchStatus::Insertion);
    }

    // --- SymbolIndex::extractCandidateIdentifiers ---------------------------
    void testExtractIdentifiers()
    {
        const auto ids = SymbolIndex::extractCandidateIdentifiers(
            QStringLiteral("please refactor MyClass and start_service quickly"), 5);
        QVERIFY(ids.contains(QStringLiteral("MyClass")));
        QVERIFY(ids.contains(QStringLiteral("start_service")));
        // Common lowercase words are filtered out.
        QVERIFY(!ids.contains(QStringLiteral("please")));
        QVERIFY(!ids.contains(QStringLiteral("refactor")));
    }
};

QTEST_MAIN(TestEditBlocks)
#include "test_editblocks.moc"
