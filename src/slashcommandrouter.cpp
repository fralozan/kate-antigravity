#include "slashcommandrouter.h"
#include "chatsession.h"
#include "chatwidget.h"
#include "plugin.h"
#include "projectdetector.h"
#include "settings.h"
#include "agyaccount.h"
#include "agyinfodialog.h"

#include <KLocalizedString>
#include <KTextEditor/MainWindow>
#include <KTextEditor/Editor>
#include <KTextEditor/Document>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QLocale>

namespace {

QString formatRelativeTime(const QDateTime &targetUtc)
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

// --- Command handlers --------------------------------------------------------
// Each handler implements one slash command. They mirror the previous behaviour
// exactly; only the dispatch mechanism changed (registry instead of if/else).

bool cmdHelp(const SlashCommandContext &ctx)
{
    if (ctx.chatWidget) {
        AgyInfoDialog::showHelp(ctx.chatWidget);
        return true;
    }
    QString help = i18n(
        "### 🛠️ Antigravity: Commands & Shortcuts\n\n"
        "| Command | Description |\n"
        "| :--- | :--- |\n"
        "| `/help` | %1 |\n"
        "| `/settings` | %2 |\n"
        "| `/clear` | %3 |\n"
        "| `/reset`, `/new` | %4 |\n"
        "| `/project` | %5 |\n"
        "| `/model [name]` | %6 |\n"
        "| `/usage` | %7 |\n"
        "| `/files` | %8 |\n"
        "| `/export [name]` | %9 |\n\n"
        "**Tips & Shortcuts:**\n"
        "- Type `@` followed by a file or folder name (e.g. `@src/main.cpp` or `@src/`) to inject its content into your prompt.\n"
        "- Type `@diagnostics` (or `@errors`) to automatically attach compiler errors and warning marks from active editor.\n"
        "- Check **\"Attach current file\"** to include the active editor buffer or current selection.\n"
        "- **Enter** (or **Ctrl+Enter**): Send slash command or prompt.\n"
        "- **Ctrl+L**: Clear visible chat display (preserves context in memory).\n"
        "- **Alt+\\**: Trigger inline AI code completion (Ghost text)."
    ).arg(
        i18n("Show available commands and usage tips"),
        i18n("Open Antigravity configuration dialog"),
        i18n("Clear visible chat display (preserves context in memory)"),
        i18n("Reset context and start a new conversation"),
        i18n("Show project root, Git branch, and status"),
        i18n("View or switch AI model for this session"),
        i18n("Display Google AI account, session stats, and backend info"),
        i18n("List currently open documents in Kate"),
        i18n("Export current conversation to a Markdown file")
    );

    ctx.session->addSystemMessage(help);
    return true;
}

bool cmdSettings(const SlashCommandContext &ctx)
{
    if (ctx.mainWindow && ctx.plugin) {
        ctx.mainWindow->showPluginConfigPage(ctx.plugin, 0);
        ctx.session->addSystemMessage(i18n("⚙️ Opened Antigravity configuration dialog."));
    } else {
        ctx.session->addSystemMessage(i18n("⚠️ Configuration dialog cannot be opened at this time. Go to Settings -> Configure Kate... -> Antigravity."));
    }
    return true;
}

bool cmdClear(const SlashCommandContext &ctx)
{
    if (ctx.chatWidget) {
        ctx.chatWidget->onClearClicked();
    } else {
        ctx.session->addSystemMessage(i18n("🧹 Chat display cleared (context preserved)."));
    }
    return true;
}

bool cmdReset(const SlashCommandContext &ctx)
{
    if (ctx.chatWidget) {
        ctx.chatWidget->onResetContextClicked();
    } else {
        ctx.session->clearHistory();
    }
    return true;
}

bool cmdProject(const SlashCommandContext &ctx)
{
    if (ctx.chatWidget) {
        AgyInfoDialog::showProject(ctx.session, ctx.chatWidget);
        return true;
    }
    const QString rootPath = ctx.session->projectPath();
    ProjectInfo info = rootPath.isEmpty() ? ProjectInfo::createGeneral() : ProjectDetector::detectForPath(rootPath);
    const QString gitStr = info.isGit
        ? QStringLiteral("`%1`").arg(info.gitBranch.isEmpty() ? QStringLiteral("detached") : info.gitBranch)
        : i18n("Not a Git repository");

    QString msg = i18n(
        "### 📁 Project Information\n\n"
        "- **Name:** %1\n"
        "- **Root Directory:** `%2`\n"
        "- **Git Branch:** %3\n"
        "- **Session History:** %4 messages\n"
    ).arg(
        info.displayName(),
        info.rootPath.isEmpty() ? i18n("None (General Workspace)") : info.rootPath,
        gitStr,
        QString::number(ctx.session->messages().size())
    );

    ctx.session->addSystemMessage(msg);
    return true;
}

bool cmdModel(const SlashCommandContext &ctx)
{
    if (ctx.args.isEmpty()) {
        const QString msg = i18n(
            "### 🤖 Current Model\n\n"
            "- **Model:** `%1`\n"
            "- **Backend Mode:** %2\n\n"
            "To change the model for this session, type `/model <name>` (e.g. `/model gemini-3.8-flash-low`, `/model claude-sonnet-4-6`)."
        ).arg(ctx.session->model(), (ctx.session->backendMode() == AgyClient::BackendMode::AgyCli) ? QStringLiteral("Antigravity CLI") : QStringLiteral("Direct Gemini API"));
        ctx.session->addSystemMessage(msg);
    } else {
        ctx.session->setModel(ctx.args);
        ctx.session->addSystemMessage(i18n("✅ Switched model for this session to: **%1**.\n\n*(Use `/settings` to change the global default model).*").arg(ctx.args));
    }
    return true;
}

bool cmdUsage(const SlashCommandContext &ctx)
{
    if (ctx.chatWidget) {
        AgyInfoDialog::showUsage(ctx.session, ctx.chatWidget);
        return true;
    }
    const int msgCount = ctx.session->messages().size();
    const QString modeStr = (ctx.session->backendMode() == AgyClient::BackendMode::AgyCli)
        ? QStringLiteral("Antigravity CLI")
        : QStringLiteral("Direct Gemini API");

    const AgyAccountInfo account = AgyAccount::currentAccount();
    const QString accountStr = account.isAuthenticated
        ? (account.name.isEmpty() ? QStringLiteral("`%1`").arg(account.email) : QStringLiteral("`%1` (%2)").arg(account.email, account.name))
        : i18n("Not authenticated");

    QString usage = i18n(
        "### 📊 Session & Account Statistics\n\n"
        "- **Google AI Account:** %1\n"
        "- **Auth Method:** %2\n"
        "- **Active Project:** %3\n"
        "- **Current Model:** `%4`\n"
        "- **Connection Backend:** %5\n"
        "- **Messages Exchanged:** %6\n"
    ).arg(
        accountStr,
        account.authMethod.isEmpty() ? QStringLiteral("N/A") : account.authMethod,
        ctx.session->projectName().isEmpty() ? i18n("General") : ctx.session->projectName(),
        ctx.session->model(),
        modeStr,
        QString::number(msgCount)
    );

    // 1. Token Metrics
    const auto tu = ctx.session->tokenUsage();
    const QLocale locale;
    usage += i18n(
        "\n#### 🔢 Token Usage\n\n"
        "| Metric | Last Turn | Session Cumulative |\n"
        "| :--- | :---: | :---: |\n"
        "| **Input Tokens** | %1 | %2 |\n"
        "| **Output Tokens** | %3 | %4 |\n"
        "| **Thinking Tokens** | %5 | %6 |\n"
        "| **Total Tokens** | **%7** | **%8** |\n"
    ).arg(
        locale.toString(tu.lastInputTokens),
        locale.toString(tu.totalInputTokens),
        locale.toString(tu.lastOutputTokens),
        locale.toString(tu.totalOutputTokens),
        locale.toString(tu.lastThinkingTokens),
        locale.toString(tu.totalThinkingTokens),
        locale.toString(tu.lastTotalTokens),
        locale.toString(tu.totalTokens)
    );

    // 2. Real-time Quotas & Rate Limits
    const auto quotas = AgyAccount::fetchQuotaLimits();
    if (!quotas.isEmpty()) {
        usage += i18n(
            "\n#### ⏱️ Quotas & Rate Limits\n\n"
            "| Model Group | Limit Window | Remaining | Estimated Reset |\n"
            "| :--- | :--- | :---: | :--- |\n"
        );
        for (const auto &q : quotas) {
            const QString indicator = (q.remainingPercent >= 50)
                ? QStringLiteral("🟩")
                : (q.remainingPercent >= 20 ? QStringLiteral("🟨") : QStringLiteral("🟥"));

            const QString resetRel = formatRelativeTime(q.resetTime);
            const QString resetFormatted = q.resetTime.isValid()
                ? QStringLiteral("%1 (`%2 UTC`)").arg(resetRel, q.resetTime.toString(QStringLiteral("yyyy-MM-dd hh:mm")))
                : q.rawResetTime;

            usage += QStringLiteral("| **%1** | %2 | **%3%** %4 | %5 |\n")
                .arg(q.modelGroup, q.limitName, QString::number(q.remainingPercent), indicator, resetFormatted);
        }
    }

    ctx.session->addSystemMessage(usage);
    return true;
}

bool cmdFiles(const SlashCommandContext &ctx)
{
    QStringList fileEntries;
    if (KTextEditor::Editor::instance()) {
        const auto docs = KTextEditor::Editor::instance()->documents();
        for (auto *doc : docs) {
            if (doc && doc->url().isLocalFile()) {
                const QString full = doc->url().toLocalFile();
                const QString rel = (!ctx.session->projectPath().isEmpty() && full.startsWith(ctx.session->projectPath()))
                    ? QDir(ctx.session->projectPath()).relativeFilePath(full)
                    : doc->url().fileName();

                fileEntries << QStringLiteral("- [📄 %1](kateagy://openfile/%2) (%3 %4%5)")
                    .arg(rel, rel, QString::number(doc->lines()), i18n("lines"), doc->isModified() ? i18n(", modified") : QString());
            }
        }
    }

    if (fileEntries.isEmpty()) {
        ctx.session->addSystemMessage(i18n("No open files found in Kate editor."));
    } else {
        ctx.session->addSystemMessage(i18n("### 📂 Open Files in Kate\n\n") + fileEntries.join(QLatin1Char('\n')));
    }
    return true;
}

bool cmdCommit(const SlashCommandContext &ctx)
{
    if (ctx.chatWidget) {
        ctx.chatWidget->startCommitFlow();
    } else {
        ctx.session->addSystemMessage(i18n("⚠️ /commit is only available from the chat panel."));
    }
    return true;
}

bool cmdExport(const SlashCommandContext &ctx)
{
    const auto messages = ctx.session->messages();
    if (messages.isEmpty()) {
        ctx.session->addSystemMessage(i18n("⚠️ No messages to export in current session."));
        return true;
    }

    QString targetPath;
    if (!ctx.args.isEmpty()) {
        targetPath = ctx.args;
        if (!targetPath.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)) {
            targetPath += QStringLiteral(".md");
        }
        if (!QFileInfo(targetPath).isAbsolute()) {
            const QString baseDir = ctx.session->projectPath().isEmpty()
                ? QDir::homePath()
                : ctx.session->projectPath();
            targetPath = QDir(baseDir).filePath(targetPath);
        }
    } else {
        const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
        const QString baseDir = ctx.session->projectPath().isEmpty()
            ? QDir::homePath()
            : ctx.session->projectPath();
        targetPath = QDir(baseDir).filePath(QStringLiteral("agy-chat-%1.md").arg(timestamp));
    }

