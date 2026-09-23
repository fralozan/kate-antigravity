#include "pluginview.h"
#include "plugin.h"
#include "viewhelper.h"
#include "agyclient.h"
#include "chatsession.h"
#include "chatsessionmanager.h"
#include "chatwidget.h"
#include "settings.h"
#include "projectdetector.h"

#include <QDir>
#include <KTextEditor/MainWindow>
#include <KTextEditor/View>
#include <KTextEditor/Document>
#include <KActionCollection>
#include <KLocalizedString>
#include <KXMLGUIFactory>

#include <QAction>
#include <QMenu>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QToolButton>
#include <QIcon>
#include <QTabBar>
#include <QTimer>
#include <QPointer>
#include <QEvent>

namespace {

// Choose the workspace to open on startup: restore the last active one if its
// folder still exists, otherwise fall back to the General workspace.
ProjectInfo initialWorkspace()
{
    const QString last = AgySettings::instance()->lastWorkspacePath;
    if (!last.isEmpty() && QDir(last).exists()) {
        return ProjectDetector::detectForPath(last);
    }
    return ProjectInfo::createGeneral();
}

} // namespace

KateAntigravityPluginView::KateAntigravityPluginView(KateAntigravityPlugin *plugin, KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
    , KXMLGUIClient()
    , m_plugin(plugin)
    , m_mainWindow(mainWindow)
    , m_client(new AgyClient(this))
    , m_chatSession(ChatSessionManager::instance()->sessionForProject(initialWorkspace()))
{
    setComponentName(QStringLiteral("kateantigravity"), i18n("Antigravity"));
    setXMLFile(QStringLiteral("ui.rc"));

    setupActions();

    if (m_mainWindow) {
        if (m_mainWindow->guiFactory()) {
            m_mainWindow->guiFactory()->addClient(this);
        }

        // Posición de la barra lateral según la configuración (Left o Right)
        const auto initialPos = (AgySettings::instance()->chatSidebarPosition == 0)
            ? KTextEditor::MainWindow::Left
            : KTextEditor::MainWindow::Right;

        m_chatToolView = m_mainWindow->createToolView(
            m_plugin,
            QStringLiteral("kate_antigravity_chat"),
            initialPos,
            QIcon::fromTheme(QStringLiteral("dialog-messages")),
            i18n("Antigravity Chat")
        );

        if (m_chatToolView) {
            m_chatToolView->setMinimumWidth(280);
            m_chatToolView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            QLayout *toolViewLayout = m_chatToolView->layout();
            if (!toolViewLayout) {
                toolViewLayout = new QVBoxLayout(m_chatToolView);
            }
            toolViewLayout->setContentsMargins(0, 0, 0, 0);
            toolViewLayout->setSpacing(0);
            m_chatWidget = new ChatWidget(m_mainWindow, m_chatSession, m_chatToolView);
            m_chatWidget->setPlugin(m_plugin);
            m_chatWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            toolViewLayout->addWidget(m_chatWidget);
        }

        connect(AgySettings::instance(), &AgySettings::settingsChanged, this, [this]() {
            if (m_chatToolView && m_mainWindow) {
                const auto targetPos = (AgySettings::instance()->chatSidebarPosition == 0)
                    ? KTextEditor::MainWindow::Left
                    : KTextEditor::MainWindow::Right;
                m_mainWindow->moveToolView(m_chatToolView, targetPos);
            }
        });

        connect(m_mainWindow, &KTextEditor::MainWindow::viewCreated,
                this, &KateAntigravityPluginView::onViewCreated);
        connect(m_mainWindow, &KTextEditor::MainWindow::viewChanged,
                this, &KateAntigravityPluginView::onViewChanged);

        const auto views = m_mainWindow->views();
        for (KTextEditor::View *v : views) {
            registerView(v);
        }

        attachTabBarButtons();
        QTimer::singleShot(250, this, &KateAntigravityPluginView::attachTabBarButtons);
        QTimer::singleShot(1000, this, &KateAntigravityPluginView::attachTabBarButtons);
    }
}

KateAntigravityPluginView::~KateAntigravityPluginView()
{
    if (m_mainWindow && m_mainWindow->guiFactory()) {
        m_mainWindow->guiFactory()->removeClient(this);
    }
    qDeleteAll(m_helpers);
    m_helpers.clear();
}

KTextEditor::MainWindow *KateAntigravityPluginView::mainWindow() const
{
    return m_mainWindow;
}

AgyClient *KateAntigravityPluginView::client() const
{
    return m_client;
}

ChatSession *KateAntigravityPluginView::chatSession() const
{
    return m_chatWidget ? m_chatWidget->session() : m_chatSession;
}

ChatWidget *KateAntigravityPluginView::chatWidget() const
{
    return m_chatWidget;
}

void KateAntigravityPluginView::setupActions()
{
    KActionCollection *ac = actionCollection();

    QAction *triggerAct = ac->addAction(QStringLiteral("antigravity_trigger_suggestion"),
                                         this, &KateAntigravityPluginView::triggerSuggestion);
    triggerAct->setText(i18n("Trigger Antigravity Suggestion"));
    triggerAct->setIcon(QIcon::fromTheme(QStringLiteral("code-context")));
    ac->setDefaultShortcut(triggerAct, QKeySequence(Qt::ALT | Qt::Key_Backslash));

    QAction *clearAct = ac->addAction(QStringLiteral("antigravity_clear_suggestion"),
                                       this, &KateAntigravityPluginView::clearSuggestion);
    clearAct->setText(i18n("Clear Antigravity Suggestion"));

    QAction *chatAct = ac->addAction(QStringLiteral("antigravity_toggle_chat"),
                                     this, &KateAntigravityPluginView::toggleChat);
    chatAct->setText(i18n("Toggle Antigravity Chat"));
    chatAct->setIcon(QIcon::fromTheme(QStringLiteral("dialog-messages")));
    ac->setDefaultShortcut(chatAct, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A));

    QAction *sendSelectionAct = ac->addAction(QStringLiteral("antigravity_send_selection"), this, [this]() {
        sendSelectionToChat(m_mainWindow ? m_mainWindow->activeView() : nullptr, QString());
    });
    sendSelectionAct->setText(i18n("Send Selection to Chat"));
    sendSelectionAct->setIcon(QIcon::fromTheme(QStringLiteral("document-send")));

    QAction *explainAct = ac->addAction(QStringLiteral("antigravity_explain_code"), this, [this]() {
        sendSelectionToChat(m_mainWindow ? m_mainWindow->activeView() : nullptr,
                            i18n("Explain in detail what this code does, its logic and potential edge cases."));
    });
    explainAct->setText(i18n("Explain Code with Antigravity"));
    explainAct->setIcon(QIcon::fromTheme(QStringLiteral("help-about")));

    QAction *refactorAct = ac->addAction(QStringLiteral("antigravity_refactor_code"), this, [this]() {
        sendSelectionToChat(m_mainWindow ? m_mainWindow->activeView() : nullptr,
                            i18n("Refactor and optimize this code following best practices and clean code principles."));
    });
    refactorAct->setText(i18n("Refactor and Optimize Code"));
    refactorAct->setIcon(QIcon::fromTheme(QStringLiteral("tools-wizard")));

    QAction *bugsAct = ac->addAction(QStringLiteral("antigravity_find_bugs"), this, [this]() {
        sendSelectionToChat(m_mainWindow ? m_mainWindow->activeView() : nullptr,
                            i18n("Analyze this code for potential bugs, edge cases, or security issues."));
    });
    bugsAct->setText(i18n("Find Bugs or Vulnerabilities"));
    bugsAct->setIcon(QIcon::fromTheme(QStringLiteral("tools-report-bug")));

    QAction *testsAct = ac->addAction(QStringLiteral("antigravity_generate_tests"), this, [this]() {
        sendSelectionToChat(m_mainWindow ? m_mainWindow->activeView() : nullptr,
                            i18n("Generate comprehensive unit tests for this code."));
    });
    testsAct->setText(i18n("Generate Unit Tests"));
    testsAct->setIcon(QIcon::fromTheme(QStringLiteral("debug-run")));
}

