#include "agyinfodialog.h"
#include "chatsession.h"
#include "agyaccount.h"
#include "slashcommandrouter.h"
#include "projectdetector.h"
#include "settings.h"
#include "tokenstats.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QIcon>
#include <QEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QLocale>
#include <KLocalizedString>

static QString formatRelativeTime(const QDateTime &targetUtc)
{
    if (!targetUtc.isValid()) {
        return QStringLiteral("N/A");
    }

    const qint64 diffSecs = QDateTime::currentDateTimeUtc().secsTo(targetUtc);
    if (diffSecs <= 0) {
        return i18n("ready to reset");
    }

    const qint64 days = diffSecs / 86400;
    const qint64 hours = (diffSecs % 86400) / 3600;
    const qint64 minutes = (diffSecs % 3600) / 60;

    if (days > 0) {
        if (hours > 0) {
            return i18n("in %1 d, %2 h", days, hours);
        }
        return i18n("in %1 d", days);
    }
    if (hours > 0) {
        if (minutes > 0) {
            return i18n("in %1 h, %2 min", hours, minutes);
        }
        return i18n("in %1 h", hours);
    }
    return i18n("in %1 min", qMax(1LL, minutes));
}

AgyInfoDialog::AgyInfoDialog(DialogType type, ChatSession *session, QWidget *parent)
    : QDialog(parent)
    , m_type(type)
    , m_session(session)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setupUi();
    refreshContent();
}

void AgyInfoDialog::showHelp(QWidget *parent)
{
    auto *dlg = new AgyInfoDialog(DialogType::Help, nullptr, parent);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void AgyInfoDialog::showUsage(ChatSession *session, QWidget *parent)
{
    auto *dlg = new AgyInfoDialog(DialogType::Usage, session, parent);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void AgyInfoDialog::showProject(ChatSession *session, QWidget *parent)
{
    auto *dlg = new AgyInfoDialog(DialogType::Project, session, parent);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void AgyInfoDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // 1. Header with Icon & Title
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    auto *iconLabel = new QLabel(this);
    iconLabel->setFixedSize(32, 32);

    auto *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);

    auto *titleLabel = new QLabel(this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: bold; color: palette(text);"));

    auto *subtitleLabel = new QLabel(this);
    subtitleLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: palette(placeholder-text);"));

    switch (m_type) {
    case DialogType::Help:
        setWindowTitle(i18n("Antigravity AI — Help & Slash Commands"));
        iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("help-about"), QIcon::fromTheme(QStringLiteral("help-contents"))).pixmap(32, 32));
        titleLabel->setText(i18n("Antigravity AI Commands & Shortcuts"));
        subtitleLabel->setText(i18n("Quick reference for commands, mentions, and navigation in Kate"));
        resize(560, 480);
        break;
    case DialogType::Usage:
        setWindowTitle(i18n("Antigravity AI — Session Statistics & Quotas"));
        iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("user-identity"), QIcon::fromTheme(QStringLiteral("dialog-information"))).pixmap(32, 32));
        titleLabel->setText(i18n("Account, Token Usage & Quotas"));
        subtitleLabel->setText(i18n("Current Google AI session status, token metrics and rate limits"));
        resize(580, 520);
        break;
    case DialogType::Project:
        setWindowTitle(i18n("Antigravity AI — Project Information"));
        iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("folder-development"), QIcon::fromTheme(QStringLiteral("project-development"))).pixmap(32, 32));
        titleLabel->setText(i18n("Active Workspace Project"));
        subtitleLabel->setText(i18n("Details and Git repository status for the current session"));
        resize(520, 360);
        break;
    }

    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(subtitleLabel);

    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);
    headerLayout->addLayout(titleLayout, 1);
    mainLayout->addLayout(headerLayout);

    // 2. Text Browser (rendered with native KDE palette styles)
    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(true);
    m_browser->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 6px;"
        "  background-color: palette(base);"
        "  padding: 8px;"
        "}"
    ));
    m_browser->document()->setDefaultStyleSheet(
        QStringLiteral(
            "body { font-family: sans-serif; font-size: 12px; color: palette(text); }\n"
            "h2, h3, h4 { color: palette(text); margin-top: 10px; margin-bottom: 6px; }\n"
            "table { background-color: palette(mid); margin: 8px 0; }\n"
            "th { background-color: palette(midlight); color: palette(text); font-weight: bold; padding: 6px 8px; text-align: left; }\n"
            "td { background-color: palette(base); color: palette(text); padding: 5px 8px; }\n"
            "code { font-family: monospace; font-size: 11px; background-color: palette(alternate-base); padding: 1px 4px; border-radius: 3px; }\n"
            "a { color: palette(highlight); text-decoration: none; }\n"
            "a:hover { text-decoration: underline; }\n"
        )
    );
    mainLayout->addWidget(m_browser, 1);

    // 3. Dialog Button Box
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    if (m_type == DialogType::Usage) {
        m_refreshButton = m_buttonBox->addButton(i18n("Refresh"), QDialogButtonBox::ActionRole);
        m_refreshButton->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
        connect(m_refreshButton, &QPushButton::clicked, this, &AgyInfoDialog::refreshContent);
    }

    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainLayout->addWidget(m_buttonBox);
}

void AgyInfoDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::ThemeChange
        || event->type() == QEvent::StyleChange) {
        refreshContent();
    }
}

void AgyInfoDialog::refreshContent()
{
    if (m_browser) {
        m_browser->setHtml(buildHtmlContent());
    }
}

QString AgyInfoDialog::buildHtmlContent() const
{
    QString html = QStringLiteral("<html><body>\n");

    if (m_type == DialogType::Help) {
        html += QStringLiteral(
            "<h3>%1</h3>\n"
            "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"1\">\n"
            "<tr><th width=\"25%\">%2</th><th width=\"35%\">%3</th><th width=\"40%\">%4</th></tr>\n"
        ).arg(
            i18n("Slash Commands"),
            i18n("Command"),
            i18n("Syntax"),
            i18n("Description")
        );

        const auto commands = SlashCommandRouter::availableCommands();
        for (const auto &cmd : commands) {
            html += QStringLiteral("<tr><td><code>/%1</code></td><td><code>%2</code></td><td>%3</td></tr>\n")
                .arg(cmd.name, cmd.syntax, cmd.description);
        }
        html += QStringLiteral("</table>\n");

        html += QStringLiteral(
            "<h3>%1</h3>\n"
            "<ul>\n"
            "<li><code>@filename</code>: %2</li>\n"
            "<li><code>@folder/</code>: %3</li>\n"
            "<li><code>@diagnostics</code> (%4): %5</li>\n"
            "</ul>\n"
            "<h3>%6</h3>\n"
            "<ul>\n"
            "<li><b>Ctrl+Enter</b>: %7</li>\n"
            "<li><b>Enter</b>: %8</li>\n"
            "<li><b>Ctrl+L</b>: %9</li>\n"
            "<li><b>Tab / Enter</b>: %10</li>\n"
            "</ul>\n"
        ).arg(
            i18n("Smart Context Mentions (@)"),
            i18n("Attach file content or selection to prompt"),
            i18n("Attach folder directory tree structure"),
            i18n("@errors"),
            i18n("Attach active compiler warnings & LSP diagnostic marks"),
            i18n("Keyboard Shortcuts"),
            i18n("Send message / prompt"),
            i18n("Execute slash command immediately"),
            i18n("Clear visible chat display"),
            i18n("Accept popup completion item / ghost text")
        );

    } else if (m_type == DialogType::Usage) {
        const AgyAccountInfo account = AgyAccount::currentAccount();
        const QString accountStr = account.isAuthenticated
            ? (account.name.isEmpty() ? QStringLiteral("<code>%1</code>").arg(account.email) : QStringLiteral("<code>%1</code> (%2)").arg(account.email, account.name))
            : i18n("Not authenticated");

        const QString modeStr = (m_session && m_session->backendMode() == AgyClient::BackendMode::AgyCli)
            ? QStringLiteral("Antigravity CLI")
            : QStringLiteral("Direct Gemini API");

        html += QStringLiteral(
            "<h3>%1</h3>\n"
            "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"1\">\n"
            "<tr><td width=\"40%\"><b>%2</b></td><td>%3</td></tr>\n"
            "<tr><td><b>%4</b></td><td>%5</td></tr>\n"
            "<tr><td><b>%6</b></td><td>%7</td></tr>\n"
            "<tr><td><b>%8</b></td><td><code>%9</code></td></tr>\n"
            "<tr><td><b>%10</b></td><td>%11</td></tr>\n"
            "<tr><td><b>%12</b></td><td>%13</td></tr>\n"
            "</table>\n"
        ).arg(
            i18n("Session & Account"),
            i18n("Google AI Account"), accountStr,
            i18n("Auth Method"), account.authMethod.isEmpty() ? QStringLiteral("N/A") : account.authMethod,
            i18n("Active Project"), (m_session && !m_session->projectName().isEmpty()) ? m_session->projectName() : i18n("General"),
            i18n("Current Model"), m_session ? m_session->model() : QStringLiteral("gemini-3.8-flash-low"),
            i18n("Backend Mode"), modeStr,
            i18n("Messages in Session"), m_session ? QString::number(m_session->messages().size()) : QStringLiteral("0")
        );

        // Tokens table
        const auto tu = m_session ? m_session->tokenUsage() : ChatSession::TokenUsage{};
        const QLocale locale;

        html += QStringLiteral(
            "<h3>%1</h3>\n"
            "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"1\">\n"
            "<tr><th>%2</th><th style=\"text-align: center;\">%3</th><th style=\"text-align: center;\">%4</th></tr>\n"
            "<tr><td><b>%5</b></td><td align=\"center\">%6</td><td align=\"center\">%7</td></tr>\n"
            "<tr><td><b>%8</b></td><td align=\"center\">%9</td><td align=\"center\">%10</td></tr>\n"
            "<tr><td><b>%11</b></td><td align=\"center\">%12</td><td align=\"center\">%13</td></tr>\n"
            "<tr style=\"background-color: palette(alternate-base);\"><td><b>%14</b></td><td align=\"center\"><b>%15</b></td><td align=\"center\"><b>%16</b></td></tr>\n"
            "</table>\n"
        ).arg(
            i18n("Token Usage"),
            i18n("Metric"), i18n("Last Turn"), i18n("Session Cumulative"),
            i18n("Input Tokens"), locale.toString(tu.lastInputTokens), locale.toString(tu.totalInputTokens),
            i18n("Output Tokens"), locale.toString(tu.lastOutputTokens), locale.toString(tu.totalOutputTokens),
            i18n("Thinking Tokens"), locale.toString(tu.lastThinkingTokens), locale.toString(tu.totalThinkingTokens),
            i18n("Total Tokens"), locale.toString(tu.lastTotalTokens), locale.toString(tu.totalTokens)
        );

        // All-time token statistics (cross-session, persisted).
        {
            TokenStats *stats = TokenStats::instance();
            html += QStringLiteral(
                "<h3>%1</h3>\n"
                "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"1\">\n"
                "<tr><td width=\"40%\"><b>%2</b></td><td>%3</td></tr>\n"
                "<tr><td><b>%4</b></td><td>%5</td></tr>\n"
                "<tr><td><b>%6</b></td><td>%7</td></tr>\n"
                "<tr><td><b>%8</b></td><td>%9</td></tr>\n"
                "</table>\n"
            ).arg(
                i18n("All-Time Usage"),
                i18n("Total Tokens"), locale.toString(stats->allTimeTotal()),
                i18n("Input Tokens"), locale.toString(stats->allTimeInput()),
                i18n("Output Tokens"), locale.toString(stats->allTimeOutput()),
                i18n("Turns Recorded"), locale.toString(stats->turnsRecorded())
            );

            const auto days = stats->recentDays(7);
            if (!days.isEmpty()) {
                html += QStringLiteral(
                    "<h3>%1</h3>\n"
                    "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"1\">\n"
                    "<tr><th>%2</th><th style=\"text-align:right;\">%3</th><th style=\"text-align:right;\">%4</th><th style=\"text-align:right;\">%5</th></tr>\n"
                ).arg(i18n("Recent Daily Usage"), i18n("Date"),
                      i18n("Input"), i18n("Output"), i18n("Total"));
                for (const auto &d : days) {
                    html += QStringLiteral("<tr><td>%1</td><td align=\"right\">%2</td><td align=\"right\">%3</td><td align=\"right\">%4</td></tr>\n")
                        .arg(d.date.toString(Qt::ISODate),
                             locale.toString(d.inputTokens),
                             locale.toString(d.outputTokens),
                             locale.toString(d.totalTokens));
                }
                html += QStringLiteral("</table>\n");
            }
        }

        // Quotas table
        const auto quotas = AgyAccount::fetchQuotaLimits();
        if (!quotas.isEmpty()) {
            html += QStringLiteral(
                "<h3>%1</h3>\n"
                "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"1\">\n"
                "<tr><th>%2</th><th>%3</th><th style=\"text-align: center;\">%4</th><th>%5</th></tr>\n"
            ).arg(
                i18n("Quotas & Rate Limits"),
                i18n("Model Group"),
                i18n("Limit Window"),
                i18n("Remaining"),
                i18n("Estimated Reset")
            );

            for (const auto &q : quotas) {
                const QString indicator = (q.remainingPercent >= 50)
                    ? QStringLiteral("🟩")
                    : (q.remainingPercent >= 20 ? QStringLiteral("🟨") : QStringLiteral("🟥"));

                const QString resetRel = formatRelativeTime(q.resetTime);
                const QString resetFormatted = q.resetTime.isValid()
                    ? QStringLiteral("%1 (<code>%2 UTC</code>)").arg(resetRel, q.resetTime.toString(QStringLiteral("yyyy-MM-dd hh:mm")))
                    : q.rawResetTime;

                html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td><td align=\"center\"><b>%3%</b> %4</td><td>%5</td></tr>\n")
                    .arg(q.modelGroup, q.limitName, QString::number(q.remainingPercent), indicator, resetFormatted);
            }
            html += QStringLiteral("</table>\n");
        }

    } else if (m_type == DialogType::Project) {
        const QString name = (m_session && !m_session->projectName().isEmpty()) ? m_session->projectName() : i18n("General");
        const QString root = (m_session && !m_session->projectPath().isEmpty()) ? m_session->projectPath() : i18n("None (General Workspace)");
        const QString branch = (m_session && !m_session->projectPath().isEmpty())
            ? ProjectDetector::detectForPath(m_session->projectPath()).gitBranch
            : QStringLiteral("N/A");

        html += QStringLiteral(
            "<h3>%1</h3>\n"
            "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"1\">\n"
            "<tr><td width=\"35%\"><b>%2</b></td><td><b>%3</b></td></tr>\n"
            "<tr><td><b>%4</b></td><td><code>%5</code></td></tr>\n"
            "<tr><td><b>%6</b></td><td><code>%7</code></td></tr>\n"
            "<tr><td><b>%8</b></td><td>%9 %10</td></tr>\n"
            "</table>\n"
        ).arg(
            i18n("Project Information"),
            i18n("Project Name"), name,
            i18n("Root Directory"), root,
            i18n("Git Branch"), branch.isEmpty() ? i18n("Not a git repository") : branch,
            i18n("Session Messages"), QString::number(m_session ? m_session->messages().size() : 0), i18n("messages")
        );
    }

    html += QStringLiteral("</body></html>\n");
    return html;
}
