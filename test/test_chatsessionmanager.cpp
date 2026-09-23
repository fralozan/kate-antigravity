#include <QTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <KLocalizedString>
#include "chatsessionmanager.h"
#include "chatsession.h"
#include "projectdetector.h"

class TestChatSessionManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testSessionMapping()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        ChatSessionManager manager;
        manager.setCacheDirectoryPath(tempDir.path());

        // Project A
        ProjectInfo projA;
        projA.rootPath = QStringLiteral("/home/user/project_a");
        projA.name = QStringLiteral("project_a");
        projA.gitBranch = QStringLiteral("main");

        // Project B
        ProjectInfo projB;
        projB.rootPath = QStringLiteral("/home/user/project_b");
        projB.name = QStringLiteral("project_b");
        projB.gitBranch = QStringLiteral("dev");

        ChatSession *sessionA = manager.sessionForProject(projA);
        ChatSession *sessionB = manager.sessionForProject(projB);
        ChatSession *sessionGeneral = manager.sessionForProject(ProjectInfo::createGeneral());

        QVERIFY(sessionA != nullptr);
        QVERIFY(sessionB != nullptr);
        QVERIFY(sessionGeneral != nullptr);

        // All 3 sessions must be distinct pointers
        QVERIFY(sessionA != sessionB);
        QVERIFY(sessionA != sessionGeneral);
        QVERIFY(sessionB != sessionGeneral);

        // Fetching again returns same instance
        QCOMPARE(manager.sessionForProject(projA), sessionA);
        QCOMPARE(manager.sessionForProject(projB), sessionB);
    }

    void testDraftPreservation()
    {
        ChatSession session;
        QCOMPARE(session.draftText(), QString());

        session.setDraftText(QStringLiteral("¿Cómo crear un componente en Vue?"));
        QCOMPARE(session.draftText(), QStringLiteral("¿Cómo crear un componente en Vue?"));

        // Clear history clears draft as well
        session.clearHistory();
        QCOMPARE(session.draftText(), QString());
    }

    void testSerializationAndPersistence()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        QString projPath = QStringLiteral("/path/to/my_app");
        ProjectInfo info;
        info.rootPath = projPath;
        info.name = QStringLiteral("my_app");

        // 1. Create session and populate messages
        {
            ChatSessionManager manager;
            manager.setCacheDirectoryPath(tempDir.path());

            ChatSession *session = manager.sessionForProject(info);
            QVERIFY(session != nullptr);

            // Add user message manually by sending message (or testing json)
            session->setBackendMode(AgyClient::BackendMode::DirectApi);
            session->setApiKey(QString()); // empty key to quickly fail turn
            session->sendMessage(QStringLiteral("Hola agente"));

            QCOMPARE(session->messages().size(), 2);
            manager.saveSession(session);
        }

        // 2. Open fresh manager in same cache directory and verify restoration
        {
            ChatSessionManager manager2;
            manager2.setCacheDirectoryPath(tempDir.path());

            ChatSession *restoredSession = manager2.sessionForProject(info);
            QVERIFY(restoredSession != nullptr);
            QCOMPARE(restoredSession->projectPath(), projPath);
            QCOMPARE(restoredSession->projectName(), QStringLiteral("my_app"));

            // Must have restored the 2 messages!
            QCOMPARE(restoredSession->messages().size(), 2);
            QCOMPARE(restoredSession->messages().at(0).role, ChatMessage::Role::User);
            QCOMPARE(restoredSession->messages().at(0).text, QStringLiteral("Hola agente"));
        }
    }

    void testRemoveProject()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        ChatSessionManager manager;
        manager.setCacheDirectoryPath(tempDir.path());

        ProjectInfo info;
        info.rootPath = QStringLiteral("/path/to/removable");
        info.name = QStringLiteral("removable");

        ChatSession *session = manager.sessionForProject(info);
        manager.saveSession(session);

        const QString key = ChatSessionManager::projectKey(info.rootPath);
        const QString filePath = QDir(tempDir.path()).filePath(key + QStringLiteral(".json"));
        QVERIFY(QFile::exists(filePath));

        // The project should now be known.
        bool known = false;
        for (const auto &p : manager.knownProjects()) {
            if (p.rootPath == info.rootPath) { known = true; break; }
        }
        QVERIFY(known);

        QSignalSpy listSpy(&manager, &ChatSessionManager::projectListChanged);
        manager.removeProject(info.rootPath);
        QVERIFY(listSpy.count() >= 1);

        // Gone from disk and from the known list.
        QVERIFY(!QFile::exists(filePath));
        bool stillKnown = false;
        for (const auto &p : manager.knownProjects()) {
            if (p.rootPath == info.rootPath) { stillKnown = true; break; }
        }
        QVERIFY(!stillKnown);

        // Removing the Global workspace is a no-op (it must remain).
        const int before = manager.knownProjects().size();
        manager.removeProject(QString());
        QCOMPARE(manager.knownProjects().size(), before);
    }

    void testClearSessionRemovesCache()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        ChatSessionManager manager;
        manager.setCacheDirectoryPath(tempDir.path());

        ProjectInfo info;
        info.rootPath = QStringLiteral("/path/to/clear_test");
        info.name = QStringLiteral("clear_test");

        ChatSession *session = manager.sessionForProject(info);
        session->setBackendMode(AgyClient::BackendMode::DirectApi);
        session->setApiKey(QString());
        session->sendMessage(QStringLiteral("Test mensaje"));
        manager.saveSession(session);

        const QString key = ChatSessionManager::projectKey(info.rootPath);
        const QString filePath = QDir(tempDir.path()).filePath(key + QStringLiteral(".json"));
        QVERIFY(QFile::exists(filePath));

        // Clear session
        manager.clearSession(info.rootPath);
        QVERIFY(!QFile::exists(filePath));
        QCOMPARE(session->messages().size(), 0);
    }
};

QTEST_MAIN(TestChatSessionManager)
#include "test_chatsessionmanager.moc"