void KateAntigravityPluginView::registerView(KTextEditor::View *view)
{
    if (!view || m_helpers.contains(view)) {
        return;
    }

    auto *helper = new AgyViewHelper(view, m_client, this);
    m_helpers.insert(view, helper);

    connect(view, &QObject::destroyed, this, &KateAntigravityPluginView::onViewDestroyed);
    connect(helper, &AgyViewHelper::triggerRequested, this, &KateAntigravityPluginView::triggerSuggestion);

    connect(view, &KTextEditor::View::selectionChanged, this, [this]() {
        if (m_chatWidget) {
            m_chatWidget->updateEditorContext();
        }
    });

    connect(view, &KTextEditor::View::contextMenuAboutToShow,
            this, &KateAntigravityPluginView::onContextMenuAboutToShow);

    attachTabBarButton(view);
}

namespace {
class TabBarVisibilityFilter : public QObject
{
public:
    TabBarVisibilityFilter(QWidget *targetButton, QObject *parent = nullptr)
        : QObject(parent), m_button(targetButton) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (m_button) {
            if (event->type() == QEvent::Hide) {
                m_button->hide();
            } else if (event->type() == QEvent::Show) {
                m_button->show();
            }
        }
        return QObject::eventFilter(obj, event);
    }

private:
    QPointer<QWidget> m_button;
};
} // namespace

