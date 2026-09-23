#include "agyclient.h"
#include "settings.h"
#include "agymodels.h"
#include "agyprocesschannel.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

AgyClient::AgyClient(QObject *parent)
    : QObject(parent)
    , m_channel(new AgyProcessChannel(this))
    , m_nam(new QNetworkAccessManager(this))
{
    connect(m_channel, &AgyProcessChannel::lineReceived,
            this, &AgyClient::onChannelLine);
    connect(m_channel, &AgyProcessChannel::processFailed,
            this, &AgyClient::onChannelFailed);
    connect(m_channel, &AgyProcessChannel::processFinished,
            this, &AgyClient::onChannelFinished);

    applySettings();
    connect(AgySettings::instance(), &AgySettings::settingsChanged,
            this, &AgyClient::applySettings);
}

void AgyClient::applySettings()
{
    AgySettings *s = AgySettings::instance();
    // Inline completion uses its own (typically faster) model preset.
    setModel(s->completionModel.isEmpty() ? s->model : s->completionModel);
    setBackendMode(static_cast<BackendMode>(s->backendMode));
    if (!s->apiKey.isEmpty()) {
        setApiKey(s->apiKey);
    } else {
        const QString envKey = qEnvironmentVariable("GEMINI_API_KEY");
        if (!envKey.isEmpty()) {
            setApiKey(envKey);
        }
    }
}

AgyClient::~AgyClient() = default;

void AgyClient::setModel(const QString &model)
{
    if (m_model != model) {
        m_model = model;
        // The channel recycles its subprocess internally when the model changes.
        m_channel->setModel(model);
    }
}

QString AgyClient::model() const
{
    return m_model;
}

void AgyClient::setBackendMode(BackendMode mode)
{
    m_backendMode = mode;
}

AgyClient::BackendMode AgyClient::backendMode() const
{
    return m_backendMode;
}

void AgyClient::setApiKey(const QString &apiKey)
{
    m_apiKey = apiKey;
}

QString AgyClient::apiKey() const
{
    return m_apiKey;
}

bool AgyClient::isBusy() const
{
    return m_inFlightRequestId != 0;
}

uint64_t AgyClient::requestCompletion(const CompletionContext &ctx)
{
    const uint64_t id = ++m_requestIdCounter;
    CompletionContext targetCtx = ctx;
    targetCtx.requestId = id;

    if (m_inFlightRequestId != 0) {
        // A turn is currently running in background: queue as newest pending
        m_pendingContext = targetCtx;
        m_hasPendingRequest = true;
        return id;
    }

    dispatchRequest(targetCtx);
    return id;
}

void AgyClient::cancelRequest(uint64_t requestId)
{
    if (m_hasPendingRequest && m_pendingContext.requestId == requestId) {
        m_hasPendingRequest = false;
    }
    if (m_inFlightRequestId == requestId) {
        // Discard result when it arrives
        m_inFlightRequestId = 0;
        // Abort the outstanding Direct API request (if any) to stop consuming
        // network/quota for a completion we no longer care about.
        if (m_currentReply) {
            m_currentReply->abort();
        }
    }
}

void AgyClient::dispatchRequest(const CompletionContext &ctx)
{
    m_inFlightRequestId = ctx.requestId;
    m_inFlightContext = ctx;

    if (m_backendMode == BackendMode::DirectApi && !m_apiKey.isEmpty()) {
        sendDirectApiPrompt(ctx);
    } else {
        sendAgyCliPrompt(ctx);
    }
}

void AgyClient::sendAgyCliPrompt(const CompletionContext &ctx)
{
    const QString prompt = ContextBuilder::buildPrompt(ctx);
    m_channel->sendUserMessage(prompt);
}

