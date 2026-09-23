#ifndef AGY_CHATSESSION_H
#define AGY_CHATSESSION_H

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QByteArray>
#include <QJsonObject>
#include "agyclient.h"

class QNetworkAccessManager;
class QNetworkReply;
class AgyProcessChannel;

struct ChatMessage
{
    enum class Role {
        User,
        Assistant,
        System,
        Error
    };

    Role role = Role::User;
    QString text;
    QString contextMeta; // Ej: "Archivo: main.cpp (15 líneas)"
    QDateTime timestamp;
};

class ChatSession : public QObject
{
    Q_OBJECT

public:
    explicit ChatSession(QObject *parent = nullptr);
    ~ChatSession() override;

    void setBackendMode(AgyClient::BackendMode mode);
    AgyClient::BackendMode backendMode() const;

    void setModel(const QString &model);
    QString model() const;

    void setApiKey(const QString &apiKey);
    QString apiKey() const;

    QList<ChatMessage> messages() const;
    bool isGenerating() const;

    void setProject(const QString &rootPath, const QString &name);
    QString projectPath() const;
    QString projectName() const;

    // agy conversation id bound to this workspace. Persisted so the workspace
    // resumes the same agy conversation across restarts. Set from disk on load;
    // updated automatically when agy assigns a new one.
    void setAgyConversationId(const QString &id);
    QString agyConversationId() const;

    void setDraftText(const QString &draft);
    QString draftText() const;

    // Index of the first *visible* message: messages [0, visibleStartIndex) are
    // hidden by "Clear" (but preserved as context). Persisted per workspace so
    // the hidden state survives workspace switches and restarts.
    void setVisibleStartIndex(int index);
    int visibleStartIndex() const;

    // Permanently delete the currently-hidden messages ([0, visibleStartIndex))
    // from the history and reset the index to 0. Does NOT touch the agy
    // conversation id, so the agent keeps its own context. Persists the change.
    void purgeHiddenMessages();

    // Attach an image to the next turn (Direct Gemini API / multimodal only).
    // Cleared automatically once the turn is sent. Empty path clears it.
    void setPendingImage(const QString &imagePath);
    QString pendingImage() const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &obj);

    struct TokenUsage {
        int lastInputTokens = 0;
        int lastOutputTokens = 0;
        int lastThinkingTokens = 0;
        int lastTotalTokens = 0;

        int totalInputTokens = 0;
        int totalOutputTokens = 0;
        int totalThinkingTokens = 0;
        int totalTokens = 0;
    };

    TokenUsage tokenUsage() const { return m_tokenUsage; }

    void addSystemMessage(const QString &text);

public Q_SLOTS:
    void sendMessage(const QString &userText, const QString &contextCode = QString(), const QString &contextMeta = QString());
    void cancelGeneration();
    void clearHistory();
    void applySettings();

Q_SIGNALS:
    void messageAdded(const ChatMessage &msg);
    void streamingDelta(const QString &delta);
    void generationFinished();
    void generationError(const QString &errorMessage);
    void statusChanged(const QString &status);
    void sessionUpdated();

private Q_SLOTS:
    void onChannelLine(const QJsonObject &obj);
    void onChannelConversationId(const QString &id);
    void onChannelWorkspaceReported(const QString &cwd);
    void onChannelFailed();
    void onDirectApiReadyRead();
    void onDirectApiFinished();

private:
    void sendViaCli(const QString &prompt);
    void sendViaDirectApi(const QString &prompt);
    QString buildSystemPrompt() const;
    QString assembleTurnPrompt(const QString &userText, const QString &contextCode, const QString &contextMeta) const;

    // Cap in-memory/persisted history so long-lived sessions don't grow without
    // bound. Keeps the most recent messages; older ones are dropped.
    void pruneHistory();
    static constexpr int kMaxRetainedMessages = 200;

    AgyClient::BackendMode m_backendMode = AgyClient::BackendMode::AgyCli;
    QString m_model = QStringLiteral("gemini-3.8-flash-low");
    QString m_apiKey;
    QString m_projectPath;
    QString m_projectName;
    QString m_agyConversationId; // resumes this workspace's agy conversation
    QString m_draftText;
    QString m_pendingImagePath; // image to attach to the next Direct API turn
    int m_visibleStartIndex = 0; // messages before this are hidden ("Clear")

    QList<ChatMessage> m_messages;
    bool m_isGenerating = false;
    QString m_currentAssistantResponse;

    // CLI Backend (shared NDJSON subprocess channel)
    AgyProcessChannel *m_channel = nullptr;

    // Direct REST API Backend
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    QByteArray m_sseBuffer;

    TokenUsage m_tokenUsage;
};

#endif // AGY_CHATSESSION_H
