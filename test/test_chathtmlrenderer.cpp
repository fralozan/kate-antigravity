#include <QTest>
#include "chathtmlrenderer.h"
#include "chatsession.h"

class TestChatHtmlRenderer : public QObject
{
    Q_OBJECT

private:
    static ChatMessage msg(ChatMessage::Role role, const QString &text)
    {
        ChatMessage m;
        m.role = role;
        m.text = text;
        m.timestamp = QDateTime::currentDateTime();
        return m;
    }

private Q_SLOTS:
    void testEmptyState()
    {
        ChatHtmlRenderer renderer;
        const QString html = renderer.renderConversation({}, ChatHtmlRenderer::Options{0});
        QVERIFY(html.contains(QLatin1String("<html>")));
        QVERIFY(html.contains(QLatin1String("Antigravity AI")));
        QVERIFY(renderer.snippets().isEmpty());
    }

    void testRendersUserAndAssistant()
    {
        ChatHtmlRenderer renderer;
        QList<ChatMessage> messages = {
            msg(ChatMessage::Role::User, QStringLiteral("Hello")),
            msg(ChatMessage::Role::Assistant, QStringLiteral("Hi there")),
        };
        const QString html = renderer.renderConversation(messages, ChatHtmlRenderer::Options{0});
        QVERIFY(html.contains(QLatin1String("Hello")));
        QVERIFY(html.contains(QLatin1String("Hi there")));
    }

    void testCodeBlockRegistersSnippet()
    {
        ChatHtmlRenderer renderer;
        const QString body = QStringLiteral("Here:\n```cpp\nint x = 42;\n```\ndone");
        QList<ChatMessage> messages = { msg(ChatMessage::Role::Assistant, body) };
        const QString html = renderer.renderConversation(messages, ChatHtmlRenderer::Options{0});

        // One fenced block -> one snippet with the raw code preserved.
        QCOMPARE(renderer.snippets().size(), 1);
        QCOMPARE(renderer.snippets().value(1), QStringLiteral("int x = 42;"));
        // Copy/insert/replace links reference snippet id 1.
        QVERIFY(html.contains(QLatin1String("kateagy://copy/1")));
        QVERIFY(html.contains(QLatin1String("kateagy://insert/1")));
        QVERIFY(html.contains(QLatin1String("kateagy://replace/1")));
    }

    void testHiddenMessagesNotice()
    {
        ChatHtmlRenderer renderer;
        QList<ChatMessage> messages = {
            msg(ChatMessage::Role::User, QStringLiteral("old")),
            msg(ChatMessage::Role::Assistant, QStringLiteral("older reply")),
            msg(ChatMessage::Role::User, QStringLiteral("recent")),
        };
        // Hide the first two messages.
        const QString html = renderer.renderConversation(messages, ChatHtmlRenderer::Options{2});
        QVERIFY(html.contains(QLatin1String("show-all-history")));
        QVERIFY(html.contains(QLatin1String("recent")));
        // Hidden content should not appear.
        QVERIFY(!html.contains(QLatin1String("older reply")));
    }

    void testIncrementalMatchesFull()
    {
        // Simulate the widget's incremental streaming path: stable prefix (all
        // but the last message) + the last message, and compare against a full
        // render of the same messages.
        QList<ChatMessage> messages = {
            msg(ChatMessage::Role::User, QStringLiteral("Question")),
            msg(ChatMessage::Role::Assistant, QStringLiteral("Partial answer with `code`")),
        };

        ChatHtmlRenderer full;
        const QString fullHtml = full.renderConversation(messages, ChatHtmlRenderer::Options{0});

        ChatHtmlRenderer incremental;
        incremental.resetSnippets();
        const QString prefix = incremental.renderMessageRange(messages.mid(0, messages.size() - 1), 0);
        const QString last = incremental.renderMessageRange(messages, messages.size() - 1);
        const QString incrementalHtml = ChatHtmlRenderer::wrapDocument(prefix + last);

        QCOMPARE(incrementalHtml, fullHtml);
    }
};

QTEST_MAIN(TestChatHtmlRenderer)
#include "test_chathtmlrenderer.moc"
