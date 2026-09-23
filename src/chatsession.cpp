#include "chatsession.h"
#include "settings.h"
#include "agyaccount.h"
#include "agymodels.h"
#include "agyprocesschannel.h"
#include "projectrules.h"
#include "tokenstats.h"

#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QFile>
#include <QFileInfo>
#include <KLocalizedString>

namespace {

// Minimal image MIME detection by extension for Gemini inline_data.
QString mimeTypeForImage(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("png")) return QStringLiteral("image/png");
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")) return QStringLiteral("image/jpeg");
    if (ext == QLatin1String("webp")) return QStringLiteral("image/webp");
    if (ext == QLatin1String("gif")) return QStringLiteral("image/gif");
    if (ext == QLatin1String("heic")) return QStringLiteral("image/heic");
    if (ext == QLatin1String("heif")) return QStringLiteral("image/heif");
    return QString();
}

} // namespace

ChatSession::ChatSession(QObject *parent)
    : QObject(parent)
    , m_channel(new AgyProcessChannel(this))
    , m_nam(new QNetworkAccessManager(this))
{
    connect(m_channel, &AgyProcessChannel::lineReceived,
            this, &ChatSession::onChannelLine);
    connect(m_channel, &AgyProcessChannel::processFailed,
            this, &ChatSession::onChannelFailed);
    connect(m_channel, &AgyProcessChannel::conversationIdChanged,
            this, &ChatSession::onChannelConversationId);
    connect(m_channel, &AgyProcessChannel::workspaceReported,
            this, &ChatSession::onChannelWorkspaceReported);

    applySettings();
    connect(AgySettings::instance(), &AgySettings::settingsChanged,
            this, &ChatSession::applySettings);
}

ChatSession::~ChatSession()
{
    cancelGeneration();
    // m_channel is a child QObject and tears down its subprocess in its own
    // destructor.
}

void ChatSession::applySettings()
{
    // The settings singleton already holds the current state (it reloads from
    // disk on construction and on explicit save). Re-reading disk here on every
    // settingsChanged signal was redundant I/O.
    AgySettings *settings = AgySettings::instance();
    const auto newMode = (settings->backendMode == 1)
        ? AgyClient::BackendMode::DirectApi
        : AgyClient::BackendMode::AgyCli;

    m_backendMode = newMode;
    m_model = settings->model;
    m_apiKey = settings->apiKey;

    // The channel recycles its subprocess internally when the model changes.
    m_channel->setModel(m_model);
}

void ChatSession::setBackendMode(AgyClient::BackendMode mode)
{
    m_backendMode = mode;
}

AgyClient::BackendMode ChatSession::backendMode() const
{
    return m_backendMode;
}

void ChatSession::setModel(const QString &model)
{
    m_model = model;
    m_channel->setModel(model);
}

QString ChatSession::model() const
{
    return m_model;
}

void ChatSession::setApiKey(const QString &apiKey)
{
    m_apiKey = apiKey;
}

QString ChatSession::apiKey() const
{
    return m_apiKey;
}

QList<ChatMessage> ChatSession::messages() const
{
    return m_messages;
}

bool ChatSession::isGenerating() const
{
    return m_isGenerating;
}

void ChatSession::setProject(const QString &rootPath, const QString &name)
{
    if (m_projectPath == rootPath && m_projectName == name) {
        return;
    }
    m_projectPath = rootPath;
    m_projectName = name;

    // Scope the agent subprocess to the project root. We both set the working
    // directory (cwd) AND expose it via --add-dir, so agy has reliable access
    // to the project's files even when no files are open in the editor and
    // regardless of the process cwd. Batch both changes so the channel recycles
    // its subprocess at most once (not once per setter).
    m_channel->beginConfig();
    m_channel->setWorkingDirectory(m_projectPath);
    m_channel->setWorkspaceDirs(m_projectPath.isEmpty() ? QStringList()
                                                        : QStringList{m_projectPath});
    m_channel->endConfig();
}

void ChatSession::setAgyConversationId(const QString &id)
{
    if (m_agyConversationId == id) {
        return;
    }
    m_agyConversationId = id;
    m_channel->setConversationId(id);
}

QString ChatSession::agyConversationId() const
{
    return m_agyConversationId;
}

