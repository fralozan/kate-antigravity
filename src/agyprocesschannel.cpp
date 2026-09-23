#include "agyprocesschannel.h"
#include "agyaccount.h"

#include <QJsonDocument>
#include <QtGlobal>
#include <QTimer>

AgyProcessChannel::AgyProcessChannel(QObject *parent)
    : QObject(parent)
{
}

AgyProcessChannel::~AgyProcessChannel()
{
    shutdown();
}

void AgyProcessChannel::setModel(const QString &model)
{
    if (m_model == model) {
        return;
    }
    m_model = model;
    // A running subprocess was launched with the old --model; recycle it so the
    // next turn picks up the new one.
    recycle();
}

QString AgyProcessChannel::model() const
{
    return m_model;
}

void AgyProcessChannel::setWorkingDirectory(const QString &dir)
{
    if (m_workingDirectory == dir) {
        return;
    }
    m_workingDirectory = dir;
    recycle();
}

QString AgyProcessChannel::workingDirectory() const
{
    return m_workingDirectory;
}

void AgyProcessChannel::setConversationId(const QString &id)
{
    if (m_conversationId == id) {
        return;
    }
    m_conversationId = id;
    // The running subprocess was launched for a different conversation; recycle
    // so the next turn resumes the requested one.
    recycle();
}

QString AgyProcessChannel::conversationId() const
{
    return m_conversationId;
}

void AgyProcessChannel::setWorkspaceDirs(const QStringList &dirs)
{
    if (m_workspaceDirs == dirs) {
        return;
    }
    m_workspaceDirs = dirs;
    recycle();
}

QStringList AgyProcessChannel::workspaceDirs() const
{
    return m_workspaceDirs;
}

bool AgyProcessChannel::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void AgyProcessChannel::beginConfig()
{
    ++m_configDepth;
}

void AgyProcessChannel::endConfig()
{
    if (m_configDepth > 0) {
        --m_configDepth;
    }
    if (m_configDepth == 0 && m_recyclePending) {
        m_recyclePending = false;
        recycle();
    }
}

void AgyProcessChannel::recycle()
{
    // While batching config changes, coalesce into a single recycle at
    // endConfig() so we don't restart the subprocess several times in a row.
    if (m_configDepth > 0) {
        m_recyclePending = true;
        return;
    }

    if (!m_process) {
        return;
    }

    QProcess *old = m_process;
    m_process = nullptr;
    m_initialized = false;
    m_readBuffer.clear();

    // Detach from the old process so we ignore any further output/signals from
    // it, and reparent it so its lifetime is independent of this channel.
    old->disconnect(this);
    old->setParent(nullptr);

    if (old->state() == QProcess::NotRunning) {
        old->deleteLater();
        return;
    }

    // Tear it down WITHOUT blocking the GUI thread (no waitForFinished): ask it
    // to stop, escalate to kill shortly after if still alive, and delete it once
    // it actually exits.
    QObject::connect(old, &QProcess::finished, old, &QObject::deleteLater);
    old->terminate();
    QTimer::singleShot(1500, old, [old]() {
        if (old->state() != QProcess::NotRunning) {
            old->kill();
        }
    });
}

void AgyProcessChannel::shutdown()
{
    recycle();
}

void AgyProcessChannel::ensureRunning()
{
    if (isRunning()) {
        return;
    }

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_process = new QProcess(this);

    // Test/override hook: point the channel at a stand-in NDJSON executable.
    // Used by integration tests to exercise the subprocess path without the
    // real `agy` binary. Ignored in normal use.
    QString agyPath = qEnvironmentVariable("AGY_EXECUTABLE_OVERRIDE");
    if (agyPath.isEmpty()) {
        agyPath = AgyAccount::findAgyExecutable();
    }
    if (agyPath.isEmpty()) {
        agyPath = QStringLiteral("agy"); // Rely on PATH resolution at launch
    }
    m_process->setProgram(agyPath);

    QStringList args;
    args << QStringLiteral("--input-format") << QStringLiteral("stream-json")
         << QStringLiteral("--output-format") << QStringLiteral("stream-json")
         << QStringLiteral("--model") << m_model
         << QStringLiteral("--disable-slash-commands");

    // Resume a specific conversation when we have its id.
    if (!m_conversationId.isEmpty()) {
        args << QStringLiteral("--conversation") << m_conversationId;
    }

    // Expose each workspace directory to the agent (repeatable --add-dir). This
    // gives agy access to the project's files regardless of the process cwd.
    for (const QString &dir : m_workspaceDirs) {
        if (!dir.isEmpty()) {
            args << QStringLiteral("--add-dir") << dir;
        }
    }

    m_process->setArguments(args);

    if (!m_workingDirectory.isEmpty()) {
        m_process->setWorkingDirectory(m_workingDirectory);
    }

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &AgyProcessChannel::onReadyRead, Qt::UniqueConnection);
    connect(m_process, &QProcess::errorOccurred,
            this, &AgyProcessChannel::onErrorOccurred, Qt::UniqueConnection);
    connect(m_process, &QProcess::finished,
            this, &AgyProcessChannel::onFinished, Qt::UniqueConnection);

    m_initialized = false;
    m_readBuffer.clear();
    m_process->start();
}

void AgyProcessChannel::sendUserMessage(const QString &content)
{
    ensureRunning();

    QJsonObject req;
    req.insert(QStringLiteral("event"), QStringLiteral("user"));

    QJsonObject msg;
    msg.insert(QStringLiteral("content"), content);
    req.insert(QStringLiteral("message"), msg);

    const QByteArray ndjsonLine = QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n';
    m_process->write(ndjsonLine);
}

void AgyProcessChannel::onReadyRead()
{
    if (!m_process) {
        return;
    }

    m_readBuffer.append(m_process->readAllStandardOutput());

    int newlineIndex;
    while ((newlineIndex = m_readBuffer.indexOf('\n')) != -1) {
        const QByteArray line = m_readBuffer.left(newlineIndex).trimmed();
        m_readBuffer.remove(0, newlineIndex + 1);

        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        const QJsonObject obj = doc.object();
        if (obj.value(QStringLiteral("event")).toString() == QLatin1String("init")) {
            m_initialized = true;

            // Capture the conversation id agy assigned. If it differs from what
            // we launched with (e.g. a brand-new conversation), remember it and
            // notify so the consumer can persist it for later resumption.
            const QString convId = obj.value(QStringLiteral("conversation_id")).toString();
            if (!convId.isEmpty() && convId != m_conversationId) {
                m_conversationId = convId;
                Q_EMIT conversationIdChanged(convId);
            }

            // Report the working directory agy actually adopted (init.cwd) so a
            // fallback to the scratch dir can be detected.
            const QString reportedCwd = obj.value(QStringLiteral("init")).toObject()
                                            .value(QStringLiteral("cwd")).toString();
            if (!reportedCwd.isEmpty()) {
                Q_EMIT workspaceReported(reportedCwd);
            }
        }
        Q_EMIT lineReceived(obj);
    }
}

void AgyProcessChannel::onErrorOccurred(QProcess::ProcessError error)
{
    Q_EMIT processFailed(error);
}

void AgyProcessChannel::onFinished(int exitCode, QProcess::ExitStatus status)
{
    m_initialized = false;
    Q_EMIT processFinished(exitCode, status);
}
