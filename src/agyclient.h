#ifndef AGY_CLIENT_H
#define AGY_CLIENT_H

#include <QObject>
#include <QByteArray>
#include <QJsonObject>
#include "contextbuilder.h"

class QNetworkAccessManager;
class QNetworkReply;
class AgyProcessChannel;

class AgyClient : public QObject
{
    Q_OBJECT

public:
    enum class BackendMode {
        AgyCli,     // Subproceso persistente de agy CLI en modo stream-json
        DirectApi   // API directa de Gemini mediante HTTPS
    };
    Q_ENUM(BackendMode)

    explicit AgyClient(QObject *parent = nullptr);
    ~AgyClient() override;

    void setModel(const QString &model);
    QString model() const;

    void setBackendMode(BackendMode mode);
    BackendMode backendMode() const;

    void setApiKey(const QString &apiKey);
    QString apiKey() const;

    uint64_t requestCompletion(const CompletionContext &ctx);
    void cancelRequest(uint64_t requestId);

    bool isBusy() const;

Q_SIGNALS:
    void completionReady(uint64_t requestId, const QString &completion);
    void completionFailed(uint64_t requestId, const QString &errorMessage);
    void statusChanged(const QString &status);

public Q_SLOTS:
    void applySettings();

private Q_SLOTS:
    void onChannelLine(const QJsonObject &obj);
    void onChannelFailed();
    void onChannelFinished();
    void onDirectApiFinished(QNetworkReply *reply, uint64_t requestId, const CompletionContext &ctx);

private:
    void dispatchRequest(const CompletionContext &ctx);
    void sendAgyCliPrompt(const CompletionContext &ctx);
    void sendDirectApiPrompt(const CompletionContext &ctx);

    BackendMode m_backendMode = BackendMode::AgyCli;
    QString m_model = QStringLiteral("gemini-3.8-flash-low");
    QString m_apiKey;

    AgyProcessChannel *m_channel = nullptr;

    uint64_t m_requestIdCounter = 0;
    uint64_t m_inFlightRequestId = 0;
    CompletionContext m_inFlightContext;

    bool m_hasPendingRequest = false;
    CompletionContext m_pendingContext;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_currentReply = nullptr; // In-flight Direct API request, if any
};

#endif // AGY_CLIENT_H