    QFile outFile(targetPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ctx.session->addSystemMessage(i18n("⚠️ Failed to create export file: `%1` (%2)", targetPath, outFile.errorString()));
        return true;
    }

    QTextStream out(&outFile);
    out << "# Antigravity Chat Export\n\n";
    out << "- **Project:** " << (ctx.session->projectName().isEmpty() ? QStringLiteral("General") : ctx.session->projectName()) << "\n";
    if (!ctx.session->projectPath().isEmpty()) {
        out << "- **Root:** `" << ctx.session->projectPath() << "`\n";
    }
    out << "- **Model:** `" << ctx.session->model() << "`\n";
    out << "- **Date:** " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n";
    out << "---\n\n";

    const AgyAccountInfo account = AgyAccount::currentAccount();
    const QString userName = (account.isAuthenticated && !account.email.isEmpty())
        ? account.email
        : QStringLiteral("User");

    for (const auto &msg : messages) {
        QString roleName;
        switch (msg.role) {
        case ChatMessage::Role::User:
            roleName = QStringLiteral("👤 ") + userName;
            break;
        case ChatMessage::Role::Assistant:
            roleName = QStringLiteral("✨ Antigravity (") + ctx.session->model() + QStringLiteral(")");
            break;
        case ChatMessage::Role::System:
            roleName = QStringLiteral("⚙️ System");
            break;
        case ChatMessage::Role::Error:
            roleName = QStringLiteral("⚠️ Error");
            break;
        }

        out << "### " << roleName << " (" << msg.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << ")\n\n";
        if (!msg.contextMeta.isEmpty()) {
            out << "> 📎 *" << msg.contextMeta << "*\n\n";
        }
        out << msg.text.trimmed() << "\n\n";
        out << "---\n\n";
    }