void ChatSession::onChannelConversationId(const QString &id)
{
    // agy assigned (or confirmed) a conversation id. Bind it to this workspace
    // and persist so we can resume the same conversation later.
    if (id.isEmpty() || m_agyConversationId == id) {
        return;
    }
    m_agyConversationId = id;
    Q_EMIT sessionUpdated(); // triggers save
}

void ChatSession::onChannelWorkspaceReported(const QString &cwd)
{
    // If agy fell back to its scratch directory while we expected a real
    // project root, surface a one-time hint so the user understands why the
    // agent may not see the project's files.
    if (m_projectPath.isEmpty()) {
        return;
    }
    if (cwd.contains(QLatin1String("/antigravity-cli/scratch"))) {
        addSystemMessage(i18n(
            "⚠️ The agent reported a scratch working directory instead of the "
            "workspace root (%1). File access may be limited.", m_projectPath));
    }
}

QString ChatSession::projectPath() const
{
    return m_projectPath;
}

QString ChatSession::projectName() const
{
    return m_projectName;
}

void ChatSession::setDraftText(const QString &draft)
{
    m_draftText = draft;
}

QString ChatSession::draftText() const
{
    return m_draftText;
}

void ChatSession::setVisibleStartIndex(int index)
{
    const int clamped = qBound(0, index, m_messages.size());
    if (m_visibleStartIndex == clamped) {
        return;
    }
    m_visibleStartIndex = clamped;
    Q_EMIT sessionUpdated(); // persist the hidden state
}

int ChatSession::visibleStartIndex() const
{
    // Never return an index past the end (messages may have been pruned).
    return qBound(0, m_visibleStartIndex, m_messages.size());
}

void ChatSession::purgeHiddenMessages()
{
    const int hidden = qBound(0, m_visibleStartIndex, m_messages.size());
    if (hidden <= 0) {
        return;
    }
    // Delete the hidden prefix [0, hidden). Keep the agy conversation id intact
    // so the agent still remembers; we only drop them from the visible panel
    // and from our persisted history.
    m_messages.erase(m_messages.begin(), m_messages.begin() + hidden);
    m_visibleStartIndex = 0;
    Q_EMIT sessionUpdated(); // persist the deletion
}

void ChatSession::setPendingImage(const QString &imagePath)
{
    m_pendingImagePath = imagePath;
}

QString ChatSession::pendingImage() const
{
    return m_pendingImagePath;
}

QJsonObject ChatSession::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("projectPath"), m_projectPath);
    root.insert(QStringLiteral("projectName"), m_projectName);
    root.insert(QStringLiteral("agyConversationId"), m_agyConversationId);
    root.insert(QStringLiteral("visibleStartIndex"), m_visibleStartIndex);
    root.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QJsonArray msgsArr;
    for (const auto &msg : m_messages) {
        if (msg.role == ChatMessage::Role::Assistant && msg.text.trimmed().isEmpty()) {
            continue;
        }
        QJsonObject msgObj;
        QString roleStr;
        switch (msg.role) {
        case ChatMessage::Role::User:
            roleStr = QStringLiteral("user");
            break;
        case ChatMessage::Role::Assistant:
            roleStr = QStringLiteral("assistant");
            break;
        case ChatMessage::Role::System:
            roleStr = QStringLiteral("system");
            break;
        case ChatMessage::Role::Error:
            roleStr = QStringLiteral("error");
            break;
        }
        msgObj.insert(QStringLiteral("role"), roleStr);
        msgObj.insert(QStringLiteral("text"), msg.text);
        if (!msg.contextMeta.isEmpty()) {
            msgObj.insert(QStringLiteral("contextMeta"), msg.contextMeta);
        }
        msgObj.insert(QStringLiteral("timestamp"), msg.timestamp.toString(Qt::ISODate));
        msgsArr.append(msgObj);
    }
    QJsonObject usageObj;
    usageObj.insert(QStringLiteral("lastInputTokens"), m_tokenUsage.lastInputTokens);
    usageObj.insert(QStringLiteral("lastOutputTokens"), m_tokenUsage.lastOutputTokens);
    usageObj.insert(QStringLiteral("lastThinkingTokens"), m_tokenUsage.lastThinkingTokens);
    usageObj.insert(QStringLiteral("lastTotalTokens"), m_tokenUsage.lastTotalTokens);
    usageObj.insert(QStringLiteral("totalInputTokens"), m_tokenUsage.totalInputTokens);
    usageObj.insert(QStringLiteral("totalOutputTokens"), m_tokenUsage.totalOutputTokens);
    usageObj.insert(QStringLiteral("totalThinkingTokens"), m_tokenUsage.totalThinkingTokens);
    usageObj.insert(QStringLiteral("totalTokens"), m_tokenUsage.totalTokens);
    root.insert(QStringLiteral("tokenUsage"), usageObj);

    root.insert(QStringLiteral("messages"), msgsArr);
    return root;
}

