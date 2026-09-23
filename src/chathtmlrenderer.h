#ifndef AGY_CHATHTMLRENDERER_H
#define AGY_CHATHTMLRENDERER_H

#include <QString>
#include <QList>
#include <QMap>
#include "chatsession.h" // ChatMessage

// Turns chat messages into the HTML shown in the chat QTextBrowser.
//
// Extracted from ChatWidget so the (non-trivial) markdown/code-block rendering
// can be unit tested and reused without a widget. The renderer is stateful only
// in that it assigns incrementing snippet ids to fenced code blocks and records
// their raw text, so the widget can resolve copy/insert/replace links.
//
// It also supports incremental streaming rendering: the caller renders the
// stable prefix of the conversation once, then re-renders only the final
// (streaming) message on each delta instead of the whole history.
class ChatHtmlRenderer
{
public:
    struct Options {
        // Index of the first visible message; messages before it are "hidden"
        // (context preserved) and summarised with a restore link.
        int visibleStartIndex = 0;
        // When non-empty, only messages containing this text (case-insensitive)
        // are rendered; the streaming/hidden-notice logic is bypassed.
        QString searchQuery;
    };

    ChatHtmlRenderer();

    // Full conversation render (empty-state banner, hidden-messages notice, and
    // every visible message). Resets and repopulates the snippet table.
    QString renderConversation(const QList<ChatMessage> &messages, const Options &opts);

    // Render just the message blocks for indices [from, messages.size()).
    // Does NOT reset the snippet table, so snippet ids stay stable across an
    // incremental streaming update. Used to re-render only the last message.
    QString renderMessageRange(const QList<ChatMessage> &messages, int from);

    // Wrap body HTML in the <html><body> shell used by the browser.
    static QString wrapDocument(const QString &bodyHtml);

    // Snippet id -> raw code, populated during rendering of fenced code blocks.
    const QMap<int, QString> &snippets() const { return m_snippets; }
    void resetSnippets();

private:
    QString renderMessage(const ChatMessage &msg, int messageIndex);
    QString formatMarkdownChunk(const QString &rawText);
    QString extractCodeBlocks(const QString &rawMarkdown);
    QString emptyStateHtml(int visibleStartIndex) const;
    QString hiddenMessagesNotice(int visibleStartIndex) const;

    QMap<int, QString> m_snippets;
    int m_snippetCounter = 0;
};

#endif // AGY_CHATHTMLRENDERER_H