    outFile.close();

    const QString relPath = (!ctx.session->projectPath().isEmpty() && targetPath.startsWith(ctx.session->projectPath()))
        ? QDir(ctx.session->projectPath()).relativeFilePath(targetPath)
        : targetPath;

    ctx.session->addSystemMessage(i18n(
        "✅ Conversation successfully exported to:\n"
        "📄 **[%1](kateagy://openfile/%2)**\n\n"
        "*(Click the link to open and edit the exported file in Kate)*",
        relPath,
        targetPath
    ));
    return true;
}

} // namespace

const QList<SlashCommand> &SlashCommandRouter::registry()
{
    static const QList<SlashCommand> s_registry = {
        {
            { QStringLiteral("help"), QStringLiteral("/help"),
              i18n("Show available commands and usage tips"), QStringLiteral("help-about") },
            { QStringLiteral("?") },
            cmdHelp
        },
        {
            { QStringLiteral("settings"), QStringLiteral("/settings"),
              i18n("Open Antigravity configuration dialog"), QStringLiteral("preferences-other") },
            {},
            cmdSettings
        },
        {
            { QStringLiteral("clear"), QStringLiteral("/clear"),
              i18n("Clear visible chat display (preserves context in memory)"), QStringLiteral("edit-clear") },
            {},
            cmdClear
        },
        {
            { QStringLiteral("reset"), QStringLiteral("/reset"),
              i18n("Reset context and start a new conversation"), QStringLiteral("document-new") },
            { QStringLiteral("new") },
            cmdReset
        },
        {
            { QStringLiteral("project"), QStringLiteral("/project"),
              i18n("Show project root, Git branch, and status"), QStringLiteral("folder-development") },
            {},
            cmdProject
        },
        {
            { QStringLiteral("model"), QStringLiteral("/model [name]"),
              i18n("View or switch AI model for this session"), QStringLiteral("code-context") },
            {},
            cmdModel
        },
        {
            { QStringLiteral("usage"), QStringLiteral("/usage"),
              i18n("Display Google AI account, session stats, and backend info"), QStringLiteral("dialog-information") },
            {},
            cmdUsage
        },
        {
            { QStringLiteral("files"), QStringLiteral("/files"),
              i18n("List currently open documents in Kate"), QStringLiteral("document-open") },
            {},
            cmdFiles
        },
        {
            { QStringLiteral("export"), QStringLiteral("/export [file.md]"),
              i18n("Export current conversation to a Markdown file"), QStringLiteral("document-save-as") },
            {},
            cmdExport
        },
        {
            { QStringLiteral("commit"), QStringLiteral("/commit"),
              i18n("Generate a commit message from staged changes and create the commit"),
              QStringLiteral("vcs-commit") },
            {},
            cmdCommit
        },
    };
    return s_registry;
}

