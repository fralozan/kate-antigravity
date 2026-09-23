#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QJsonObject>
#include <QStandardPaths>

#include "agyprocesschannel.h"

// End-to-end test of the NDJSON subprocess path using a fake `agy` executable.
// A small Python script stands in for the real binary: it reads NDJSON user
// turns on stdin and emits init/step_update/result events on stdout, exactly
// like the real stream-json protocol. This exercises AgyProcessChannel's
// process launch, framing, and line parsing without the real agy binary.
class TestAgyChannelIntegration : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_scriptPath;
    QString m_python;

    static QString findPython()
    {
        for (const auto &name : {QStringLiteral("python3"), QStringLiteral("python")}) {
            const QString p = QStandardPaths::findExecutable(name);
            if (!p.isEmpty()) {
                return p;
            }
        }
        return QString();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_python = findPython();
        if (m_python.isEmpty()) {
            QSKIP("python not available for the fake agy script");
        }

        // Fake agy: echo NDJSON protocol. Emits init once, then for each user
        // line emits a streamed delta and a SUCCESS result.
        m_scriptPath = QDir(m_dir.path()).filePath(QStringLiteral("fake_agy.py"));
        QFile f(m_scriptPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "import sys, json\n"
               "print(json.dumps({\"event\": \"init\", \"conversation_id\": \"conv-test-123\", \"init\": {\"cwd\": \"/tmp\"}}), flush=True)\n"
               "for line in sys.stdin:\n"
               "    line = line.strip()\n"
               "    if not line:\n"
               "        continue\n"
               "    try:\n"
               "        msg = json.loads(line)\n"
               "    except Exception:\n"
               "        continue\n"
               "    content = msg.get('message', {}).get('content', '')\n"
               "    print(json.dumps({\"event\": \"step_update\", \"step_update\": {\"text_delta\": \"echo: \"}}), flush=True)\n"
               "    print(json.dumps({\"event\": \"result\", \"result\": {\"status\": \"SUCCESS\", \"response\": \"echo: \" + content, \"usage\": {\"input_tokens\": 3, \"output_tokens\": 4, \"total_tokens\": 7}}}), flush=True)\n";
        f.close();
    }

    void testInitAndResultFlow()
    {
        if (m_python.isEmpty()) {
            QSKIP("python not available");
        }

        // Point the channel at "python fake_agy.py" by overriding the program.
        // AgyProcessChannel reads AGY_EXECUTABLE_OVERRIDE; we set it to python
        // and pass the script via arguments through a tiny wrapper: since the
        // channel controls args, we instead create a wrapper shell script.
        const QString wrapper = QDir(m_dir.path()).filePath(QStringLiteral("agy"));
        QFile w(wrapper);
        QVERIFY(w.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream ws(&w);
        ws << "#!/bin/sh\n" << "exec \"" << m_python << "\" \"" << m_scriptPath << "\" \"$@\"\n";
        w.close();
        QFile::setPermissions(wrapper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                       | QFile::ReadGroup | QFile::ExeGroup);

        qputenv("AGY_EXECUTABLE_OVERRIDE", wrapper.toUtf8());

        AgyProcessChannel channel;
        channel.setModel(QStringLiteral("test-model"));

        QSignalSpy lineSpy(&channel, &AgyProcessChannel::lineReceived);
        QVERIFY(lineSpy.isValid());
        QSignalSpy convSpy(&channel, &AgyProcessChannel::conversationIdChanged);
        QVERIFY(convSpy.isValid());

        channel.sendUserMessage(QStringLiteral("hello"));

        // Wait for init + step_update + result (3 lines).
        QTRY_VERIFY_WITH_TIMEOUT(lineSpy.count() >= 3, 8000);

        // The init event's conversation_id must be captured and reported.
        QVERIFY(convSpy.count() >= 1);
        QCOMPARE(convSpy.first().at(0).toString(), QStringLiteral("conv-test-123"));
        QCOMPARE(channel.conversationId(), QStringLiteral("conv-test-123"));

        bool sawInit = false;
        bool sawResult = false;
        QString response;
        for (const auto &call : lineSpy) {
            const QJsonObject obj = call.at(0).toJsonObject();
            const QString ev = obj.value(QStringLiteral("event")).toString();
            if (ev == QLatin1String("init")) {
                sawInit = true;
            } else if (ev == QLatin1String("result")) {
                sawResult = true;
                response = obj.value(QStringLiteral("result")).toObject()
                              .value(QStringLiteral("response")).toString();
            }
        }
        QVERIFY(sawInit);
        QVERIFY(sawResult);
        QCOMPARE(response, QStringLiteral("echo: hello"));

        qunsetenv("AGY_EXECUTABLE_OVERRIDE");
    }
};

QTEST_MAIN(TestAgyChannelIntegration)
#include "test_agychannel_integration.moc"
