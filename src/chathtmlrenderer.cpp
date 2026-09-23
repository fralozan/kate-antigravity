#include "chathtmlrenderer.h"
#include "agyaccount.h"
#include "mentionresolver.h"
#include "editblockparser.h"

#include <QRegularExpression>
#include <KLocalizedString>

ChatHtmlRenderer::ChatHtmlRenderer() = default;

void ChatHtmlRenderer::resetSnippets()
{
    m_snippets.clear();
    m_snippetCounter = 0;
}

QString ChatHtmlRenderer::wrapDocument(const QString &bodyHtml)
{
    return QStringLiteral("<html><body style=\"margin: 0; background-color: transparent;\">\n")
        + bodyHtml
        + QStringLiteral("</body></html>");
}

QString ChatHtmlRenderer::formatMarkdownChunk(const QString &rawText)
{
    QString text = rawText.toHtmlEscaped();

    // Inline code: `code`
    static const QRegularExpression inlineCodeRegex(QStringLiteral("`([^`\n]+)`"));
    text.replace(inlineCodeRegex, QStringLiteral("<code style=\"font-family: monospace; font-size: 11px; background-color: palette(midlight); padding: 1px 4px; border-radius: 3px;\">\\1</code>"));

    // Bold: **text**
    static const QRegularExpression boldRegex(QStringLiteral("\\*\\*([^*]+)\\*\\*"));
    text.replace(boldRegex, QStringLiteral("<b>\\1</b>"));

    // Links: [label](url)
    static const QRegularExpression linkRegex(QStringLiteral("\\[([^\\]]+)\\]\\(([^\\)]+)\\)"));
    text.replace(linkRegex, QStringLiteral("<a href=\"\\2\" style=\"color: palette(highlight); text-decoration: underline;\">\\1</a>"));

    // Line breaks
    text.replace(QLatin1Char('\n'), QStringLiteral("<br>"));

    return text;
}

QString ChatHtmlRenderer::extractCodeBlocks(const QString &rawMarkdown)
{
    QString result;
    int lastPos = 0;

    static const QRegularExpression codeRegex(QStringLiteral("```([a-zA-Z0-9_-]*)[\\r\\n](.*?)[\\r\\n]```"),
                                              QRegularExpression::DotMatchesEverythingOption);

    auto iterator = codeRegex.globalMatch(rawMarkdown);
    while (iterator.hasNext()) {
        auto match = iterator.next();
        const int matchStart = match.capturedStart();
        const int matchEnd = match.capturedEnd();

        if (matchStart > lastPos) {
            result += formatMarkdownChunk(rawMarkdown.mid(lastPos, matchStart - lastPos));
        }

        const QString lang = match.captured(1).trimmed().isEmpty() ? QStringLiteral("code") : match.captured(1).trimmed();
        const QString code = match.captured(2);

        m_snippetCounter++;
        const int snippetId = m_snippetCounter;
        m_snippets.insert(snippetId, code);

        result += QStringLiteral(
            "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" style=\"margin: 6px 0; border: 1px solid palette(mid); border-radius: 4px; background-color: palette(alternate-base);\">"
            "<tr><td style=\"background-color: palette(midlight); border-bottom: 1px solid palette(mid); padding: 4px 8px;\">"
            "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\"><tr>"
            "<td align=\"left\" style=\"font-size: 11px; font-weight: bold; color: palette(text);\">%1</td>"
            "<td align=\"right\" style=\"font-size: 11px;\">"
            "<a href=\"kateagy://copy/%2\" style=\"color: palette(highlight); text-decoration: none;\">%4</a>&nbsp;&nbsp;"
            "<a href=\"kateagy://insert/%2\" style=\"color: palette(highlight); text-decoration: none;\">%5</a>&nbsp;&nbsp;"
            "<a href=\"kateagy://replace/%2\" style=\"color: palette(highlight); text-decoration: none;\">%6</a>"
            "</td>"
            "</tr></table>"
            "</td></tr>"
            "<tr><td style=\"padding: 6px 8px;\">"
            "<pre style=\"margin: 0; font-family: monospace; font-size: 12px; color: palette(text); background: transparent; white-space: pre-wrap;\">%3</pre>"
            "</td></tr>"
            "</table>"
        ).arg(lang, QString::number(snippetId), code.toHtmlEscaped(),
              i18n("📋 Copy"), i18n("📥 Insert"), i18n("🔄 Replace"));

        lastPos = matchEnd;
    }

    if (lastPos < rawMarkdown.length()) {
        result += formatMarkdownChunk(rawMarkdown.mid(lastPos));
    }

    return result;
}

