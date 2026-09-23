#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QJsonObject>

#include "slashcommandrouter.h"
#include "mentionresolver.h"
#include "projectfileindexer.h"
#include "chatsession.h"
#include "chatwidget.h"
#include "settings.h"
#include "agyaccount.h"

class TestSlashAndMentions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testSlashCommandDetection()
    {
        QVERIFY(SlashCommandRouter::isSlashCommand(QStringLiteral("/help")));
        QVERIFY(SlashCommandRouter::isSlashCommand(QStringLiteral("/settings")));
        QVERIFY(SlashCommandRouter::isSlashCommand(QStringLiteral("/clear")));
        QVERIFY(SlashCommandRouter::isSlashCommand(QStringLiteral("/project")));
        QVERIFY(SlashCommandRouter::isSlashCommand(QStringLiteral("/model gemini-2.5-pro")));
        QVERIFY(SlashCommandRouter::isSlashCommand(QStringLiteral("/usage")));
        QVERIFY(SlashCommandRouter::isSlashCommand(QStringLiteral("/files")));
        QVERIFY(SlashCommandRouter::isSlashCommand(QStringLiteral("/unknown")));

        // Negatives
        QVERIFY(!SlashCommandRouter::isSlashCommand(QStringLiteral("/")));
        QVERIFY(!SlashCommandRouter::isSlashCommand(QStringLiteral("//comment")));
        QVERIFY(!SlashCommandRouter::isSlashCommand(QStringLiteral("hello /help")));
        QVERIFY(!SlashCommandRouter::isSlashCommand(QStringLiteral("@src/main.cpp")));
        QVERIFY(!SlashCommandRouter::isSlashCommand(QStringLiteral("")));
    }

    void testSlashCommandAvailableList()
    {
        const auto cmds = SlashCommandRouter::availableCommands();
        QVERIFY(!cmds.isEmpty());

        QStringList names;
        for (const auto &c : cmds) {
            names << c.name;
        }

        QVERIFY(names.contains(QStringLiteral("help")));
        QVERIFY(names.contains(QStringLiteral("settings")));
        QVERIFY(names.contains(QStringLiteral("clear")));
        QVERIFY(names.contains(QStringLiteral("reset")));
        QVERIFY(names.contains(QStringLiteral("project")));
        QVERIFY(names.contains(QStringLiteral("model")));
        QVERIFY(names.contains(QStringLiteral("usage")));
        QVERIFY(names.contains(QStringLiteral("files")));
        QVERIFY(names.contains(QStringLiteral("export")));
    }

    void testSlashCommandClear()
    {
        ChatSession session;
        session.addSystemMessage(QStringLiteral("Initial message"));
        QCOMPARE(session.messages().size(), 1);

        ChatWidget widget(nullptr, &session);
        bool handled = SlashCommandRouter::execute(QStringLiteral("/clear"), &widget, &session, nullptr, nullptr);
        QVERIFY(handled);
        // /clear preserves messages in memory
        QCOMPARE(session.messages().size(), 1);
    }

    void testSlashCommandReset()
    {
        ChatSession session;
        session.addSystemMessage(QStringLiteral("Initial message"));
        QCOMPARE(session.messages().size(), 1);

        ChatWidget widget(nullptr, &session);
        bool handled = SlashCommandRouter::execute(QStringLiteral("/reset"), &widget, &session, nullptr, nullptr);
        QVERIFY(handled);
        // /reset wipes history
        QCOMPARE(session.messages().size(), 0);
    }

    void testSlashCommandHelp()
    {
        ChatSession session;
        bool handled = SlashCommandRouter::execute(QStringLiteral("/help"), nullptr, &session, nullptr, nullptr);
        QVERIFY(handled);
        QCOMPARE(session.messages().size(), 1);
        QCOMPARE(session.messages().at(0).role, ChatMessage::Role::System);
        QVERIFY(session.messages().at(0).text.contains(QStringLiteral("/help")));
        QVERIFY(session.messages().at(0).text.contains(QStringLiteral("/settings")));
    }

    void testSlashCommandUsage()
    {
        ChatSession session;
        bool handled = SlashCommandRouter::execute(QStringLiteral("/usage"), nullptr, &session, nullptr, nullptr);
        QVERIFY(handled);
        QCOMPARE(session.messages().size(), 1);
        QCOMPARE(session.messages().at(0).role, ChatMessage::Role::System);
        QVERIFY(session.messages().at(0).text.contains(QStringLiteral("📊")));
        QVERIFY(session.messages().at(0).text.contains(session.model()));
    }

    void testSlashCommandModel()
    {
        ChatSession session;
        const QString origModel = session.model();
        const QString globalDefault = AgySettings::instance()->model;

        // Query model
        bool handled = SlashCommandRouter::execute(QStringLiteral("/model"), nullptr, &session, nullptr, nullptr);
        QVERIFY(handled);
        QCOMPARE(session.messages().size(), 1);
        QVERIFY(session.messages().at(0).text.contains(origModel));

        // Switch model for this session only
        handled = SlashCommandRouter::execute(QStringLiteral("/model custom-claude-test"), nullptr, &session, nullptr, nullptr);
        QVERIFY(handled);
        QCOMPARE(session.model(), QStringLiteral("custom-claude-test"));
        QCOMPARE(session.messages().size(), 2);
        QVERIFY(session.messages().at(1).text.contains(QStringLiteral("custom-claude-test")));

        // Verify global settings remain untouched (scoped to active session)
        QCOMPARE(AgySettings::instance()->model, globalDefault);
    }

    void testSlashCommandProject()
    {
        ChatSession session;
        bool handled = SlashCommandRouter::execute(QStringLiteral("/project"), nullptr, &session, nullptr, nullptr);
        QVERIFY(handled);
        QCOMPARE(session.messages().size(), 1);
        QVERIFY(session.messages().at(0).text.contains(QStringLiteral("📁")));
    }

    void testMentionResolution()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Create sample files
        QDir root(tempDir.path());
        root.mkdir(QStringLiteral("src"));

        QFile f1(root.filePath(QStringLiteral("main.py")));
        QVERIFY(f1.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out1(&f1);
        out1 << "print('hello world')\n";
        f1.close();

        QFile f2(root.filePath(QStringLiteral("src/util.cpp")));
        QVERIFY(f2.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out2(&f2);
        out2 << "int add(int a, int b) { return a + b; }\n";
        f2.close();

        // Resolve mentions
        const QString prompt = QStringLiteral("Please explain @main.py and @src/util.cpp to me.");
        MentionResolution res = MentionResolver::resolveMentions(prompt, tempDir.path(), nullptr);

        QCOMPARE(res.resolvedFiles.size(), 2);
        QVERIFY(res.resolvedFiles.contains(QStringLiteral("main.py")));
        QVERIFY(res.resolvedFiles.contains(QStringLiteral("src/util.cpp")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("#### Mentioned File: main.py")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("print('hello world')")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("#### Mentioned File: src/util.cpp")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("int add(int a, int b)")));
        QVERIFY(res.warnings.isEmpty());
    }

    void testMentionWarningsAndAlerts()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // 1. Non-existent file alerts user
        MentionResolution emptyRes = MentionResolver::resolveMentions(QStringLiteral("Check @nonexistent.txt"), tempDir.path(), nullptr);
        QVERIFY(emptyRes.resolvedFiles.isEmpty());
        QVERIFY(emptyRes.assembledContext.isEmpty());
        QCOMPARE(emptyRes.warnings.size(), 1);
        QVERIFY(emptyRes.warnings.first().contains(QStringLiteral("@nonexistent.txt")));

        // 2. Truncated file alert
        QDir root(tempDir.path());
        QFile bigFile(root.filePath(QStringLiteral("big.txt")));
        QVERIFY(bigFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&bigFile);
        for (int i = 0; i < 600; ++i) {
            out << "Line number " << i << " with some long text to fill up bytes\n";
        }
        bigFile.close();

        MentionResolution bigRes = MentionResolver::resolveMentions(QStringLiteral("Analyze @big.txt"), tempDir.path(), nullptr);
        QCOMPARE(bigRes.resolvedFiles.size(), 1);
        QVERIFY(!bigRes.warnings.isEmpty());
        QVERIFY(bigRes.warnings.first().contains(QStringLiteral("@big.txt")));
        QVERIFY(bigRes.assembledContext.contains(QStringLiteral("[Truncated to 500 lines / 20 KB]")));
    }

    void testMentionFolderTree()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        QDir root(tempDir.path());
        root.mkdir(QStringLiteral("components"));

        QFile f1(root.filePath(QStringLiteral("components/Button.tsx")));
        QVERIFY(f1.open(QIODevice::WriteOnly | QIODevice::Text));
        f1.write("export const Button = () => null;");
        f1.close();

        QFile f2(root.filePath(QStringLiteral("components/Input.tsx")));
        QVERIFY(f2.open(QIODevice::WriteOnly | QIODevice::Text));
        f2.write("export const Input = () => null;");
        f2.close();

        MentionResolution res = MentionResolver::resolveMentions(QStringLiteral("Check @components/ folder"), tempDir.path(), nullptr);
        QCOMPARE(res.resolvedFiles.size(), 1);
        QVERIFY(res.assembledContext.contains(QStringLiteral("#### Mentioned Folder: components/ (Directory Tree)")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("Button.tsx")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("Input.tsx")));
    }

    void testMentionHtmlFormatting()
    {
        const QString input = QStringLiteral("Check @src/util.cpp and @README.md and @components/");
        const QString html = MentionResolver::formatMentionsInHtml(input);

        QVERIFY(html.contains(QStringLiteral("href=\"kateagy://openfile/src/util.cpp\"")));
        QVERIFY(html.contains(QStringLiteral(">@src/util.cpp</a>")));
        QVERIFY(html.contains(QStringLiteral("href=\"kateagy://openfile/README.md\"")));
        QVERIFY(html.contains(QStringLiteral(">@README.md</a>")));
        QVERIFY(html.contains(QStringLiteral("href=\"kateagy://openfile/components/\"")));
        QVERIFY(html.contains(QStringLiteral(">@components/</a>")));
    }

    void testProjectFileIndexerFiltering()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        QDir root(tempDir.path());
        root.mkdir(QStringLiteral("src"));
        root.mkdir(QStringLiteral(".git"));
        root.mkdir(QStringLiteral("node_modules"));
        root.mkdir(QStringLiteral("build"));

        auto createFile = [&root](const QString &relPath) {
            QFile f(root.filePath(relPath));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write("content");
                f.close();
            }
        };

        createFile(QStringLiteral("README.md"));
        createFile(QStringLiteral("src/app.cpp"));
        createFile(QStringLiteral("src/app.h"));
        createFile(QStringLiteral(".git/config"));
        createFile(QStringLiteral("node_modules/pkg.json"));
        createFile(QStringLiteral("build/test.o"));
        createFile(QStringLiteral("image.png"));

        ProjectFileIndexer *indexer = ProjectFileIndexer::instance();
        indexer->clearCache();
        indexer->scanProjectSync(tempDir.path());

        auto allFiles = indexer->searchFiles(tempDir.path(), QString(), nullptr, 50);

        QStringList relPaths;
        bool foundSrcDir = false;
        for (const auto &item : allFiles) {
            relPaths << item.relativePath;
            if (item.isDirectory && item.relativePath == QStringLiteral("src/")) {
                foundSrcDir = true;
            }
        }

        QVERIFY(relPaths.contains(QStringLiteral("README.md")));
        QVERIFY(relPaths.contains(QStringLiteral("src/app.cpp")));
        QVERIFY(relPaths.contains(QStringLiteral("src/app.h")));
        QVERIFY(foundSrcDir);

        // Ignored files and dirs
        QVERIFY(!relPaths.contains(QStringLiteral(".git/config")));
        QVERIFY(!relPaths.contains(QStringLiteral("node_modules/pkg.json")));
        QVERIFY(!relPaths.contains(QStringLiteral("build/test.o")));
        QVERIFY(!relPaths.contains(QStringLiteral("image.png")));

        // Query filter
        auto filtered = indexer->searchFiles(tempDir.path(), QStringLiteral("app.h"), nullptr, 50);
        QCOMPARE(filtered.size(), 1);
        QCOMPARE(filtered.first().fileName, QStringLiteral("app.h"));
    }

    void testSlashCommandExport()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        ChatSession session;
        session.setProject(tempDir.path(), QStringLiteral("TestProject"));
        session.sendMessage(QStringLiteral("Hello assistant"));
        ChatMessage assistantMsg;
        assistantMsg.role = ChatMessage::Role::Assistant;
        assistantMsg.text = QStringLiteral("Hello developer! Here is some code:\n```cpp\nint a = 1;\n```");
        session.addSystemMessage(assistantMsg.text);

        const QString exportFile = tempDir.filePath(QStringLiteral("chat_export.md"));
        bool handled = SlashCommandRouter::execute(QStringLiteral("/export %1").arg(exportFile), nullptr, &session, nullptr, nullptr);
        QVERIFY(handled);

        QFile f(exportFile);
        QVERIFY(f.exists());
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString exportedContent = QString::fromUtf8(f.readAll());
        QVERIFY(exportedContent.contains(QStringLiteral("# Antigravity Chat Export")));
        QVERIFY(exportedContent.contains(QStringLiteral("TestProject")));
        QVERIFY(exportedContent.contains(QStringLiteral("Hello assistant")));
        QVERIFY(exportedContent.contains(QStringLiteral("int a = 1;")));
    }

    void testDiagnosticsMention()
    {
        MentionResolution res = MentionResolver::resolveMentions(QStringLiteral("Fix @diagnostics now"), QString(), nullptr);
        QCOMPARE(res.resolvedFiles.size(), 1);
        QVERIFY(res.resolvedFiles.contains(QStringLiteral("@diagnostics")));
        QVERIFY(res.assembledContext.contains(QStringLiteral("Editor Diagnostics")));
    }

    void testUsageCommandWithTokensAndQuota()
    {
        ChatSession session;
        session.setProject(QStringLiteral("/tmp"), QStringLiteral("TestProj"));

        // Simulate token usage serialization
        QJsonObject sessionObj = session.toJson();
        QJsonObject usageObj;
        usageObj.insert(QStringLiteral("lastInputTokens"), 1234);
        usageObj.insert(QStringLiteral("lastOutputTokens"), 56);
        usageObj.insert(QStringLiteral("lastTotalTokens"), 1290);
        usageObj.insert(QStringLiteral("totalInputTokens"), 5678);
        usageObj.insert(QStringLiteral("totalOutputTokens"), 210);
        usageObj.insert(QStringLiteral("totalTokens"), 5888);
        sessionObj.insert(QStringLiteral("tokenUsage"), usageObj);

        session.fromJson(sessionObj);
        QCOMPARE(session.tokenUsage().lastInputTokens, 1234);
        QCOMPARE(session.tokenUsage().totalTokens, 5888);

        bool handled = SlashCommandRouter::execute(QStringLiteral("/usage"), nullptr, &session, nullptr, nullptr);
        QVERIFY(handled);
        QVERIFY(!session.messages().isEmpty());
        const QString msg = session.messages().last().text;
        QVERIFY(msg.contains(QStringLiteral("📊")));
        QVERIFY(msg.contains(QStringLiteral("🔢")));
        QVERIFY(msg.contains(QStringLiteral("1234")) || msg.contains(QStringLiteral("1.234")) || msg.contains(QStringLiteral("1,234")));
        QVERIFY(msg.contains(QStringLiteral("5888")) || msg.contains(QStringLiteral("5.888")) || msg.contains(QStringLiteral("5,888")));
    }
};

QTEST_MAIN(TestSlashAndMentions)
#include "test_slashandmentions.moc"
