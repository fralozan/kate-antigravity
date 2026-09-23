#include <QTest>
#include <QSignalSpy>
#include "agyclient.h"

class TestAgyClient : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testConfiguration()
    {
        AgyClient client;
        QVERIFY(!client.model().isEmpty());

        client.setModel(QStringLiteral("gemini-3.7-flash-low"));
        QCOMPARE(client.model(), QStringLiteral("gemini-3.7-flash-low"));

        client.setBackendMode(AgyClient::BackendMode::DirectApi);
        QCOMPARE(client.backendMode(), AgyClient::BackendMode::DirectApi);

        client.setApiKey(QStringLiteral("test-api-key-123"));
        QCOMPARE(client.apiKey(), QStringLiteral("test-api-key-123"));
    }

    void testCancelRequest()
    {
        AgyClient client;
        CompletionContext ctx;
        ctx.prefix = QStringLiteral("int x = ");
        ctx.suffix = QStringLiteral(";");

        uint64_t reqId = client.requestCompletion(ctx);
        QVERIFY(reqId > 0);

        // Cancel the request
        client.cancelRequest(reqId);
        // It should handle cancel gracefully without crashes
    }
};

QTEST_MAIN(TestAgyClient)
#include "test_agyclient.moc"