QString ChatHtmlRenderer::renderMessage(const ChatMessage &msg, int messageIndex)
{
    if (msg.role == ChatMessage::Role::User) {
        const AgyAccountInfo account = AgyAccount::currentAccount();
        const QString userTitle = (account.isAuthenticated && !account.email.isEmpty())
            ? QStringLiteral("👤 %1:").arg(account.email)
            : i18n("👤 You:");

        const QString userEscaped = msg.text.trimmed().toHtmlEscaped();
        const QString userBody = MentionResolver::formatMentionsInHtml(userEscaped).replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        QString metaHtml;
        if (!msg.contextMeta.isEmpty()) {
            metaHtml = QStringLiteral("<div style=\"font-size: 10px; color: palette(placeholder-text); margin-top: 4px;\">📎 %1</div>\n").arg(msg.contextMeta.toHtmlEscaped());
        }

        return QStringLiteral(
            "<table width=\"100%\" cellpadding=\"8\" cellspacing=\"0\" style=\"margin-top: 4px; margin-bottom: 6px; background-color: palette(alternate-base); border: 1px solid palette(midlight); border-radius: 6px;\">\n"
            "  <tr><td style=\"padding: 8px 10px;\">\n"
            "    <div style=\"font-weight: bold; font-size: 11px; color: palette(highlight); margin: 0 0 2px 0;\">%1</div>\n"
            "    <div style=\"color: palette(text); font-size: 13px; margin: 0; line-height: 1.35;\">%2</div>\n"
            "    %3"
            "  </td></tr>\n"
            "</table>\n"
        ).arg(userTitle, userBody, metaHtml);
    }

    if (msg.role == ChatMessage::Role::System) {
        const QString formattedBody = extractCodeBlocks(msg.text.trimmed());
        return QStringLiteral(
            "<table width=\"100%\" cellpadding=\"8\" cellspacing=\"0\" style=\"margin-top: 4px; margin-bottom: 6px; background-color: palette(alternate-base); border: 1px solid palette(highlight); border-radius: 6px;\">\n"
            "  <tr><td style=\"padding: 8px 10px;\">\n"
            "    <div style=\"font-weight: bold; font-size: 11px; color: palette(highlight); margin: 0 0 2px 0;\">%1</div>\n"
            "    <div style=\"color: palette(text); font-size: 12px; margin: 0; line-height: 1.35;\">%2</div>\n"
            "  </td></tr>\n"
            "</table>\n"
        ).arg(i18n("⚙️ Antigravity:"), formattedBody);
    }

    if (msg.role == ChatMessage::Role::Assistant) {
        if (msg.text.trimmed().isEmpty()) {
            return QStringLiteral(
                "<table width=\"100%\" cellpadding=\"8\" cellspacing=\"0\" style=\"margin-top: 4px; margin-bottom: 6px; background-color: palette(base); border: 1px solid palette(midlight); border-radius: 6px;\">\n"
                "  <tr><td style=\"padding: 8px 10px;\">\n"
                "    <div style=\"font-weight: bold; font-size: 11px; color: #2ecc71; margin: 0 0 2px 0;\">✨ Antigravity:</div>\n"
                "    <div style=\"color: palette(placeholder-text); font-style: italic; font-size: 12px; margin: 0;\">%1</div>\n"
                "  </td></tr>\n"
                "</table>\n"
            ).arg(i18n("Thinking response..."));
        }
        const QString formattedBody = extractCodeBlocks(msg.text.trimmed());

        // If the reply contains search/replace edit blocks, offer an action to
        // review and apply them to the editor.
        QString applyBar;
        if (EditBlockParser::containsEditBlock(msg.text)) {
            applyBar = QStringLiteral(
                "<div style=\"margin-top: 6px; padding-top: 6px; border-top: 1px solid palette(midlight); font-size: 11px;\">"
                "<a href=\"kateagy://apply-edits/%1\" style=\"color: palette(highlight); font-weight: bold; text-decoration: none;\">%2</a>"
                "</div>"
            ).arg(QString::number(messageIndex), i18n("🩹 Review &amp; apply changes"));
        }

        return QStringLiteral(
            "<table width=\"100%\" cellpadding=\"8\" cellspacing=\"0\" style=\"margin-top: 4px; margin-bottom: 6px; background-color: palette(base); border: 1px solid palette(midlight); border-radius: 6px;\">\n"
            "  <tr><td style=\"padding: 8px 10px;\">\n"
            "    <div style=\"font-weight: bold; font-size: 11px; color: #2ecc71; margin: 0 0 2px 0;\">✨ Antigravity:</div>\n"
            "    <div style=\"color: palette(text); font-size: 13px; margin: 0; line-height: 1.35;\">%1</div>\n"
            "    %2"
            "  </td></tr>\n"
            "</table>\n"
        ).arg(formattedBody, applyBar);
    }

    // Error
    return QStringLiteral(
        "<table width=\"100%\" cellpadding=\"8\" cellspacing=\"0\" style=\"margin-top: 4px; margin-bottom: 6px; background-color: palette(alternate-base); border: 1px solid #ef5350; border-radius: 6px;\">\n"
        "  <tr><td style=\"padding: 8px 10px;\">\n"
        "    <div style=\"font-weight: bold; font-size: 11px; color: #ef5350; margin: 0 0 2px 0;\">%1</div>\n"
        "    <div style=\"color: palette(text); font-size: 12px; margin: 0;\">%2</div>\n"
        "  </td></tr>\n"
        "</table>\n"
    ).arg(i18n("⚠️ Error:"), msg.text.trimmed().toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
}

QString ChatHtmlRenderer::emptyStateHtml(int visibleStartIndex) const
{
    if (visibleStartIndex > 0) {
        // Two actions rendered as buttons: restore the hidden messages, or
        // delete them permanently (keeping the assistant's own context).
        const QString restoreBtn = QStringLiteral(
            "<a href=\"kate-agy://show-all-history\" style=\"display: inline-block; padding: 4px 10px; margin: 0 4px; "
            "border: 1px solid palette(highlight); border-radius: 4px; color: palette(highlight); text-decoration: none;\">%1</a>")
            .arg(i18n("Restore hidden messages"));
        const QString purgeBtn = QStringLiteral(
            "<a href=\"kateagy://purge-hidden\" style=\"display: inline-block; padding: 4px 10px; margin: 0 4px; "
            "border: 1px solid #ef5350; border-radius: 4px; color: #ef5350; text-decoration: none;\">%1</a>")
            .arg(i18n("Delete messages permanently"));

        return QStringLiteral(
            "<div style=\"text-align: center; padding: 24px 8px; color: palette(placeholder-text); font-size: 11px;\">\n"
            "  <div style=\"font-size: 20px; margin-bottom: 6px;\">🧹</div>\n"
            "  <div style=\"font-weight: bold; color: palette(text); margin-bottom: 4px;\">%1</div>\n"
            "  <div style=\"margin-bottom: 12px;\">%2</div>\n"
            "  <div>%3&nbsp;%4</div>\n"
            "</div>\n"
        ).arg(
            i18n("Chat display cleared"),
            i18n("The assistant still remembers previous context and messages."),
            restoreBtn, purgeBtn
        );
    }
    return QStringLiteral(
        "<div style=\"text-align: center; padding: 16px 8px; color: palette(placeholder-text);\">\n"
        "  <div style=\"font-size: 22px; margin-bottom: 6px;\">✨</div>\n"
        "  <div style=\"font-size: 13px; font-weight: bold; color: palette(text); margin-bottom: 4px;\">Antigravity AI</div>\n"
        "  <div style=\"font-size: 11px; margin-bottom: 14px; color: palette(text);\">%1</div>\n"
        "  <table width=\"100%\" cellpadding=\"8\" cellspacing=\"0\" style=\"text-align: left; background-color: palette(alternate-base); border: 1px solid palette(midlight); border-radius: 6px; font-size: 11px; color: palette(text);\">\n"
        "    <tr><td style=\"padding: 8px 10px;\">\n"
        "      <div style=\"font-weight: bold; margin-bottom: 4px; color: palette(highlight);\">%2</div>\n"
        "      <div style=\"margin-bottom: 3px;\">%3</div>\n"
        "      <div style=\"margin-bottom: 3px;\">%4</div>\n"
        "      <div>%5</div>\n"
        "    </td></tr>\n"
        "  </table>\n"
        "</div>\n"
    ).arg(i18n("Intelligent code assistant for Kate"),
          i18n("💡 How to get started?"),
          i18n("• Type your question or prompt in the top box."),
          i18n("• Check <b>\"Attach current file\"</b> for context."),
          i18n("• Press <b>Ctrl+Enter</b> to send your message."));
}

QString ChatHtmlRenderer::hiddenMessagesNotice(int visibleStartIndex) const
{
    return QStringLiteral(
        "<div style=\"text-align: center; padding: 6px 4px 8px 4px; color: palette(placeholder-text); font-size: 11px; border-bottom: 1px dashed palette(midlight); margin-bottom: 8px;\">\n"
        "  <span>💬 %1</span> &bull; "
        "<a href=\"kate-agy://show-all-history\" style=\"color: palette(highlight); text-decoration: underline;\">%2</a> &bull; "
        "<a href=\"kateagy://purge-hidden\" style=\"color: #ef5350; text-decoration: underline;\">%3</a>\n"
        "</div>\n"
    ).arg(
        i18n("%1 earlier messages hidden (context active)", QString::number(visibleStartIndex)),
        i18n("Show earlier messages"),
        i18n("Delete permanently")
    );
}

QString ChatHtmlRenderer::renderMessageRange(const QList<ChatMessage> &messages, int from)
{
    QString html;
    for (int i = qMax(0, from); i < messages.size(); ++i) {
        html += renderMessage(messages.at(i), i);
    }
    return html;
}

QString ChatHtmlRenderer::renderConversation(const QList<ChatMessage> &messages, const Options &opts)
{
    resetSnippets();

    // Search mode: render only matching messages across the whole history.
    const QString query = opts.searchQuery.trimmed();
    if (!query.isEmpty()) {
        QString body;
        int matches = 0;
        for (int i = 0; i < messages.size(); ++i) {
            if (messages.at(i).text.contains(query, Qt::CaseInsensitive)) {
                body += renderMessage(messages.at(i), i);
                ++matches;
            }
        }
        if (matches == 0) {
            body = QStringLiteral(
                "<div style=\"text-align:center; padding:16px; color:palette(placeholder-text); font-size:12px;\">%1</div>")
                .arg(i18n("No messages match \"%1\".", query.toHtmlEscaped()));
        } else {
            body.prepend(QStringLiteral(
                "<div style=\"padding:4px 6px; color:palette(placeholder-text); font-size:11px;\">%1</div>")
                .arg(i18n("%1 message(s) matching \"%2\"", QString::number(matches), query.toHtmlEscaped())));
        }
        return wrapDocument(body);
    }

    int visibleStart = opts.visibleStartIndex;
    if (visibleStart > messages.size()) {
        visibleStart = 0;
    }

    const int totalMessages = messages.size();
    const int visibleCount = totalMessages - visibleStart;

    QString body;

    if (totalMessages == 0 || visibleCount == 0) {
        body += emptyStateHtml(visibleStart);
    } else {
        if (visibleStart > 0) {
            body += hiddenMessagesNotice(visibleStart);
        }
        body += renderMessageRange(messages, visibleStart);
    }

    return wrapDocument(body);
}
