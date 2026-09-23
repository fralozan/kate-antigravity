#ifndef AGY_PROCESS_CHANNEL_H
#define AGY_PROCESS_CHANNEL_H

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Reusable wrapper around a persistent `agy` subprocess running in
// stream-json (NDJSON) mode. It owns the QProcess lifecycle, handles process
// (re)start, writes user turns as NDJSON, and frames stdout into individual
// JSON objects. The semantics of each event ("init", "result",
// "step_update", ...) are intentionally left to the consumer, which connects
// to lineReceived() and interprets the objects it cares about.
//
// Both AgyClient (inline completion) and ChatSession (chat) share this channel
// to avoid duplicating the process/NDJSON plumbing.
class AgyProcessChannel : public QObject
{
    Q_OBJECT

public:
    explicit AgyProcessChannel(QObject *parent = nullptr);
    ~AgyProcessChannel() override;

    // Model passed as --model. Changing it recycles the running process so the
    // next turn starts a fresh subprocess with the new model.
    void setModel(const QString &model);
    QString model() const;

    // Optional working directory for the subprocess (used by chat to scope the
    // agent to a project root). Changing it recycles the process.
    void setWorkingDirectory(const QString &dir);
    QString workingDirectory() const;

    // agy conversation to resume: when set, the subprocess is launched with
    // `--conversation <id>` so it restores that conversation's context. Empty
    // means "start a fresh conversation" (agy will assign a new id, reported in
    // the init event and surfaced via conversationIdChanged()). Changing it
    // recycles the process.
    void setConversationId(const QString &id);
    QString conversationId() const;

    // Directories to expose to the agent as workspace roots, passed as repeated
    // `--add-dir <path>` flags. This gives agy access to the project's files
    // without relying on the process working directory. Changing it recycles.
    void setWorkspaceDirs(const QStringList &dirs);
    QStringList workspaceDirs() const;

    // Batch several setter changes so the subprocess is recycled at most once.
    // Wrap related updates (model + conversation + workspace) in
    // beginConfig()/endConfig() to avoid multiple back-to-back restarts.
    void beginConfig();
    void endConfig();

    bool isRunning() const;
    bool isInitialized() const { return m_initialized; }

    // Ensure the subprocess is up and send a single user turn as an NDJSON
    // {"event":"user","message":{"content": ...}} line.
    void sendUserMessage(const QString &content);

    // Terminate and dispose the subprocess (graceful terminate then kill).
    // Safe to call when nothing is running.
    void shutdown();

Q_SIGNALS:
    // A complete JSON object was parsed from a single stdout line.
    void lineReceived(const QJsonObject &obj);
    // The subprocess reported an error (failed to start, crashed, ...).
    void processFailed(QProcess::ProcessError error);
    // The subprocess exited.
    void processFinished(int exitCode, QProcess::ExitStatus status);
    // Emitted when the init event reports a conversation id that differs from
    // the one we launched with (i.e. agy created a new conversation). Lets the
    // consumer persist it so the workspace can be resumed later.
    void conversationIdChanged(const QString &conversationId);
    // Reports the working directory agy actually adopted (from the init event),
    // so the consumer can detect a fallback to the scratch directory.
    void workspaceReported(const QString &cwd);

private Q_SLOTS:
    void onReadyRead();
    void onErrorOccurred(QProcess::ProcessError error);
    void onFinished(int exitCode, QProcess::ExitStatus status);

private:
    void ensureRunning();
    void recycle();

    QProcess *m_process = nullptr;
    QByteArray m_readBuffer;
    QString m_model;
    QString m_workingDirectory;
    QString m_conversationId;
    QStringList m_workspaceDirs;
    bool m_initialized = false;

    // Config batching: while >0, setters defer recycling and set the pending
    // flag; endConfig() performs a single recycle if anything changed.
    int m_configDepth = 0;
    bool m_recyclePending = false;
};

#endif // AGY_PROCESS_CHANNEL_H