void AgyClient::onChannelLine(const QJsonObject &obj)
{
    const QString event = obj.value(QStringLiteral("event")).toString();

    if (event == QLatin1String("init")) {
        Q_EMIT statusChanged(QStringLiteral("Antigravity connected"));
    } else if (event == QLatin1String("result")) {
        const QJsonObject result = obj.value(QStringLiteral("result")).toObject();
        const QString status = result.value(QStringLiteral("status")).toString();

        uint64_t completedId = m_inFlightRequestId;
        CompletionContext completedCtx = m_inFlightContext;
        m_inFlightRequestId = 0;

        if (completedId != 0) {
            if (status == QLatin1String("SUCCESS")) {
                const QString rawResponse = result.value(QStringLiteral("response")).toString();
                const QString sanitized = ContextBuilder::sanitizeResponse(rawResponse, completedCtx);
                Q_EMIT completionReady(completedId, sanitized);
            } else {
                const QString errorMsg = result.value(QStringLiteral("error")).toString();
                Q_EMIT completionFailed(completedId, errorMsg);
            }
        }

        // If a newer request was queued while waiting, execute it now
        if (m_hasPendingRequest) {
            CompletionContext nextCtx = m_pendingContext;
            m_hasPendingRequest = false;
            dispatchRequest(nextCtx);
        }
    }
}

void AgyClient::onChannelFailed()
{
    if (m_inFlightRequestId != 0) {
        uint64_t reqId = m_inFlightRequestId;
        m_inFlightRequestId = 0;
        Q_EMIT completionFailed(reqId, QStringLiteral("Proceso de Antigravity falló"));
    }
}

void AgyClient::onChannelFinished()
{
    if (m_inFlightRequestId != 0) {
        uint64_t reqId = m_inFlightRequestId;
        m_inFlightRequestId = 0;
        Q_EMIT completionFailed(reqId, QStringLiteral("Proceso de Antigravity terminó inesperadamente"));
    }
}

void AgyClient::sendDirectApiPrompt(const CompletionContext &ctx)
{
    const QString prompt = ContextBuilder::buildPrompt(ctx);

    // REST endpoint using Gemini API. The public endpoint only serves a subset
    // of the model names the CLI understands, so map to a supported model.
    const QString modelName = AgyModels::restApiModelFor(m_model);
    const QString urlStr = QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent")
                               .arg(modelName);

    QNetworkRequest request((QUrl(urlStr)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // Send the API key as a header rather than a query parameter to avoid leaking it in
    // process listings, proxy logs, or crash dumps.
    request.setRawHeader(QByteArrayLiteral("x-goog-api-key"), m_apiKey.toUtf8());

    QJsonObject textPart;
    textPart.insert(QStringLiteral("text"), prompt);

    QJsonArray parts;
    parts.append(textPart);

    QJsonObject content;
    content.insert(QStringLiteral("parts"), parts);

    QJsonArray contents;
    contents.append(content);

    QJsonObject genConfig;
    genConfig.insert(QStringLiteral("temperature"), 0.2);
    genConfig.insert(QStringLiteral("maxOutputTokens"), 128);

    QJsonObject root;
    root.insert(QStringLiteral("contents"), contents);
    root.insert(QStringLiteral("generationConfig"), genConfig);

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(root).toJson(QJsonDocument::Compact));
    m_currentReply = reply;

    uint64_t reqId = ctx.requestId;
    connect(reply, &QNetworkReply::finished, this, [this, reply, reqId, ctx]() {
        onDirectApiFinished(reply, reqId, ctx);
    });
}

void AgyClient::onDirectApiFinished(QNetworkReply *reply, uint64_t requestId, const CompletionContext &ctx)
{
    reply->deleteLater();
    if (m_currentReply == reply) {
        m_currentReply = nullptr;
    }

    if (m_inFlightRequestId != requestId) {
        // Stale or cancelled
        return;
    }

    m_inFlightRequestId = 0;

    if (reply->error() != QNetworkReply::NoError) {
        Q_EMIT completionFailed(requestId, reply->errorString());
    } else {
        const QByteArray responseData = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(responseData);
        QString completionText;

        const QJsonObject root = doc.object();
        const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
        if (!candidates.isEmpty()) {
            const QJsonObject candidate = candidates.at(0).toObject();
            const QJsonObject content = candidate.value(QStringLiteral("content")).toObject();
            const QJsonArray parts = content.value(QStringLiteral("parts")).toArray();
            if (!parts.isEmpty()) {
                completionText = parts.at(0).toObject().value(QStringLiteral("text")).toString();
            }
        }

        const QString sanitized = ContextBuilder::sanitizeResponse(completionText, ctx);
        Q_EMIT completionReady(requestId, sanitized);
    }

    if (m_hasPendingRequest) {
        CompletionContext nextCtx = m_pendingContext;
        m_hasPendingRequest = false;
        dispatchRequest(nextCtx);
    }
}
