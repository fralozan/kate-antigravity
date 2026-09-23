#include <QTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QTabBar>
#include <QGridLayout>
#include <QStackedWidget>
#include <QMenu>
#include <QTextBrowser>
#include <QTemporaryDir>
#include <KLocalizedString>
#include "chatsession.h"
#include "chatwidget.h"
#include "chatsessionmanager.h"

class TestChatSession : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_cacheDir; // isolates the session manager from the real cache

private Q_SLOTS:
    void initTestCase()
    {
        // Redirect the ChatSessionManager singleton's cache to a throwaway dir
        // so widget tests (switchToProject, etc.) never write sessions into the
        // user's real ~/.cache/kateantigravity/sessions.
        QVERIFY(m_cacheDir.isValid());
        ChatSessionManager::instance()->setCacheDirectoryPath(m_cacheDir.path());
    }

    void testMessageHistory()
    {
        ChatSession session;
        QCOMPARE(session.messages().size(), 0);

        // Send a message (mock mode / invalid API key will emit error or request)
        session.setBackendMode(AgyClient::BackendMode::DirectApi);
        session.setApiKey(QString()); // empty API key

        QSignalSpy errorSpy(&session, &ChatSession::generationError);
        session.sendMessage(QStringLiteral("¿Cómo funciona un QTimer en Qt?"));

        // Must have user message + assistant/error message
        QCOMPARE(session.messages().size(), 2);
        QCOMPARE(session.messages().at(0).role, ChatMessage::Role::User);
        QCOMPARE(session.messages().at(0).text, QStringLiteral("¿Cómo funciona un QTimer en Qt?"));

        // Second message was turned into error because API key is empty
        QCOMPARE(session.messages().at(1).role, ChatMessage::Role::Error);
        QVERIFY(errorSpy.count() > 0);
    }

    void testClearHistory()
    {
        ChatSession session;
        session.sendMessage(QStringLiteral("Mensaje 1"));
        QVERIFY(session.messages().size() > 0);

        session.clearHistory();
        QCOMPARE(session.messages().size(), 0);
        QCOMPARE(session.isGenerating(), false);
    }

    void testVisibleStartIndexPersistence()
    {
        // The hidden-messages boundary must survive JSON round-trip (per
        // workspace / across restarts).
        ChatSession session;
        session.addSystemMessage(QStringLiteral("m1"));
        session.addSystemMessage(QStringLiteral("m2"));
        session.addSystemMessage(QStringLiteral("m3"));
        session.setVisibleStartIndex(2); // hide first two

        const QJsonObject json = session.toJson();
        QCOMPARE(json.value(QStringLiteral("visibleStartIndex")).toInt(), 2);

        ChatSession restored;
        QVERIFY(restored.fromJson(json));
        QCOMPARE(restored.visibleStartIndex(), 2);
        QCOMPARE(restored.messages().size(), 3); // messages preserved
    }

    void testPurgeHiddenKeepsContext()
    {
        ChatSession session;
        session.setAgyConversationId(QStringLiteral("keep-me-123"));
        session.addSystemMessage(QStringLiteral("old1"));
        session.addSystemMessage(QStringLiteral("old2"));
        session.addSystemMessage(QStringLiteral("recent"));
        session.setVisibleStartIndex(2); // hide old1, old2

        session.purgeHiddenMessages();

        // Hidden messages gone, recent kept, index reset, conversation intact.
        QCOMPARE(session.messages().size(), 1);
        QCOMPARE(session.messages().at(0).text, QStringLiteral("recent"));
        QCOMPARE(session.visibleStartIndex(), 0);
        QCOMPARE(session.agyConversationId(), QStringLiteral("keep-me-123"));
    }

    void testVisibleStartIndexClampedOnLoad()
    {
        // A stale index larger than the message count must be clamped.
        ChatSession session;
        session.addSystemMessage(QStringLiteral("only"));
        QJsonObject json = session.toJson();
        json.insert(QStringLiteral("visibleStartIndex"), 99);

        ChatSession restored;
        QVERIFY(restored.fromJson(json));
        QCOMPARE(restored.visibleStartIndex(), restored.messages().size());
    }

    void testAgyConversationIdPersistence()
    {
        // The agy conversation id bound to a workspace must round-trip through
        // JSON so the workspace can resume its conversation across restarts.
        ChatSession session;
        session.setProject(QStringLiteral("/tmp/some-project"), QStringLiteral("some-project"));
        session.setAgyConversationId(QStringLiteral("c5f4499b-ac7a-4f2f-9ea8-ea10deb36f96"));

        const QJsonObject json = session.toJson();
        QCOMPARE(json.value(QStringLiteral("agyConversationId")).toString(),
                 QStringLiteral("c5f4499b-ac7a-4f2f-9ea8-ea10deb36f96"));

        ChatSession restored;
        QVERIFY(restored.fromJson(json));
        QCOMPARE(restored.agyConversationId(),
                 QStringLiteral("c5f4499b-ac7a-4f2f-9ea8-ea10deb36f96"));

        // clearHistory starts a fresh conversation (id dropped).
        restored.clearHistory();
        QVERIFY(restored.agyConversationId().isEmpty());
    }

    void testInputKeyHandling()
    {
        ChatInputEdit edit;
        QSignalSpy submitSpy(&edit, &ChatInputEdit::submitRequested);

        // Plain enter key should NOT trigger submit
        QKeyEvent enterEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&edit, &enterEvent);
        QCOMPARE(submitSpy.count(), 0);

        // Ctrl+Enter MUST trigger submit
        QKeyEvent ctrlEnterEvent(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier);
        QApplication::sendEvent(&edit, &ctrlEnterEvent);
        QCOMPARE(submitSpy.count(), 1);
    }

    void testChatWidgetControls()
    {
        ChatSession session;
        ChatWidget widget(nullptr, &session);

        auto *check = widget.findChild<QCheckBox *>();
        QVERIFY(check != nullptr);
        QCOMPARE(check->isChecked(), false); // Desmarcado por defecto
        QCOMPARE(check->text(), i18n("Attach current file"));

        const auto buttons = widget.findChildren<QAbstractButton *>();
        QStringList buttonTexts;
        for (auto *b : buttons) {
            buttonTexts << b->text();
        }
        QVERIFY(buttonTexts.contains(i18n("Clear")));
        QVERIFY(buttonTexts.contains(i18n("Reset")));
        QVERIFY(buttonTexts.contains(i18n("Send")));

        QVERIFY(widget.minimumSizeHint().width() <= 280);
        QVERIFY(widget.minimumSizeHint().height() <= 260);
    }

    void testChatWidgetRendering()
    {
        ChatSession session;
        ChatWidget widget(nullptr, &session);

        auto *browser = widget.findChild<QTextBrowser *>();
        QVERIFY(browser != nullptr);
        QVERIFY(browser->toHtml().contains(QStringLiteral("cellpadding=\"8\"")));

        // Simulate a message query
        widget.sendDirectQuery(QStringLiteral("¿Cómo usar QTimer?"),
                               QStringLiteral("QTimer::singleShot(100, [](){});"),
                               QStringLiteral("main.cpp"));

        const QString html = browser->toHtml();
        QVERIFY(html.contains(QStringLiteral("👤")));
        QVERIFY(html.contains(QStringLiteral("¿Cómo usar QTimer?")));
        QVERIFY(html.contains(QStringLiteral("main.cpp")));
        // Verify table layout is used for padding
        QVERIFY(html.contains(QStringLiteral("cellpadding=\"8\"")));
    }

    void testTabBarAttachmentLogic()
    {
        QWidget window;
        auto *viewSpace = new QWidget(&window);
        auto *grid = new QGridLayout(viewSpace);

        auto *tabBar = new QTabBar(viewSpace);
        grid->addWidget(tabBar, 0, 2);

        auto *quickOpen = new QToolButton(viewSpace);
        grid->addWidget(quickOpen, 0, 4);

        auto *split = new QToolButton(viewSpace);
        grid->addWidget(split, 0, 5);

        auto *stack = new QStackedWidget(viewSpace);
        grid->addWidget(stack, 2, 0, 1, 6);

        // Simular lógica de attachTabBarButtons
        int maxCol = 5;
        for (int i = 0; i < grid->count(); ++i) {
            int r = 0, c = 0, rs = 0, cs = 0;
            grid->getItemPosition(i, &r, &c, &rs, &cs);
            if (r == 0) {
                int rightCol = (cs > 0) ? (c + cs - 1) : c;
                if (rightCol > maxCol) {
                    maxCol = rightCol;
                }
            }
        }
        const int targetCol = maxCol + 1;
        QCOMPARE(targetCol, 6);

        auto *chatBtn = new QToolButton(viewSpace);
        chatBtn->setObjectName(QStringLiteral("antigravity_tabbar_button"));
        grid->addWidget(chatBtn, 0, targetCol, Qt::AlignRight | Qt::AlignVCenter);

        // Verificar inserción
        int btnRow = -1, btnCol = -1, btnRowSpan = -1, btnColSpan = -1;
        int btnIndex = grid->indexOf(chatBtn);
        QVERIFY(btnIndex >= 0);
        grid->getItemPosition(btnIndex, &btnRow, &btnCol, &btnRowSpan, &btnColSpan);
        QCOMPARE(btnRow, 0);
        QCOMPARE(btnCol, 6);
    }

    void testAutoSwitchDoesNotDegradeToGeneral()
    {
        // A document with no real project (here: nullptr -> General) must not
        // change the active workspace away from a real project.
        ChatSession session;
        ChatWidget widget(nullptr, &session);

        ProjectInfo real;
        real.rootPath = QStringLiteral("/tmp/real-project");
        real.name = QStringLiteral("real-project");
        widget.switchToProject(real);
        QCOMPARE(widget.session()->projectPath(), QStringLiteral("/tmp/real-project"));

        // No real project resolved -> keep current workspace.
        widget.maybeAutoSwitchToDocument(nullptr);
        QCOMPARE(widget.session()->projectPath(), QStringLiteral("/tmp/real-project"));

        // Pinned -> auto-switch is suspended regardless.
        QVERIFY(!widget.isWorkspacePinned());
    }

    void testDirectQueryAndPrepare()
    {
        ChatSession session;
        ChatWidget widget(nullptr, &session);

        widget.prepareContextQuery(QStringLiteral("¿Cómo optimizar esto?"));
        auto *check = widget.findChild<QCheckBox *>();
        QVERIFY(check != nullptr);
        QCOMPARE(check->isChecked(), true);

        auto *edit = widget.findChild<ChatInputEdit *>();
        QVERIFY(edit != nullptr);
        QCOMPARE(edit->toPlainText(), QStringLiteral("¿Cómo optimizar esto?"));

        // Test sendDirectQuery
        session.setBackendMode(AgyClient::BackendMode::DirectApi);
        session.setApiKey(QString()); // empty key -> generates user msg + error response

        widget.sendDirectQuery(QStringLiteral("Explica este código"),
                               QStringLiteral("int x = 42;"),
                               QStringLiteral("main.cpp"));

        QCOMPARE(session.messages().size(), 2);
        QCOMPARE(session.messages().at(0).role, ChatMessage::Role::User);
        QCOMPARE(session.messages().at(0).text, QStringLiteral("Explica este código"));
        QCOMPARE(session.messages().at(0).contextMeta, QStringLiteral("main.cpp"));
    }

    void testContextMenuCleanupLogic()
    {
        QMenu menu;

        auto addMenuFunc = [&menu](bool withSelection) {
            const auto existingActions = menu.actions();
            for (QAction *act : existingActions) {
                if (act && (act->objectName().startsWith(QStringLiteral("antigravity_"))
                            || act->text() == QStringLiteral("✨ Antigravity AI"))) {
                    menu.removeAction(act);
                    if (QMenu *sub = act->menu()) {
                        delete sub;
                    } else {
                        delete act;
                    }
                }
            }

            auto *sep = menu.addSeparator();
            sep->setObjectName(QStringLiteral("antigravity_separator"));

            auto *agyMenu = menu.addMenu(QStringLiteral("✨ Antigravity AI"));
            agyMenu->setObjectName(QStringLiteral("antigravity_submenu"));
            if (agyMenu->menuAction()) {
                agyMenu->menuAction()->setObjectName(QStringLiteral("antigravity_menu_action"));
            }

            if (withSelection) {
                agyMenu->addAction(QStringLiteral("Explicar código"));
            } else {
                agyMenu->addAction(QStringLiteral("Abrir Chat"));
            }
        };

        // Simular 3 invocaciones consecutivas de clic derecho sobre el mismo menú reutilizado
        addMenuFunc(true);
        QCOMPARE(menu.actions().size(), 2);
        QCOMPARE(menu.actions().at(1)->menu()->actions().size(), 1);
        QCOMPARE(menu.actions().at(1)->menu()->actions().at(0)->text(), QStringLiteral("Explicar código"));

        addMenuFunc(false);
        QCOMPARE(menu.actions().size(), 2); // Debe mantenerse en 2, NO 4
        QCOMPARE(menu.actions().at(1)->menu()->actions().size(), 1);
        QCOMPARE(menu.actions().at(1)->menu()->actions().at(0)->text(), QStringLiteral("Abrir Chat"));

        addMenuFunc(true);
        QCOMPARE(menu.actions().size(), 2); // Debe mantenerse en 2, NO 6
    }
};

QTEST_MAIN(TestChatSession)
#include "test_chatsession.moc"

