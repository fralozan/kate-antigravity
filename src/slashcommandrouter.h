#ifndef SLASHCOMMANDROUTER_H
#define SLASHCOMMANDROUTER_H

#include <QString>
#include <QList>
#include <QIcon>
#include <functional>

class ChatWidget;
class ChatSession;
class KateAntigravityPlugin;

namespace KTextEditor {
class MainWindow;
}

struct SlashCommandInfo {
    QString name;
    QString syntax;
    QString description;
    QString iconName;
};

// Everything a slash command handler needs to do its work. Passed by const
// reference to each handler so the signature stays stable as commands grow.
struct SlashCommandContext {
    QString command;              // e.g. "model" (lowercased, no leading slash)
    QString args;                 // trailing arguments, trimmed
    ChatWidget *chatWidget = nullptr;
    ChatSession *session = nullptr;
    KTextEditor::MainWindow *mainWindow = nullptr;
    KateAntigravityPlugin *plugin = nullptr;
};

// A registered command: its metadata plus the handler that runs it.
struct SlashCommand {
    SlashCommandInfo info;
    QStringList aliases; // extra names that map to this command (e.g. "?", "new")
    std::function<bool(const SlashCommandContext &)> handler;
};

class SlashCommandRouter
{
public:
    // Metadata for every command (used by the input popup and the help dialog).
    static QList<SlashCommandInfo> availableCommands();

    static bool isSlashCommand(const QString &text);

    static bool execute(const QString &text,
                        ChatWidget *chatWidget,
                        ChatSession *session,
                        KTextEditor::MainWindow *mainWindow,
                        KateAntigravityPlugin *plugin);

private:
    // The command registry (single source of truth: metadata + handlers).
    static const QList<SlashCommand> &registry();
};

#endif // SLASHCOMMANDROUTER_H