void KateAntigravityPluginView::attachTabBarButtons()
{
    if (!m_mainWindow || !m_mainWindow->window()) {
        return;
    }

    const auto tabBars = m_mainWindow->window()->findChildren<QTabBar *>();
    for (QTabBar *tabBar : tabBars) {
        QWidget *viewSpace = tabBar->parentWidget();
        if (!viewSpace) {
            continue;
        }

        if (viewSpace->findChild<QToolButton *>(QStringLiteral("antigravity_tabbar_button"))) {
            continue;
        }

        auto *grid = qobject_cast<QGridLayout *>(viewSpace->layout());
        if (!grid) {
            continue;
        }

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

        auto *chatBtn = new QToolButton(viewSpace);
        chatBtn->setObjectName(QStringLiteral("antigravity_tabbar_button"));
        chatBtn->setIcon(QIcon::fromTheme(QStringLiteral("dialog-messages")));
        chatBtn->setToolTip(i18n("Chat with Antigravity (Ctrl+Alt+A)"));
        chatBtn->setAutoRaise(true);
        chatBtn->setCursor(Qt::PointingHandCursor);

        const auto siblingButtons = viewSpace->findChildren<QToolButton *>();
        for (auto *sibling : siblingButtons) {
            if (sibling != chatBtn && !sibling->icon().isNull()) {
                chatBtn->setIconSize(sibling->iconSize());
                break;
            }
        }

        connect(chatBtn, &QToolButton::clicked, this, &KateAntigravityPluginView::toggleChat);

        chatBtn->setVisible(tabBar->isVisible());
        tabBar->installEventFilter(new TabBarVisibilityFilter(chatBtn, chatBtn));

        grid->addWidget(chatBtn, 0, targetCol, Qt::AlignRight | Qt::AlignVCenter);

        // Asegurar que el urlBar (fila 1) y el editor stack (fila 2) abarquen todas las columnas
        for (int r = 1; r <= 2; ++r) {
            if (auto *item = grid->itemAtPosition(r, 0)) {
                if (auto *w = item->widget()) {
                    grid->removeWidget(w);
                    grid->addWidget(w, r, 0, 1, -1);
                }
            }
        }
    }
}

void KateAntigravityPluginView::attachTabBarButton(KTextEditor::View *view)
{
    if (view) {
        QWidget *w = view->parentWidget();
        while (w && w != m_mainWindow->window()) {
            if (qobject_cast<QGridLayout *>(w->layout())) {
                if (w->findChild<QTabBar *>() && !w->findChild<QToolButton *>(QStringLiteral("antigravity_tabbar_button"))) {
                    attachTabBarButtons();
                    return;
                }
            }
            w = w->parentWidget();
        }
    }
    attachTabBarButtons();
}

