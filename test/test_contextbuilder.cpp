#include <QTest>
#include "contextbuilder.h"

class TestContextBuilder : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testBuildPrompt()
    {
        CompletionContext ctx;
        ctx.fileName = QStringLiteral("main.cpp");
        ctx.language = QStringLiteral("C++");
        ctx.prefix = QStringLiteral("int main() {\n    std::cout << \"Hello\";\n    ");
        ctx.suffix = QStringLiteral("\n    return 0;\n}");

        QString prompt = ContextBuilder::buildPrompt(ctx);
        QVERIFY(prompt.contains(QStringLiteral("main.cpp")));
        QVERIFY(prompt.contains(QStringLiteral("C++")));
        QVERIFY(prompt.contains(QStringLiteral("<|cursor|>")));
        QVERIFY(prompt.contains(QStringLiteral("std::cout << \"Hello\";")));
        QVERIFY(prompt.contains(QStringLiteral("return 0;")));
        QVERIFY(prompt.contains(QStringLiteral("FEW-SHOT EXAMPLES")));
    }

    void testEnrichedPromptFeatures()
    {
        CompletionContext ctx;
        ctx.fileName = QStringLiteral("service.cpp");
        ctx.language = QStringLiteral("C++");
        ctx.fileHeader = QStringLiteral("#include \"service.h\"\n#include <vector>");
        ctx.indentation = QStringLiteral("    ");
        ctx.isMidLine = true;

        RelatedFileContext rfc;
        rfc.fileName = QStringLiteral("service.h");
        rfc.language = QStringLiteral("C++");
        rfc.snippet = QStringLiteral("class Service { void run(); };");
        ctx.relatedFiles.append(rfc);

        ctx.prefix = QStringLiteral("void Service::run() { auto x = ");
        ctx.suffix = QStringLiteral("; }");

        QString prompt = ContextBuilder::buildPrompt(ctx);
        QVERIFY(prompt.contains(QStringLiteral("<file_imports_and_declarations>")));
        QVERIFY(prompt.contains(QStringLiteral("#include \"service.h\"")));
        QVERIFY(prompt.contains(QStringLiteral("<related_open_file name=\"service.h\">")));
        QVERIFY(prompt.contains(QStringLiteral("class Service")));
        QVERIFY(prompt.contains(QStringLiteral("DO NOT output newlines")));
    }

    void testMidLineSanitizing()
    {
        CompletionContext ctx;
        ctx.isMidLine = true;

        // Model returned multiple lines for an inline expression: it should be truncated to the first line
        QString raw = QStringLiteral("calculateSomething()\n    extraLine();");
        QString clean = ContextBuilder::sanitizeResponse(raw, ctx);
        QCOMPARE(clean, QStringLiteral("calculateSomething()"));
    }

    void testSanitizeMarkdownFences()
    {
        CompletionContext ctx;
        QString raw = QStringLiteral("```cpp\nauto value = calculate();\n```");
        QString clean = ContextBuilder::sanitizeResponse(raw, ctx);
        QCOMPARE(clean, QStringLiteral("auto value = calculate();"));
    }

    void testSanitizeStrayFimTags()
    {
        CompletionContext ctx;
        QString raw = QStringLiteral("<|cursor|>foo.bar();<|suffix|>");
        QString clean = ContextBuilder::sanitizeResponse(raw, ctx);
        QCOMPARE(clean, QStringLiteral("foo.bar();"));
    }

    void testSanitizeSuffixOverlap()
    {
        CompletionContext ctx;
        ctx.suffix = QStringLiteral(");\n}");
        QString raw = QStringLiteral("compute(42);\n}");
        QString clean = ContextBuilder::sanitizeResponse(raw, ctx);
        QCOMPARE(clean, QStringLiteral("compute(42"));
    }
};

QTEST_MAIN(TestContextBuilder)
#include "test_contextbuilder.moc"