bool ChatSession::fromJson(const QJsonObject &obj)
{
    if (obj.isEmpty()) {
        return false;
    }

    m_projectPath = obj.value(QStringLiteral("projectPath")).toString();
    m_projectName = obj.value(QStringLiteral("projectName")).toString();
    // Restore the agy conversation bound to this workspace so the next turn
    // resumes it (agy --conversation <id>). Push it to the channel too.
    m_agyConversationId = obj.value(QStringLiteral("agyConversationId")).toString();
    if (m_channel) {
        m_channel->setConversationId(m_agyConversationId);
    }

    if (obj.contains(QStringLiteral("tokenUsage"))) {
        const QJsonObject usageObj = obj.value(QStringLiteral("tokenUsage")).toObject();
        m_tokenUsage.lastInputTokens = usageObj.value(QStringLiteral("lastInputTokens")).toInt();
        m_tokenUsage.lastOutputTokens = usageObj.value(QStringLiteral("lastOutputTokens")).toInt();
        m_tokenUsage.lastThinkingTokens = usageObj.value(QStringLiteral("lastThinkingTokens")).toInt();
        m_tokenUsage.lastTotalTokens = usageObj.value(QStringLiteral("lastTotalTokens")).toInt();
        m_tokenUsage.totalInputTokens = usageObj.value(QStringLiteral("totalInputTokens")).toInt();
        m_tokenUsage.totalOutputTokens = usageObj.value(QStringLiteral("totalOutputTokens")).toInt();
        m_tokenUsage.totalThinkingTokens = usageObj.value(QStringLiteral("totalThinkingTokens")).toInt();
        m_tokenUsage.totalTokens = usageObj.value(QStringLiteral("totalTokens")).toInt();
    }

    m_messages.clear();
    const QJsonArray msgsArr = obj.value(QStringLiteral("messages")).toArray();
    for (const auto &val : msgsArr) {
        const QJsonObject msgObj = val.toObject();
        ChatMessage msg;
        const QString roleStr = msgObj.value(QStringLiteral("role")).toString();
        if (roleStr == QLatin1String("assistant")) {
            msg.role = ChatMessage::Role::Assistant;
        } else if (roleStr == QLatin1String("error")) {
            msg.role = ChatMessage::Role::Error;
        } else if (roleStr == QLatin1String("system")) {
            msg.role = ChatMessage::Role::System;
        } else {
            msg.role = ChatMessage::Role::User;
        }
        msg.text = msgObj.value(QStringLiteral("text")).toString();
        msg.contextMeta = msgObj.value(QStringLiteral("contextMeta")).toString();
        msg.timestamp = QDateTime::fromString(msgObj.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
        if (!msg.timestamp.isValid()) {
            msg.timestamp = QDateTime::currentDateTime();
        }
        m_messages.append(msg);
    }

    pruneHistory();

    // Restore the persisted hidden state, clamped to the (possibly pruned)
    // message count.
    m_visibleStartIndex = qBound(0, obj.value(QStringLiteral("visibleStartIndex")).toInt(), m_messages.size());

    return true;
}

void ChatSession::clearHistory()
{
    cancelGeneration();
    m_messages.clear();
    m_draftText.clear();
    m_visibleStartIndex = 0;
    m_tokenUsage = TokenUsage{};

    // Start a fresh agy conversation for this workspace: drop the bound id so
    // the next launch creates a new one (and gets a new conversation_id).
    m_agyConversationId.clear();
    m_channel->setConversationId(QString());

    // Start a fresh agent context for the next turn.
    m_channel->shutdown();

    Q_EMIT sessionUpdated();
    Q_EMIT statusChanged(i18n("New conversation started"));
}

void ChatSession::addSystemMessage(const QString &text)
{
    ChatMessage msg;
    msg.role = ChatMessage::Role::System;
    msg.text = text;
    msg.timestamp = QDateTime::currentDateTime();
    m_messages.append(msg);
    Q_EMIT messageAdded(msg);
    Q_EMIT sessionUpdated();
}

void ChatSession::cancelGeneration()
{
    if (!m_isGenerating) {
        return;
    }

    m_isGenerating = false;

    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    // Tear down the CLI subprocess so a cancelled turn doesn't keep streaming.
    m_channel->shutdown();

    if (!m_messages.isEmpty() && m_messages.last().role == ChatMessage::Role::Assistant) {
        if (m_messages.last().text.trimmed().isEmpty()) {
            m_messages.last().text = i18n("[Generation cancelled by user]");
        } else {
            m_messages.last().text += QStringLiteral("\n\n") + i18n("[Cancelled]");
        }
    }

    Q_EMIT generationFinished();
    Q_EMIT sessionUpdated();
    Q_EMIT statusChanged(i18n("Generation cancelled"));
}

QString ChatSession::buildSystemPrompt() const
{
    QString prompt = QStringLiteral(
        "You are Antigravity, an expert AI programming assistant integrated natively into the Kate text editor (KDE).\n"
        "Your role is to help the developer write, debug, explain, refactor, and review code.\n\n"
        "CRITICAL INSTRUCTIONS:\n"
        "1. Write concise, accurate, and high-quality answers.\n"
        "2. When providing code, ALWAYS format it inside standard markdown code fences (```language ... ```).\n"
        "3. Provide clean code snippets ready to be copied or inserted directly into the editor.\n"
        "4. When the user provides editor context (active file, selection), focus your response directly on that context.\n"
        "5. Speak the language used by the user (respond in Spanish if the user asks in Spanish, or English if in English).\n\n"
        "APPLYING EDITS:\n"
        "When the user asks you to modify, refactor, or fix existing code and you want the change to be\n"
        "directly applicable in the editor, emit one or more search/replace edit blocks using EXACTLY this format:\n\n"
        "```agy-edit\n"
        "file: relative/path/to/file.ext\n"
        "<<<<<<< SEARCH\n"
        "the exact original lines to find (copied verbatim, including indentation)\n"
        "=======\n"
        "the replacement lines\n"
        ">>>>>>> REPLACE\n"
        "```\n\n"
        "Rules for edit blocks:\n"
        "- The SEARCH section must match the current file content EXACTLY (whitespace included).\n"
        "- Keep SEARCH sections small and focused; use several blocks for several changes.\n"
        "- Always include the `file:` line so the editor knows which document to change.\n"
        "- Still explain your reasoning in prose around the edit blocks."
    );

    if (!m_projectPath.isEmpty()) {
        prompt += QStringLiteral(
            "\n\nPROJECT CONTEXT:\n"
            "- Project Name: %1\n"
            "- Root Directory: %2\n"
            "- Instructions for project files:\n"
            "  * Any commands, scripts, or file paths must be relative to the project root directory.\n"
            "  * When mentioning files created or modified, format them as [📄 filename](kateagy://openfile/relpath) so the developer can open them in Kate with a single click."
        ).arg(m_projectName, m_projectPath);

        // Project-scoped rules from a .antigravity file at the project root are
        // injected verbatim so a repo can carry its own conventions.
        const QString rules = ProjectRules::rulesForProject(m_projectPath);
        if (!rules.isEmpty()) {
            prompt += QStringLiteral(
                "\n\nPROJECT RULES (from .antigravity, authored by the developer; follow them):\n")
                + rules;
        }
    }

    return prompt;
}

QString ChatSession::assembleTurnPrompt(const QString &userText, const QString &contextCode, const QString &contextMeta) const
{
    QString prompt;

    if (!contextCode.trimmed().isEmpty()) {
        prompt += QStringLiteral("### Editor Context\n");
        if (!contextMeta.isEmpty()) {
            prompt += QStringLiteral("**Context:** %1\n").arg(contextMeta);
        }
        prompt += QStringLiteral("```\n%1\n```\n\n").arg(contextCode);
    }

    prompt += userText;
    return prompt;
}

void ChatSession::pruneHistory()
{
    // Drop the oldest messages once the history exceeds the retention cap.
    // Safe to call only when no assistant placeholder is mid-generation.
    if (m_messages.size() > kMaxRetainedMessages) {
        const int dropped = m_messages.size() - kMaxRetainedMessages;
        m_messages.erase(m_messages.begin(), m_messages.begin() + dropped);
        // Keep the hidden boundary aligned with the messages that remain.
        m_visibleStartIndex = qMax(0, m_visibleStartIndex - dropped);
    }
}

void ChatSession::sendMessage(const QString &userText, const QString &contextCode, const QString &contextMeta)
{
    if (m_isGenerating || userText.trimmed().isEmpty()) {
        return;
    }

    // Prune before appending the new turn so we never disturb the in-flight
    // assistant placeholder that the streaming handlers rely on.
    pruneHistory();

    // 1. Mensaje del Usuario
    ChatMessage userMsg;
    userMsg.role = ChatMessage::Role::User;
    userMsg.text = userText;
    userMsg.contextMeta = contextMeta;
    userMsg.timestamp = QDateTime::currentDateTime();
    m_messages.append(userMsg);
    Q_EMIT messageAdded(userMsg);

    // 2. Mensaje placeholder del Asistente
    ChatMessage assistantMsg;
    assistantMsg.role = ChatMessage::Role::Assistant;
    assistantMsg.text = QString();
    assistantMsg.timestamp = QDateTime::currentDateTime();
    m_messages.append(assistantMsg);
    Q_EMIT messageAdded(assistantMsg);
    Q_EMIT sessionUpdated();

    m_isGenerating = true;
    m_currentAssistantResponse.clear();
    Q_EMIT statusChanged(i18n("Generating response..."));

    const QString assembled = assembleTurnPrompt(userText, contextCode, contextMeta);

    if (m_backendMode == AgyClient::BackendMode::AgyCli) {
        if (!m_pendingImagePath.isEmpty()) {
            // The CLI path doesn't carry inline images; drop it with a note.
            m_pendingImagePath.clear();
            addSystemMessage(i18n("ℹ️ Image attachments require the Direct Gemini API backend; the image was ignored for this CLI turn."));
        }
        sendViaCli(assembled);
    } else {
        sendViaDirectApi(assembled);
    }
}

void ChatSession::sendViaCli(const QString &prompt)
{
    // Ensure the channel targets the current project root, then send the turn.
    m_channel->setWorkingDirectory(m_projectPath);
    m_channel->sendUserMessage(prompt);
}

void ChatSession::onChannelLine(const QJsonObject &obj)
{
    const QString eventType = obj.value(QStringLiteral("event")).toString();

    if (eventType == QLatin1String("step_update")) {
        QJsonObject stepUpdate = obj.value(QStringLiteral("step_update")).toObject();
        const QString textDelta = stepUpdate.value(QStringLiteral("text_delta")).toString();
        if (!textDelta.isEmpty() && m_isGenerating) {
            m_currentAssistantResponse.append(textDelta);
            if (!m_messages.isEmpty() && m_messages.last().role == ChatMessage::Role::Assistant) {
                m_messages.last().text = m_currentAssistantResponse;
            }
            Q_EMIT streamingDelta(textDelta);
        }
    } else if (eventType == QLatin1String("result")) {
        QJsonObject resultObj = obj.value(QStringLiteral("result")).toObject();
        const QString status = resultObj.value(QStringLiteral("status")).toString();

        if (resultObj.contains(QStringLiteral("usage"))) {
            const QJsonObject usageObj = resultObj.value(QStringLiteral("usage")).toObject();
            m_tokenUsage.lastInputTokens = usageObj.value(QStringLiteral("input_tokens")).toInt();
            m_tokenUsage.lastOutputTokens = usageObj.value(QStringLiteral("output_tokens")).toInt();
            m_tokenUsage.lastThinkingTokens = usageObj.value(QStringLiteral("thinking_tokens")).toInt();
            m_tokenUsage.lastTotalTokens = usageObj.value(QStringLiteral("total_tokens")).toInt();

            m_tokenUsage.totalInputTokens += m_tokenUsage.lastInputTokens;
            m_tokenUsage.totalOutputTokens += m_tokenUsage.lastOutputTokens;
            m_tokenUsage.totalThinkingTokens += m_tokenUsage.lastThinkingTokens;
            m_tokenUsage.totalTokens += m_tokenUsage.lastTotalTokens;
        }

        if (status == QLatin1String("SUCCESS")) {
            const QString fullResp = resultObj.value(QStringLiteral("response")).toString();
            if (m_currentAssistantResponse.isEmpty() && !fullResp.isEmpty()) {
                m_currentAssistantResponse = fullResp;
                if (!m_messages.isEmpty() && m_messages.last().role == ChatMessage::Role::Assistant) {
                    m_messages.last().text = m_currentAssistantResponse;
                }
            }
            m_isGenerating = false;
            // Feed this turn's usage into the cross-session stats.
            TokenStats::instance()->recordUsage(m_tokenUsage.lastInputTokens,
                                                m_tokenUsage.lastOutputTokens,
                                                m_tokenUsage.lastTotalTokens);
            Q_EMIT generationFinished();
            Q_EMIT sessionUpdated();
            Q_EMIT statusChanged(i18n("Ready"));
        } else {
            const QString err = resultObj.value(QStringLiteral("error")).toString();
            m_isGenerating = false;
            if (!m_messages.isEmpty() && m_messages.last().role == ChatMessage::Role::Assistant) {
                m_messages.last().role = ChatMessage::Role::Error;
                m_messages.last().text = i18n("Antigravity error: %1", err.isEmpty() ? status : err);
            }
            Q_EMIT generationError(err);
            Q_EMIT sessionUpdated();
            Q_EMIT statusChanged(i18n("Generation error"));
        }
    }
}

void ChatSession::onChannelFailed()
{
    if (!m_isGenerating) {
        return;
    }
    m_isGenerating = false;
    const QString err = i18n("Error in agy CLI subprocess");
    if (!m_messages.isEmpty() && m_messages.last().role == ChatMessage::Role::Assistant) {
        m_messages.last().role = ChatMessage::Role::Error;
        m_messages.last().text = err;
    }
    Q_EMIT generationError(err);
    Q_EMIT sessionUpdated();
    Q_EMIT statusChanged(i18n("Error in agy CLI"));
}

void ChatSession::sendViaDirectApi(const QString &prompt)
{
    if (m_apiKey.trimmed().isEmpty()) {
        m_isGenerating = false;
        const QString err = i18n("Gemini API key is not configured. Go to Settings -> Configure Kate... -> Antigravity.");
        if (!m_messages.isEmpty() && m_messages.last().role == ChatMessage::Role::Assistant) {
            m_messages.last().role = ChatMessage::Role::Error;
            m_messages.last().text = err;
        }
        Q_EMIT generationError(err);
        Q_EMIT statusChanged(i18n("API key missing"));
        return;
    }

    const QString restModel = AgyModels::restApiModelFor(m_model);
    const QString endpoint = QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:streamGenerateContent?alt=sse")
                                 .arg(restModel);

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // Send the API key as a header rather than a query parameter to avoid leaking it.
    request.setRawHeader(QByteArrayLiteral("x-goog-api-key"), m_apiKey.trimmed().toUtf8());

    // Construir historial multi-turn
    QJsonObject root;

    // System instruction
    QJsonObject sysInst;
    QJsonArray sysParts;
    QJsonObject sysPart;
    sysPart.insert(QStringLiteral("text"), buildSystemPrompt());
    sysParts.append(sysPart);
    sysInst.insert(QStringLiteral("parts"), sysParts);
    root.insert(QStringLiteral("system_instruction"), sysInst);

    // Contents: mensajes previos
    QJsonArray contents;
    const int startIdx = qMax(0, m_messages.size() - 20); // Mantener últimas interacciones

    for (int i = startIdx; i < m_messages.size() - 1; ++i) { // Omitir el último que es el placeholder vacío
        const auto &msg = m_messages.at(i);
        if (msg.role == ChatMessage::Role::Error) {
            continue;
        }

        QJsonObject turnObj;
        turnObj.insert(QStringLiteral("role"), (msg.role == ChatMessage::Role::User) ? QStringLiteral("user") : QStringLiteral("model"));

        const bool isCurrentTurn = (i == m_messages.size() - 2);

        QJsonArray parts;
        QJsonObject textPart;
        textPart.insert(QStringLiteral("text"), isCurrentTurn ? prompt : msg.text);
        parts.append(textPart);

        // Attach a pending image to the current user turn as inline_data.
        if (isCurrentTurn && !m_pendingImagePath.isEmpty()) {
            QFile imgFile(m_pendingImagePath);
            if (imgFile.open(QIODevice::ReadOnly)) {
                const QByteArray bytes = imgFile.read(8 * 1024 * 1024); // cap 8 MB
                imgFile.close();
                const QString mime = mimeTypeForImage(m_pendingImagePath);
                if (!bytes.isEmpty() && !mime.isEmpty()) {
                    QJsonObject inlineData;
                    inlineData.insert(QStringLiteral("mime_type"), mime);
                    inlineData.insert(QStringLiteral("data"), QString::fromLatin1(bytes.toBase64()));
                    QJsonObject imagePart;
                    imagePart.insert(QStringLiteral("inline_data"), inlineData);
                    parts.append(imagePart);
                }
            }
        }

        turnObj.insert(QStringLiteral("parts"), parts);
        contents.append(turnObj);
    }

    // The image (if any) has now been consumed for this turn.
    m_pendingImagePath.clear();

    root.insert(QStringLiteral("contents"), contents);

    m_sseBuffer.clear();
    const QByteArray requestData = QJsonDocument(root).toJson(QJsonDocument::Compact);

    m_currentReply = m_nam->post(request, requestData);

    connect(m_currentReply, &QNetworkReply::readyRead,
            this, &ChatSession::onDirectApiReadyRead);
    connect(m_currentReply, &QNetworkReply::finished,
            this, &ChatSession::onDirectApiFinished);
}

void ChatSession::onDirectApiReadyRead()
{
    if (!m_currentReply) {
        return;
    }

    m_sseBuffer.append(m_currentReply->readAll());

    int newlineIndex;
    while ((newlineIndex = m_sseBuffer.indexOf('\n')) != -1) {
        QByteArray line = m_sseBuffer.left(newlineIndex).trimmed();
        m_sseBuffer.remove(0, newlineIndex + 1);

        if (line.startsWith("data: ")) {
            QByteArray jsonPayload = line.mid(6).trimmed();
            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(jsonPayload, &parseErr);
            if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject obj = doc.object();
                QJsonArray candidates = obj.value(QStringLiteral("candidates")).toArray();
                if (!candidates.isEmpty()) {
                    QJsonObject cand = candidates.first().toObject();
                    QJsonObject content = cand.value(QStringLiteral("content")).toObject();
                    QJsonArray parts = content.value(QStringLiteral("parts")).toArray();
                    for (const auto &partVal : parts) {
                        const QString chunk = partVal.toObject().value(QStringLiteral("text")).toString();
                        if (!chunk.isEmpty()) {
                            m_currentAssistantResponse.append(chunk);
                            if (!m_messages.isEmpty() && m_messages.last().role == ChatMessage::Role::Assistant) {
                                m_messages.last().text = m_currentAssistantResponse;
                            }
                            Q_EMIT streamingDelta(chunk);
                        }
                    }
                }

                if (obj.contains(QStringLiteral("usageMetadata"))) {
                    const QJsonObject usageObj = obj.value(QStringLiteral("usageMetadata")).toObject();
                    m_tokenUsage.lastInputTokens = usageObj.value(QStringLiteral("promptTokenCount")).toInt();
                    m_tokenUsage.lastOutputTokens = usageObj.value(QStringLiteral("candidatesTokenCount")).toInt();
                    m_tokenUsage.lastTotalTokens = usageObj.value(QStringLiteral("totalTokenCount")).toInt();
                    m_tokenUsage.totalInputTokens += m_tokenUsage.lastInputTokens;
                    m_tokenUsage.totalOutputTokens += m_tokenUsage.lastOutputTokens;
                    m_tokenUsage.totalTokens += m_tokenUsage.lastTotalTokens;
                }
            }
        }
    }
}

void ChatSession::onDirectApiFinished()
{
    if (!m_currentReply) {
        return;
    }

    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_isGenerating = false;
        const QString err = i18n("Gemini API error: %1", reply->errorString());
        if (!m_messages.isEmpty() && m_messages.last().role == ChatMessage::Role::Assistant) {
            m_messages.last().role = ChatMessage::Role::Error;
            m_messages.last().text = err;
        }
        Q_EMIT generationError(err);
        Q_EMIT sessionUpdated();
        Q_EMIT statusChanged(i18n("Gemini API error"));
        return;
    }

    m_isGenerating = false;
    // Record this turn's usage (from the streamed usageMetadata) into stats.
    TokenStats::instance()->recordUsage(m_tokenUsage.lastInputTokens,
                                        m_tokenUsage.lastOutputTokens,
                                        m_tokenUsage.lastTotalTokens);
    Q_EMIT generationFinished();
    Q_EMIT sessionUpdated();
    Q_EMIT statusChanged(i18n("Ready"));
}