QList<SlashCommandInfo> SlashCommandRouter::availableCommands()
{
    QList<SlashCommandInfo> infos;
    infos.reserve(registry().size());
    for (const auto &cmd : registry()) {
        infos.append(cmd.info);
    }
    return infos;
}

bool SlashCommandRouter::isSlashCommand(const QString &text)
{
    const QString trimmed = text.trimmed();
    return trimmed.startsWith(QLatin1Char('/')) && trimmed.length() > 1 && !trimmed.startsWith(QLatin1String("//"));
}

bool SlashCommandRouter::execute(const QString &text,
                                 ChatWidget *chatWidget,
                                 ChatSession *session,
                                 KTextEditor::MainWindow *mainWindow,
                                 KateAntigravityPlugin *plugin)
{
    if (!isSlashCommand(text) || !session) {
        return false;
    }

    const QString trimmed = text.trimmed();
    const QString withoutSlash = trimmed.mid(1);

    SlashCommandContext ctx;
    ctx.command = withoutSlash.section(QLatin1Char(' '), 0, 0).toLower();
    ctx.args = withoutSlash.section(QLatin1Char(' '), 1).trimmed();
    ctx.chatWidget = chatWidget;
    ctx.session = session;
    ctx.mainWindow = mainWindow;
    ctx.plugin = plugin;

    for (const auto &cmd : registry()) {
        if (cmd.info.name == ctx.command || cmd.aliases.contains(ctx.command)) {
            return cmd.handler(ctx);
        }
    }

    // Unrecognized slash command
    session->addSystemMessage(i18n("⚠️ Unrecognized command: `/%1`. Type **/help** for a list of available commands.").arg(ctx.command));
    return true;
}