void KateAntigravityPluginView::onViewCreated(KTextEditor::View *view)
{
    registerView(view);
    attachTabBarButtons();
    QTimer::singleShot(100, this, &KateAntigravityPluginView::attachTabBarButtons);
}

void KateAntigravityPluginView::onViewChanged(KTextEditor::View *view)
{
    if (view && !m_helpers.contains(view)) {
        registerView(view);
    }
    attachTabBarButton(view);
    if (m_chatWidget) {
        m_chatWidget->updateEditorContext();
        if (view && view->document()) {
            // Let the chat widget decide (it applies pinning + precedence rules
            // and won't degrade a real workspace to "General").
            m_chatWidget->maybeAutoSwitchToDocument(view->document());
        }
    }
}

void KateAntigravityPluginView::onViewDestroyed(QObject *obj)
{
    auto *view = static_cast<KTextEditor::View *>(obj);
    if (m_helpers.contains(view)) {
        auto *helper = m_helpers.take(view);
        helper->detachView();
        helper->deleteLater();
    }
}

AgyViewHelper *KateAntigravityPluginView::helperForView(KTextEditor::View *view) const
{
    return m_helpers.value(view, nullptr);
}

void KateAntigravityPluginView::triggerSuggestion()
{
    if (!m_mainWindow) {
        return;
    }

    KTextEditor::View *activeView = m_mainWindow->activeView();
    AgyViewHelper *helper = helperForView(activeView);
    if (helper) {
        helper->triggerCompletionNow();
    }
}

void KateAntigravityPluginView::clearSuggestion()
{
    if (!m_mainWindow) {
        return;
    }

    KTextEditor::View *activeView = m_mainWindow->activeView();
    AgyViewHelper *helper = helperForView(activeView);
    if (helper) {
        helper->clearSuggestion();
    }
}

void KateAntigravityPluginView::toggleChat()
{
    if (!m_mainWindow || !m_chatToolView) {
        return;
    }

    if (m_chatToolView->isVisible()) {
        m_mainWindow->hideToolView(m_chatToolView);
    } else {
        m_mainWindow->showToolView(m_chatToolView);
        if (m_chatWidget) {
            m_chatWidget->focusInput();
        }
    }
}

void KateAntigravityPluginView::onContextMenuAboutToShow(KTextEditor::View *view, QMenu *menu)
{
    if (!view || !menu) {
        return;
    }

    // Limpiar elementos previos de Antigravity si el menú es reutilizado por Kate
    const auto existingActions = menu->actions();
    for (QAction *act : existingActions) {
        if (act && (act->objectName().startsWith(QStringLiteral("antigravity_"))
                    || act->text() == i18n("✨ Antigravity AI"))) {
            menu->removeAction(act);
            if (QMenu *sub = act->menu()) {
                delete sub;
            } else {
                delete act;
            }
        }
    }

    auto *sep = menu->addSeparator();
    sep->setObjectName(QStringLiteral("antigravity_separator"));

    auto *agyMenu = menu->addMenu(QIcon::fromTheme(QStringLiteral("dialog-messages")), i18n("✨ Antigravity AI"));
    agyMenu->setObjectName(QStringLiteral("antigravity_submenu"));
    if (agyMenu->menuAction()) {
        agyMenu->menuAction()->setObjectName(QStringLiteral("antigravity_menu_action"));
    }

    const QPointer<KTextEditor::View> v = view;

    if (view->selection()) {
        auto *sendAct = agyMenu->addAction(QIcon::fromTheme(QStringLiteral("document-send")),
                                           i18n("Send Selection to Chat"));
        connect(sendAct, &QAction::triggered, this, [this, v]() {
            if (v) {
                sendSelectionToChat(v.data(), QString());
            }
        });

        agyMenu->addSeparator();

        auto *explainAct = agyMenu->addAction(QIcon::fromTheme(QStringLiteral("help-about")),
                                              i18n("Explain Code"));
        connect(explainAct, &QAction::triggered, this, [this, v]() {
            if (v) {
                sendSelectionToChat(v.data(), i18n("Explain in detail what this code does, its logic and potential edge cases."));
            }
        });

        auto *refactorAct = agyMenu->addAction(QIcon::fromTheme(QStringLiteral("tools-wizard")),
                                               i18n("Refactor and Optimize"));
        connect(refactorAct, &QAction::triggered, this, [this, v]() {
            if (v) {
                sendSelectionToChat(v.data(), i18n("Refactor and optimize this code following best practices and clean code principles."));
            }
        });

        auto *bugsAct = agyMenu->addAction(QIcon::fromTheme(QStringLiteral("tools-report-bug")),
                                           i18n("Find Bugs or Vulnerabilities"));
        connect(bugsAct, &QAction::triggered, this, [this, v]() {
            if (v) {
                sendSelectionToChat(v.data(), i18n("Analyze this code for potential bugs, edge cases, or security issues."));
            }
        });

        auto *testsAct = agyMenu->addAction(QIcon::fromTheme(QStringLiteral("debug-run")),
                                            i18n("Generate Unit Tests"));
        connect(testsAct, &QAction::triggered, this, [this, v]() {
            if (v) {
                sendSelectionToChat(v.data(), i18n("Generate comprehensive unit tests for this code."));
            }
        });
    } else {
        auto *openChatAct = agyMenu->addAction(QIcon::fromTheme(QStringLiteral("dialog-messages")),
                                              i18n("Open Antigravity Chat"));
        openChatAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A));
        connect(openChatAct, &QAction::triggered, this, [this]() {
            if (m_mainWindow && m_chatToolView) {
                if (!m_chatToolView->isVisible()) {
                    m_mainWindow->showToolView(m_chatToolView);
                }
                if (m_chatWidget) {
                    m_chatWidget->focusInput();
                }
            }
        });

        auto *suggestAct = agyMenu->addAction(QIcon::fromTheme(QStringLiteral("code-context")),
                                             i18n("Suggest Code Here"));
        suggestAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Backslash));
        connect(suggestAct, &QAction::triggered, this, &KateAntigravityPluginView::triggerSuggestion);
    }
}

void KateAntigravityPluginView::sendSelectionToChat(KTextEditor::View *view, const QString &prompt)
{
    if (!m_mainWindow || !m_chatWidget) {
        return;
    }

    if (!view) {
        view = m_mainWindow->activeView();
    }
    if (!view || !view->document()) {
        return;
    }

    // Align the chat workspace with the document the selection came from,
    // honoring pinning and the no-degrade-to-General rule.
    m_chatWidget->maybeAutoSwitchToDocument(view->document());

    if (m_chatToolView && !m_chatToolView->isVisible()) {
        m_mainWindow->showToolView(m_chatToolView);
    }

    if (prompt.isEmpty()) {
        m_chatWidget->prepareContextQuery();
        return;
    }

    const QString fileName = view->document()->url().fileName();
    const QString lang = view->document()->highlightingMode();
    QString code;
    QString meta;

    if (view->selection()) {
        code = view->selectionText();
        meta = i18n("File: %1 (%2), Selection: lines %3-%4",
                    fileName.isEmpty() ? i18n("Untitled") : fileName,
                    lang,
                    view->selectionRange().start().line() + 1,
                    view->selectionRange().end().line() + 1);
    } else {
        const int curLine = view->cursorPosition().line();
        const int startLine = qMax(0, curLine - 25);
        const int endLine = qMin(view->document()->lines() - 1, curLine + 25);

        QStringList snippetLines;
        for (int l = startLine; l <= endLine; ++l) {
            snippetLines << view->document()->line(l);
        }
        code = snippetLines.join(QLatin1Char('\n'));
        meta = i18n("File: %1 (%2), Cursor at line %3",
                    fileName.isEmpty() ? i18n("Untitled") : fileName,
                    lang,
                    curLine + 1);
    }

    m_chatWidget->sendDirectQuery(prompt, code, meta);
}
