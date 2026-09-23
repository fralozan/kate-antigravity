#include "chatwidget.h"
#include "chatsession.h"
#include "chatsessionmanager.h"
#include "chatcompletionpopup.h"
#include "slashcommandrouter.h"
#include "mentionresolver.h"
#include "projectfileindexer.h"
#include "agyaccount.h"
#include "agymodels.h"
#include "symbolindex.h"
#include "editblockparser.h"
#include "editblockapplier.h"
#include "gitcommithelper.h"
#include "settings.h"
#include "plugin.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QFrame>
#include <QSplitter>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QScrollBar>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QGroupBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QStackedWidget>
#include <KLocalizedString>
#include <KTextEditor/MainWindow>
#include <KTextEditor/View>
#include <KTextEditor/Document>

// -----------------------------------------------------------------------------
// ChatInputEdit
// -----------------------------------------------------------------------------
ChatInputEdit::ChatInputEdit(QWidget *parent)
    : QTextEdit(parent)
{
    setPlaceholderText(i18n("Write a prompt, type / for commands, @ to mention files... (Ctrl+Enter to send)"));
    setAcceptRichText(false);
    setMinimumHeight(50);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(this, &QTextEdit::textChanged, this, &ChatInputEdit::onTextChanged);
    connect(ProjectFileIndexer::instance(), &ProjectFileIndexer::indexingFinished, this, [this](const QString &projectPath) {
        if (projectPath == m_projectPath && m_popup && m_popup->isVisible() && m_triggerPosition >= 0) {
            checkTriggerCompletion();
        }
    });
}

void ChatInputEdit::setCompletionPopup(ChatCompletionPopup *popup)
{
    m_popup = popup;
    if (m_popup) {
        connect(m_popup, &ChatCompletionPopup::itemSelected,
                this, &ChatInputEdit::onCompletionItemSelected);
    }
}

void ChatInputEdit::setProjectPath(const QString &projectPath)
{
    m_projectPath = projectPath;
    if (!m_projectPath.isEmpty()) {
        ProjectFileIndexer::instance()->startIndexing(m_projectPath);
    }
}

void ChatInputEdit::setMainWindow(KTextEditor::MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;
}

void ChatInputEdit::keyPressEvent(QKeyEvent *event)
{
    if (m_popup && m_popup->isVisible()) {
        if (m_popup->handleKeyPress(event)) {
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (event->modifiers() & Qt::ControlModifier) {
            event->accept();
            if (m_popup && m_popup->isVisible()) {
                m_popup->hide();
            }
            Q_EMIT submitRequested();
            return;
        }

        // Execute slash commands immediately upon Enter without Ctrl
        if (!(event->modifiers() & Qt::ShiftModifier)) {
            const QString trimmed = toPlainText().trimmed();
            if (SlashCommandRouter::isSlashCommand(trimmed)) {
                event->accept();
                if (m_popup && m_popup->isVisible()) {
                    m_popup->hide();
                }
                Q_EMIT submitRequested();
                return;
            }
        }
    }

    QTextEdit::keyPressEvent(event);
}

void ChatInputEdit::focusOutEvent(QFocusEvent *event)
{
    if (m_popup && m_popup->isVisible()) {
        QTimer::singleShot(200, m_popup, &QWidget::hide);
    }
    QTextEdit::focusOutEvent(event);
}

void ChatInputEdit::onTextChanged()
{
    checkTriggerCompletion();
}

void ChatInputEdit::checkTriggerCompletion()
{
    if (!m_popup) {
        return;
    }

    const QTextCursor cursor = textCursor();
    const QString text = toPlainText();
    const int pos = cursor.position();

    if (pos == 0 || text.isEmpty()) {
        m_popup->hide();
        m_triggerPosition = -1;
        return;
    }

    int start = pos - 1;
    while (start >= 0 && !text.at(start).isSpace()) {
        if (text.at(start) == QLatin1Char('/') || text.at(start) == QLatin1Char('@')) {
            if (start == 0 || text.at(start - 1).isSpace()) {
                break;
            }
        }
        --start;
    }

    if (start < 0 || (text.at(start) != QLatin1Char('/') && text.at(start) != QLatin1Char('@'))) {
        m_popup->hide();
        m_triggerPosition = -1;
        return;
    }

    const QChar trigger = text.at(start);
    const QString query = text.mid(start + 1, pos - (start + 1));
    m_triggerPosition = start;

    QList<CompletionItem> items;
    if (trigger == QLatin1Char('/')) {
        const auto commands = SlashCommandRouter::availableCommands();
        for (const auto &cmd : commands) {
            if (query.isEmpty() || cmd.name.startsWith(query, Qt::CaseInsensitive)) {
                CompletionItem item;
                item.type = CompletionItem::Type::SlashCommand;
                item.text = cmd.syntax;
                item.hint = cmd.description;
                item.insertText = QStringLiteral("/%1 ").arg(cmd.name);
                item.icon = QIcon::fromTheme(cmd.iconName);
                items.append(item);
            }
        }
    } else if (trigger == QLatin1Char('@')) {
        // Special (non-file) mentions, offered when the query is a prefix of one.
        struct SpecialMention {
            QString keyword;   // canonical keyword shown after '@'
            QString insert;    // text inserted (with trailing space)
            QString hint;
            QString iconName;
            QString iconFallback;
        };
        const QList<SpecialMention> specials = {
            { QStringLiteral("diagnostics"), QStringLiteral("@diagnostics "),
              i18n("[Special] Active file compiler marks and LSP diagnostics"),
              QStringLiteral("tools-report-bug"), QStringLiteral("dialog-warning") },
            { QStringLiteral("selection"), QStringLiteral("@selection "),
              i18n("[Special] The current editor selection"),
              QStringLiteral("edit-select-all"), QStringLiteral("edit-copy") },
            { QStringLiteral("git-diff"), QStringLiteral("@git-diff "),
              i18n("[Special] Uncommitted git changes (staged and unstaged)"),
              QStringLiteral("vcs-diff"), QStringLiteral("document-multiple") },
            { QStringLiteral("terminal"), QStringLiteral("@terminal "),
              i18n("[Special] Best-effort embedded terminal output"),
              QStringLiteral("utilities-terminal"), QStringLiteral("terminal") },
            { QStringLiteral("symbol:"), QStringLiteral("@symbol:"),
              i18n("[Special] Find a symbol definition, e.g. @symbol:MyClass"),
              QStringLiteral("code-context"), QStringLiteral("code-function") },
        };
        for (const auto &sp : specials) {
            if (query.isEmpty() || sp.keyword.startsWith(query, Qt::CaseInsensitive)) {
                CompletionItem item;
                item.type = CompletionItem::Type::FileMention;
                item.text = QStringLiteral("@%1").arg(sp.keyword);
                item.hint = sp.hint;
                item.insertText = sp.insert;
                item.icon = QIcon::fromTheme(sp.iconName, QIcon::fromTheme(sp.iconFallback));
                items.append(item);
            }
        }

        const auto files = ProjectFileIndexer::instance()->searchFiles(m_projectPath, query, m_mainWindow, 25);
        for (const auto &f : files) {
            CompletionItem item;
            item.type = CompletionItem::Type::FileMention;
            item.text = QStringLiteral("@%1").arg(f.relativePath);
            if (f.isDirectory) {
                item.hint = i18n("[Folder]");
                item.icon = QIcon::fromTheme(QStringLiteral("folder-development"), QIcon::fromTheme(QStringLiteral("folder")));
            } else if (f.isOpenInEditor) {
                item.hint = i18n("[Open] (%1 lines)", f.lineCount);
                item.icon = QIcon::fromTheme(QStringLiteral("document-open"), QIcon::fromTheme(QStringLiteral("text-x-generic")));
            } else {
                item.hint = QStringLiteral("(%1 lines)").arg(f.lineCount);
                item.icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));
            }
            item.insertText = QStringLiteral("@%1 ").arg(f.relativePath);
            items.append(item);
        }
    }

    if (items.isEmpty()) {
        m_popup->hide();
        return;
    }

    m_popup->setCompletions(items);
    const QRect curRect = cursorRect(cursor);
    const QPoint globalCursorPos = mapToGlobal(curRect.bottomLeft());
    m_popup->showAt(globalCursorPos + QPoint(0, 4));
}

void ChatInputEdit::onCompletionItemSelected(const CompletionItem &item)
{
    if (m_triggerPosition < 0) {
        return;
    }

    if (item.type == CompletionItem::Type::SlashCommand) {
        // Commands that do not take arguments execute immediately!
        if (!item.insertText.startsWith(QLatin1String("/model"))) {
            m_triggerPosition = -1;
            if (m_popup && m_popup->isVisible()) {
                m_popup->hide();
            }
            setText(item.insertText.trimmed());
            Q_EMIT submitRequested();
            return;
        }
    }

    QTextCursor cursor = textCursor();
    const int currentPos = cursor.position();

    cursor.setPosition(m_triggerPosition);
    cursor.setPosition(currentPos, QTextCursor::KeepAnchor);
    cursor.insertText(item.insertText);

    setTextCursor(cursor);
    setFocus();
    m_triggerPosition = -1;
}

// -----------------------------------------------------------------------------
// ChatWidget
// -----------------------------------------------------------------------------
ChatWidget::ChatWidget(KTextEditor::MainWindow *mainWindow, ChatSession *session, QWidget *parent)
    : QWidget(parent)
    , m_mainWindow(mainWindow)
    , m_session(session)
{
    setMinimumWidth(280);
    setMinimumHeight(250);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    if (!m_session) {
        m_session = ChatSessionManager::instance()->sessionForProject(ProjectInfo::createGeneral());
    }

    if (m_session && !m_session->projectPath().isEmpty()) {
        m_currentProject = ProjectDetector::detectForPath(m_session->projectPath());
    } else {
        m_currentProject = ProjectInfo::createGeneral();
    }

    setupUi();

    if (m_session) {
        connect(m_session, &ChatSession::messageAdded, this, &ChatWidget::onMessageAdded);
        connect(m_session, &ChatSession::streamingDelta, this, &ChatWidget::onStreamingDelta);
        connect(m_session, &ChatSession::generationFinished, this, &ChatWidget::onGenerationFinished);
        connect(m_session, &ChatSession::generationError, this, &ChatWidget::onGenerationError);
        connect(m_session, &ChatSession::statusChanged, this, &ChatWidget::onStatusChanged);
    }

    connect(m_projectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChatWidget::onProjectComboChanged);
    connect(ChatSessionManager::instance(), &ChatSessionManager::projectListChanged,
            this, &ChatWidget::refreshProjectCombo);

    m_streamRenderTimer = new QTimer(this);
    m_streamRenderTimer->setSingleShot(true);
    m_streamRenderTimer->setInterval(60); // ~16 renders/sec ceiling during streaming
    connect(m_streamRenderTimer, &QTimer::timeout, this, &ChatWidget::renderStreamingUpdate);

    connect(m_inputEdit, &ChatInputEdit::submitRequested, this, &ChatWidget::onSendClicked);
    connect(m_sendButton, &QPushButton::clicked, this, &ChatWidget::onSendClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &ChatWidget::onStopClicked);
    connect(m_clearButton, &QToolButton::clicked, this, &ChatWidget::onClearClicked);
    connect(m_resetButton, &QToolButton::clicked, this, &ChatWidget::onResetContextClicked);
    connect(m_browser, &QTextBrowser::anchorClicked, this, &ChatWidget::onAnchorClicked);

    refreshProjectCombo();
    updateEditorContext();
    updateAccountAndModelDisplay();
    renderAllMessages();
}

ChatWidget::~ChatWidget() = default;

void ChatWidget::updateAccountAndModelDisplay()
{
    const AgyAccountInfo account = AgyAccount::currentAccount();
    if (m_accountButton) {
        if (account.isAuthenticated && !account.email.isEmpty()) {
            QString labelText = account.email;
            if (labelText.length() > 22) {
                labelText = account.email.section(QLatin1Char('@'), 0, 0);
            }
            m_accountButton->setText(labelText);
            m_accountButton->setToolTip(i18n(
                "Google AI Account: %1\n"
                "Name: %2\n"
                "Auth: %3\n\n"
                "(Click to view session usage and statistics)",
                account.email,
                account.name.isEmpty() ? i18n("N/A") : account.name,
                account.authMethod
            ));
        } else {
            m_accountButton->setText(i18n("Guest"));
            m_accountButton->setToolTip(i18n("Not logged into Google AI.\n(Click to view details)"));
        }
    }

    if (m_modelBadge && m_session) {
        const QString modeStr = (m_session->backendMode() == AgyClient::BackendMode::AgyCli)
            ? QStringLiteral("CLI")
            : QStringLiteral("API");
        const QString currentModel = m_session->model();
        m_modelBadge->setText(QStringLiteral("● %1 | %2 ▾").arg(modeStr, currentModel));
        m_modelBadge->setToolTip(i18n("Connection: %1\nModel: %2\n\n(Click to switch model)", modeStr, currentModel));

        auto *modelMenu = new QMenu(m_modelBadge);
        const QStringList commonModels = AgyModels::commonModels();

        for (const auto &modelName : commonModels) {
            auto *action = modelMenu->addAction(modelName);
            action->setCheckable(true);
            action->setChecked(modelName == currentModel);
            connect(action, &QAction::triggered, this, [this, modelName]() {
                if (m_session) {
                    m_session->setModel(modelName);
                    updateAccountAndModelDisplay();
                    m_statusLabel->setText(i18n("Model switched to: %1", modelName));
                }
            });
        }

        modelMenu->addSeparator();
        auto *customAction = modelMenu->addAction(i18n("Custom model... (/model)"));
        connect(customAction, &QAction::triggered, this, [this]() {
            if (m_inputEdit) {
                m_inputEdit->setText(QStringLiteral("/model "));
                m_inputEdit->setFocus();
                QTextCursor c = m_inputEdit->textCursor();
                c.movePosition(QTextCursor::End);
                m_inputEdit->setTextCursor(c);
            }
        });

        if (m_modelBadge->menu()) {
            m_modelBadge->menu()->deleteLater();
        }
        m_modelBadge->setMenu(modelMenu);
    }
}

void ChatWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateEditorContext();
}

void ChatWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::ThemeChange
        || event->type() == QEvent::StyleChange) {

        // Force reload stylesheets to re-evaluate palette(...) macros with the newly active palette
        const auto widgets = findChildren<QWidget *>();
        for (auto *w : widgets) {
            const QString ss = w->styleSheet();
            if (!ss.isEmpty()) {
                w->setStyleSheet(QString());
                w->setStyleSheet(ss);
            }
            w->style()->unpolish(w);
            w->style()->polish(w);
            w->update();
        }

        if (m_browser) {
            m_browser->document()->setDefaultStyleSheet(
                QStringLiteral(
                    "body { font-family: sans-serif; font-size: 13px; color: palette(text); background-color: transparent; }\n"
                    "a { color: palette(highlight); text-decoration: none; }\n"
                    "a:hover { text-decoration: underline; }\n"
                )
            );
        }

        updateAccountAndModelDisplay();
        renderAllMessages();
        update();
    }
}

void ChatWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // 0. Barra de Cabecera Superior del Panel
    auto *headerWidget = new QWidget(this);
    headerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(6);

    auto *headerIcon = new QLabel(headerWidget);
    headerIcon->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-messages")).pixmap(16, 16));

    m_projectCombo = new QComboBox(headerWidget);
    m_projectCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_projectCombo->setToolTip(i18n("Select active project chat session (right-click to rename or remove)"));
    m_projectCombo->setStyleSheet(QStringLiteral("QComboBox { font-size: 11px; padding: 2px 4px; }"));
    m_projectCombo->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_projectCombo, &QWidget::customContextMenuRequested,
            this, &ChatWidget::onProjectComboContextMenu);

    m_accountButton = new QToolButton(headerWidget);
    m_accountButton->setIcon(QIcon::fromTheme(QStringLiteral("user-identity"), QIcon::fromTheme(QStringLiteral("im-user"))));
    m_accountButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_accountButton->setCursor(Qt::PointingHandCursor);
    m_accountButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "  color: palette(text);"
        "  border: 1px solid palette(midlight);"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  background-color: palette(alternate-base);"
        "}"
        "QToolButton:hover {"
        "  background-color: palette(highlight);"
        "  color: palette(highlighted-text);"
        "}"
    ));
    connect(m_accountButton, &QToolButton::clicked, this, [this]() {
        SlashCommandRouter::execute(QStringLiteral("/usage"), this, m_session, m_mainWindow, m_plugin);
    });

    m_clearButton = new QToolButton(headerWidget);
    m_clearButton->setAutoRaise(true);
    m_clearButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear"), QIcon::fromTheme(QStringLiteral("edit-clear-history"))));
    m_clearButton->setText(i18n("Clear"));
    m_clearButton->setToolTip(i18n("Clear Chat (Ctrl+L)\nClears the visible dialogue while preserving active context in memory."));
    m_clearButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  font-size: 11px;"
        "  padding: 2px 4px;"
        "  border-radius: 3px;"
        "  color: palette(text);"
        "}"
        "QToolButton:hover {"
        "  background-color: palette(alternate-base);"
        "}"
    ));

    m_resetButton = new QToolButton(headerWidget);
    m_resetButton->setAutoRaise(true);
    m_resetButton->setIcon(QIcon::fromTheme(QStringLiteral("document-new"), QIcon::fromTheme(QStringLiteral("view-refresh"))));
    m_resetButton->setText(i18n("Reset"));
    m_resetButton->setToolTip(i18n("Reset Context (/reset)\nStarts a new conversation from scratch and clears memory."));
    m_resetButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  font-size: 11px;"
        "  padding: 2px 4px;"
        "  border-radius: 3px;"
        "  color: palette(text);"
        "}"
        "QToolButton:hover {"
        "  background-color: palette(alternate-base);"
        "}"
    ));

    m_pinButton = new QToolButton(headerWidget);
    m_pinButton->setAutoRaise(true);
    m_pinButton->setCheckable(true);
    m_pinButton->setCursor(Qt::PointingHandCursor);
    connect(m_pinButton, &QToolButton::toggled, this, [this](bool on) {
        m_workspacePinned = on;
        updatePinButton();
        m_statusLabel->setText(on ? i18n("Workspace pinned (auto-switch off)")
                                  : i18n("Workspace unpinned (auto-switch on)"));
    });

    // Explicit workspace-actions button (⋯). A discoverable alternative to the
    // combo's right-click menu, which is unreliable on QComboBox.
    m_workspaceMenuButton = new QToolButton(headerWidget);
    m_workspaceMenuButton->setAutoRaise(true);
    m_workspaceMenuButton->setCursor(Qt::PointingHandCursor);
    m_workspaceMenuButton->setPopupMode(QToolButton::InstantPopup);
    m_workspaceMenuButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu"),
                                   QIcon::fromTheme(QStringLiteral("overflow-menu"))));
    m_workspaceMenuButton->setText(QStringLiteral("⋯"));
    m_workspaceMenuButton->setToolTip(i18n("Workspace actions (add, rename, remove)"));
    {
        auto *wsMenu = new QMenu(m_workspaceMenuButton);
        // Rebuild items each time it's shown so labels reflect the current one.
        connect(wsMenu, &QMenu::aboutToShow, this, [this, wsMenu]() {
            wsMenu->clear();
            wsMenu->addAction(QIcon::fromTheme(QStringLiteral("folder-add"), QIcon::fromTheme(QStringLiteral("list-add"))),
                              i18n("Add workspace…"), this, &ChatWidget::addWorkspaceInteractive);

            const bool manageable = m_currentProject.isValid();
            QAction *renameAct = wsMenu->addAction(QIcon::fromTheme(QStringLiteral("edit-rename")),
                                                   i18n("Rename current workspace…"),
                                                   this, [this]() { renameCurrentWorkspace(); });
            QAction *removeAct = wsMenu->addAction(QIcon::fromTheme(QStringLiteral("list-remove")),
                                                   i18n("Remove current workspace"),
                                                   this, [this]() { removeCurrentWorkspace(); });
            renameAct->setEnabled(manageable);
            removeAct->setEnabled(manageable);
        });
        m_workspaceMenuButton->setMenu(wsMenu);
    }

    m_searchButton = new QToolButton(headerWidget);
    m_searchButton->setAutoRaise(true);
    m_searchButton->setCheckable(true);
    m_searchButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-find"), QIcon::fromTheme(QStringLiteral("search"))));
    m_searchButton->setToolTip(i18n("Search in conversation history"));

    headerLayout->addWidget(headerIcon, 0, Qt::AlignVCenter);
    headerLayout->addWidget(m_projectCombo, 1, Qt::AlignVCenter);
    headerLayout->addWidget(m_workspaceMenuButton, 0, Qt::AlignVCenter);
    headerLayout->addWidget(m_pinButton, 0, Qt::AlignVCenter);
    headerLayout->addWidget(m_accountButton, 0, Qt::AlignVCenter);
    headerLayout->addWidget(m_searchButton, 0, Qt::AlignVCenter);
    headerLayout->addWidget(m_clearButton, 0, Qt::AlignVCenter);
    headerLayout->addWidget(m_resetButton, 0, Qt::AlignVCenter);

    updatePinButton();

    mainLayout->addWidget(headerWidget);

    // Search field (hidden until toggled) that filters the visible history.
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(i18n("Search messages..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setVisible(false);
    mainLayout->addWidget(m_searchEdit);

    connect(m_searchButton, &QToolButton::toggled, this, [this](bool on) {
        m_searchEdit->setVisible(on);
        if (on) {
            m_searchEdit->setFocus();
        } else {
            m_searchEdit->clear();
            m_searchQuery.clear();
            renderAllMessages();
        }
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_searchQuery = text;
        renderAllMessages();
    });

    // QSplitter Vertical: permite que el usuario redimensione el input y el historial a voluntad
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    splitter->setChildrenCollapsible(false);
    splitter->setStyleSheet(QStringLiteral(
        "QSplitter::handle:vertical {"
        "  height: 5px;"
        "  background-color: palette(midlight);"
        "  border-radius: 2px;"
        "  margin: 1px 16px;"
        "}"
        "QSplitter::handle:vertical:hover {"
        "  background-color: palette(highlight);"
        "}"
    ));

    // 1. Tarjeta Superior: Input del Chat y Acciones (redimensionable con el splitter)
    auto *inputCard = new QFrame(splitter);
    inputCard->setObjectName(QStringLiteral("inputCard"));
    inputCard->setFrameShape(QFrame::StyledPanel);
    inputCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    inputCard->setStyleSheet(QStringLiteral(
        "QFrame#inputCard {"
        "  background-color: palette(base);"
        "  border: 1px solid palette(mid);"
        "  border-radius: 6px;"
        "}"
    ));
    auto *inputLayout = new QVBoxLayout(inputCard);
    inputLayout->setContentsMargins(6, 6, 6, 6);
    inputLayout->setSpacing(4);

    // Campo de texto de entrada: elástico
    m_inputEdit = new ChatInputEdit(inputCard);
    m_inputEdit->setFrameShape(QFrame::NoFrame);
    m_inputEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_inputEdit->setStyleSheet(QStringLiteral("QTextEdit { background: transparent; font-size: 13px; color: palette(text); }"));
    inputLayout->addWidget(m_inputEdit, 1);

    m_popup = new ChatCompletionPopup(this);
    m_inputEdit->setCompletionPopup(m_popup);
    m_inputEdit->setMainWindow(m_mainWindow);
    if (m_session) {
        m_inputEdit->setProjectPath(m_session->projectPath());
    }

    // Fila 1 de controles: Checkbox con texto claro, desmarcado por defecto, y detalle de archivo
    auto *contextRow = new QHBoxLayout();
    contextRow->setContentsMargins(2, 2, 2, 2);
    contextRow->setSpacing(6);

    m_includeContextCheck = new QCheckBox(i18n("Attach current file"), inputCard);
    m_includeContextCheck->setChecked(false); // DESMARCADO por defecto
    m_includeContextCheck->setToolTip(i18n("If checked, includes the content or selection of the active file in your prompt"));
    m_includeContextCheck->setStyleSheet(QStringLiteral("QCheckBox { font-size: 11px; }"));

    m_contextDetailLabel = new QLabel(inputCard);
    m_contextDetailLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-size: 11px;"
        "  padding: 1px 5px;"
        "  border-radius: 3px;"
        "  background-color: palette(alternate-base);"
        "  color: palette(text);"
        "}"
    ));
    m_contextDetailLabel->setEnabled(false);
    connect(m_includeContextCheck, &QCheckBox::toggled, m_contextDetailLabel, &QLabel::setEnabled);

    m_attachImageButton = new QToolButton(inputCard);
    m_attachImageButton->setAutoRaise(true);
    m_attachImageButton->setIcon(QIcon::fromTheme(QStringLiteral("insert-image"), QIcon::fromTheme(QStringLiteral("image-x-generic"))));
    m_attachImageButton->setToolTip(i18n("Attach an image (Direct Gemini API only)"));
    m_attachImageButton->setCursor(Qt::PointingHandCursor);
    connect(m_attachImageButton, &QToolButton::clicked, this, &ChatWidget::onAttachImageClicked);

    contextRow->addWidget(m_includeContextCheck);
    contextRow->addWidget(m_contextDetailLabel, 1);
    contextRow->addWidget(m_attachImageButton, 0, Qt::AlignVCenter);
    inputLayout->addLayout(contextRow);

    // Fila 2 de controles: Estado y Botones principales (nativos de KDE)
    auto *actionsRow = new QHBoxLayout();
    actionsRow->setContentsMargins(2, 4, 2, 2);
    actionsRow->setSpacing(8);

    m_statusLabel = new QLabel(i18n("Ready"), inputCard);
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: palette(placeholder-text);"));

    m_modelBadge = new QToolButton(inputCard);
    m_modelBadge->setAutoRaise(true);
    m_modelBadge->setCursor(Qt::PointingHandCursor);
    m_modelBadge->setPopupMode(QToolButton::InstantPopup);
    m_modelBadge->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  font-size: 10px;"
        "  font-weight: bold;"
        "  padding: 2px 6px;"
        "  border-radius: 3px;"
        "  background-color: palette(alternate-base);"
        "  color: palette(text);"
        "  border: 1px solid palette(midlight);"
        "}"
        "QToolButton:hover {"
        "  background-color: palette(midlight);"
        "}"
        "QToolButton::menu-indicator { image: none; width: 0px; }"
    ));

    m_stopButton = new QPushButton(QIcon::fromTheme(QStringLiteral("process-stop")), i18n("Stop"), inputCard);
    m_stopButton->setIconSize(QSize(16, 16));
    m_stopButton->setToolTip(i18n("Stop ongoing generation"));
    m_stopButton->setVisible(false);

    m_sendButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-send")), i18n("Send"), inputCard);
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setIconSize(QSize(16, 16));
    m_sendButton->setToolTip(i18n("Send prompt (Ctrl+Enter)"));

    actionsRow->addWidget(m_statusLabel, 1, Qt::AlignVCenter);
    actionsRow->addWidget(m_modelBadge, 0, Qt::AlignVCenter);
    actionsRow->addWidget(m_stopButton, 0, Qt::AlignVCenter);
    actionsRow->addWidget(m_sendButton, 0, Qt::AlignVCenter);
    inputLayout->addLayout(actionsRow);

    splitter->addWidget(inputCard);

    // 2. Historial de Conversación Inferior (creciendo hacia abajo)
    // The lower area is a stack: page 0 is the conversation browser, page 1 is
    // a native "cleared" panel (label + Restore/Delete buttons) shown when the
    // history is hidden and nothing is visible below it.
    m_historyStack = new QStackedWidget(splitter);

    m_browser = new QTextBrowser(m_historyStack);
    m_browser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_browser->setOpenLinks(false);
    m_browser->setOpenExternalLinks(false);
    m_browser->document()->setDocumentMargin(6);
    m_browser->setStyleSheet(QStringLiteral("QTextBrowser { background-color: transparent; border: none; }"));
    m_browser->document()->setDefaultStyleSheet(
        QStringLiteral(
            "body { font-family: sans-serif; font-size: 13px; color: palette(text); background-color: transparent; }\n"
            "a { color: palette(highlight); text-decoration: none; }\n"
            "a:hover { text-decoration: underline; }\n"
        )
    );
    m_historyStack->addWidget(m_browser); // index 0

    // Native "cleared" panel with real Kate/Breeze buttons.
    m_clearedPanel = new QWidget(m_historyStack);
    {
        auto *outer = new QVBoxLayout(m_clearedPanel);
        outer->addStretch();

        auto *icon = new QLabel(QStringLiteral("🧹"), m_clearedPanel);
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet(QStringLiteral("font-size: 22px;"));

        auto *title = new QLabel(i18n("Hidden messages."), m_clearedPanel);
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet(QStringLiteral("font-weight: bold; color: palette(text);"));

        auto *subtitle = new QLabel(
            i18n("The assistant still remembers previous context and messages."), m_clearedPanel);
        subtitle->setAlignment(Qt::AlignCenter);
        subtitle->setWordWrap(true);
        subtitle->setStyleSheet(QStringLiteral("color: palette(placeholder-text); font-size: 11px;"));

        auto *btnRow = new QHBoxLayout();
        btnRow->addStretch();
        m_restoreButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-history"),
                                          QIcon::fromTheme(QStringLiteral("edit-undo"))),
                                          i18n("Restore messages"), m_clearedPanel);
        m_purgeButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                        i18n("Delete permanently"), m_clearedPanel);
        btnRow->addWidget(m_restoreButton);
        btnRow->addWidget(m_purgeButton);
        btnRow->addStretch();

        connect(m_restoreButton, &QPushButton::clicked, this, [this]() {
            if (m_session) {
                m_session->setVisibleStartIndex(0);
            }
            renderAllMessages();
            m_statusLabel->setText(i18n("Chat history restored"));
        });
        connect(m_purgeButton, &QPushButton::clicked, this, &ChatWidget::purgeHiddenMessagesConfirmed);

        outer->addWidget(icon);
        outer->addWidget(title);
        outer->addWidget(subtitle);
        outer->addSpacing(10);
        outer->addLayout(btnRow);
        outer->addStretch();
    }
    m_historyStack->addWidget(m_clearedPanel); // index 1

    splitter->addWidget(m_historyStack);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>{135, 450});

    mainLayout->addWidget(splitter, 1);
}

void ChatWidget::focusInput()
{
    if (m_inputEdit) {
        m_inputEdit->setFocus();
    }
}

void ChatWidget::updateEditorContext()
{
    if (!m_mainWindow) {
        m_contextDetailLabel->setText(i18n("No editor"));
        m_contextDetailLabel->setToolTip(QString());
        return;
    }

    KTextEditor::View *activeView = m_mainWindow->activeView();
    if (!activeView || !activeView->document()) {
        m_contextDetailLabel->setText(i18n("No document"));
        m_contextDetailLabel->setToolTip(QString());
        return;
    }

    QString fileName = activeView->document()->url().fileName();
    if (fileName.isEmpty()) {
        fileName = i18n("Untitled");
    }

    QString fullText;
    if (activeView->selection()) {
        const int lines = activeView->selectionRange().numberOfLines() + 1;
        fullText = i18n("📄 %1 (%2 lines)", fileName, lines);
    } else {
        fullText = i18n("📄 %1", fileName);
    }

    const int availableWidth = qMax(160, m_contextDetailLabel->width() > 0 ? m_contextDetailLabel->width() : 250);
    QFontMetrics fm(m_contextDetailLabel->font());
    QString elided = fm.elidedText(fullText, Qt::ElideMiddle, availableWidth);
    m_contextDetailLabel->setText(elided);
    m_contextDetailLabel->setToolTip(activeView->document()->url().toDisplayString());
}

void ChatWidget::sendDirectQuery(const QString &prompt, const QString &code, const QString &meta)
{
    if (m_session->isGenerating()) {
        m_session->cancelGeneration();
    }

    m_includeContextCheck->setChecked(true);
    updateEditorContext();

    m_inputEdit->clear();
    m_sendButton->setEnabled(false);
    m_stopButton->setVisible(true);

    m_session->sendMessage(prompt, code, meta);
}

void ChatWidget::prepareContextQuery(const QString &initialPrompt)
{
    m_includeContextCheck->setChecked(true);
    updateEditorContext();

    if (!initialPrompt.isEmpty()) {
        m_inputEdit->setPlainText(initialPrompt);
        QTextCursor cursor = m_inputEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_inputEdit->setTextCursor(cursor);
    }
    focusInput();
}

void ChatWidget::setPlugin(KateAntigravityPlugin *plugin)
{
    m_plugin = plugin;
}

void ChatWidget::setSession(ChatSession *session)
{
    if (m_session == session) {
        return;
    }

    if (m_session) {
        disconnect(m_session, &ChatSession::messageAdded, this, &ChatWidget::onMessageAdded);
        disconnect(m_session, &ChatSession::streamingDelta, this, &ChatWidget::onStreamingDelta);
        disconnect(m_session, &ChatSession::generationFinished, this, &ChatWidget::onGenerationFinished);
        disconnect(m_session, &ChatSession::generationError, this, &ChatWidget::onGenerationError);
        disconnect(m_session, &ChatSession::statusChanged, this, &ChatWidget::onStatusChanged);

        if (m_inputEdit) {
            m_session->setDraftText(m_inputEdit->toPlainText());
        }
    }

    m_session = session;
    // The hidden-messages boundary is restored from the session itself.

    if (m_session) {
        connect(m_session, &ChatSession::messageAdded, this, &ChatWidget::onMessageAdded);
        connect(m_session, &ChatSession::streamingDelta, this, &ChatWidget::onStreamingDelta);
        connect(m_session, &ChatSession::generationFinished, this, &ChatWidget::onGenerationFinished);
        connect(m_session, &ChatSession::generationError, this, &ChatWidget::onGenerationError);
        connect(m_session, &ChatSession::statusChanged, this, &ChatWidget::onStatusChanged);

        if (m_inputEdit) {
            m_inputEdit->setPlainText(m_session->draftText());
            m_inputEdit->setProjectPath(m_session->projectPath());
        }

        m_stopButton->setVisible(m_session->isGenerating());
        m_sendButton->setEnabled(!m_session->isGenerating());
        if (m_session->isGenerating()) {
            m_statusLabel->setText(i18n("Generating response..."));
        } else {
            m_statusLabel->setText(i18n("Ready"));
        }
    }

    updateAccountAndModelDisplay();
    renderAllMessages();
}

void ChatWidget::switchToProject(const ProjectInfo &info)
{
    m_currentProject = info;
    ChatSession *newSession = ChatSessionManager::instance()->sessionForProject(info);

    // Remember the active workspace so it can be restored on next launch.
    AgySettings::instance()->lastWorkspacePath = info.rootPath;
    AgySettings::instance()->save();

    refreshProjectCombo();

    if (m_projectCombo) {
        m_projectCombo->blockSignals(true);
        for (int i = 0; i < m_projectCombo->count(); ++i) {
            if (m_projectCombo->itemData(i).toString() == info.rootPath) {
                m_projectCombo->setCurrentIndex(i);
                break;
            }
        }
        m_projectCombo->blockSignals(false);
    }

    setSession(newSession);
    if (m_inputEdit) {
        m_inputEdit->setProjectPath(info.rootPath);
    }
}

void ChatWidget::updatePinButton()
{
    if (!m_pinButton) {
        return;
    }
    if (m_workspacePinned) {
        m_pinButton->setIcon(QIcon::fromTheme(QStringLiteral("window-pin"), QIcon::fromTheme(QStringLiteral("lock"))));
        m_pinButton->setToolTip(i18n("Workspace pinned: the chat won't follow the active file. Click to unpin."));
    } else {
        m_pinButton->setIcon(QIcon::fromTheme(QStringLiteral("window-unpin"), QIcon::fromTheme(QStringLiteral("unlock"))));
        m_pinButton->setToolTip(i18n("Workspace follows the active file. Click to pin the current workspace."));
    }
    if (m_pinButton->isChecked() != m_workspacePinned) {
        QSignalBlocker blocker(m_pinButton);
        m_pinButton->setChecked(m_workspacePinned);
    }
}

void ChatWidget::addWorkspaceInteractive()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, i18n("Add workspace folder"), QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (dir.isEmpty()) {
        // User cancelled: restore the combo selection to the current workspace.
        refreshProjectCombo();
        return;
    }

    // Detect project metadata (git/markers) for the chosen folder; fall back to
    // a plain folder workspace rooted at the selected directory.
    ProjectInfo info = ProjectDetector::detectForPath(dir);
    if (info.rootPath.isEmpty()) {
        info.rootPath = QDir(dir).absolutePath();
        info.name = QFileInfo(info.rootPath).fileName();
    }

    switchToProject(info);
    m_statusLabel->setText(i18n("Workspace added: %1", info.displayName()));
}

void ChatWidget::onProjectComboContextMenu(const QPoint &pos)
{
    if (!m_projectCombo || !m_currentProject.isValid()) {
        return;
    }

    QMenu menu(this);
    QAction *renameAct = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18n("Rename workspace…"));
    QAction *removeAct = menu.addAction(QIcon::fromTheme(QStringLiteral("list-remove")), i18n("Remove workspace"));

    QAction *chosen = menu.exec(m_projectCombo->mapToGlobal(pos));
    if (chosen == renameAct) {
        renameCurrentWorkspace();
    } else if (chosen == removeAct) {
        removeCurrentWorkspace();
    }
}

void ChatWidget::renameCurrentWorkspace()
{
    const QString rootPath = m_currentProject.rootPath;
    if (rootPath.isEmpty()) {
        return; // Global can't be renamed.
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, i18n("Rename workspace"), i18n("New name:"),
        QLineEdit::Normal, m_currentProject.name, &ok);
    if (ok && !newName.trimmed().isEmpty()) {
        ChatSessionManager::instance()->renameProject(rootPath, newName.trimmed());
        m_currentProject.name = newName.trimmed();
        refreshProjectCombo();
        m_statusLabel->setText(i18n("Workspace renamed"));
    }
}

void ChatWidget::removeCurrentWorkspace()
{
    const QString rootPath = m_currentProject.rootPath;
    if (rootPath.isEmpty()) {
        return; // Global can't be removed.
    }

    const auto btn = QMessageBox::question(
        this, i18n("Remove workspace"),
        i18n("Remove this workspace and its saved conversation?\n\n%1", rootPath),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn == QMessageBox::Yes) {
        ChatSessionManager::instance()->removeProject(rootPath);
        switchToProject(ProjectInfo::createGeneral());
        m_statusLabel->setText(i18n("Workspace removed"));
    }
}

void ChatWidget::maybeAutoSwitchToDocument(KTextEditor::Document *doc)
{
    // Respect the user's explicit choices: never auto-switch while pinned, or
    // when the feature is disabled in settings.
    if (m_workspacePinned || !AgySettings::instance()->autoSwitchProjectChat) {
        return;
    }

    const ProjectInfo info = ProjectDetector::detectForDocument(doc);

    // If the document doesn't resolve to a real project (e.g. an unsaved buffer
    // or a file with no project markers), keep the current workspace instead of
    // degrading to "General" — this is what previously clobbered a manual pick.
    if (info.rootPath.isEmpty()) {
        return;
    }

    // Only switch when it's actually a different project than the active one.
    if (info.rootPath == m_currentProject.rootPath) {
        return;
    }

    switchToProject(info);
}

void ChatWidget::onProjectComboChanged(int index)
{
    if (index < 0 || !m_projectCombo || index >= m_projectCombo->count()) {
        return;
    }

    const QString path = m_projectCombo->itemData(index).toString();

    // Sentinel "Add workspace…" row: open a folder picker instead of switching.
    if (path == QLatin1String("__add_workspace__")) {
        addWorkspaceInteractive();
        return;
    }

    ProjectInfo targetProject;
    const auto known = ChatSessionManager::instance()->knownProjects();
    for (const auto &p : known) {
        if (p.rootPath == path) {
            targetProject = p;
            break;
        }
    }
    if (targetProject.name.isEmpty()) {
        targetProject = path.isEmpty() ? ProjectInfo::createGeneral() : ProjectDetector::detectForPath(path);
    }

    switchToProject(targetProject);
}

void ChatWidget::refreshProjectCombo()
{
    if (!m_projectCombo) {
        return;
    }

    const QString currentPath = m_projectCombo->currentData().toString();
    m_projectCombo->blockSignals(true);
    m_projectCombo->clear();

    const auto known = ChatSessionManager::instance()->knownProjects();
    int selectedIdx = 0;

    for (int i = 0; i < known.size(); ++i) {
        const auto &proj = known.at(i);
        QIcon icon = QIcon::fromTheme(proj.isGlobal() ? QStringLiteral("dialog-messages") : QStringLiteral("folder-development"));
        m_projectCombo->addItem(icon, proj.displayName(), proj.rootPath);
        m_projectCombo->setItemData(i, proj.rootPath.isEmpty() ? i18n("General workspace") : proj.rootPath, Qt::ToolTipRole);

        if (proj.rootPath == currentPath || (proj.rootPath == m_currentProject.rootPath && !m_currentProject.rootPath.isEmpty())) {
            selectedIdx = i;
        }
    }

    // Sentinel action row: opens a folder picker to add a new workspace.
    m_projectCombo->insertSeparator(m_projectCombo->count());
    m_projectCombo->addItem(QIcon::fromTheme(QStringLiteral("folder-add"), QIcon::fromTheme(QStringLiteral("list-add"))),
                            i18n("Add workspace…"), QStringLiteral("__add_workspace__"));

    m_projectCombo->setCurrentIndex(selectedIdx);
    m_projectCombo->blockSignals(false);
}

void ChatWidget::onSendClicked()
{
    const QString userText = m_inputEdit->toPlainText().trimmed();
    if (userText.isEmpty()) {
        return;
    }

    // 1. Interceptar comandos slash
    if (SlashCommandRouter::isSlashCommand(userText)) {
        m_inputEdit->clear();
        SlashCommandRouter::execute(userText, this, m_session, m_mainWindow, m_plugin);
        return;
    }

    // 2. Extraer y resolver menciones @archivo
    QString contextCode;
    QString contextMeta;

    const QString projPath = m_session ? m_session->projectPath() : QString();
    MentionResolution mentions = MentionResolver::resolveMentions(userText, projPath, m_mainWindow);
    if (!mentions.assembledContext.isEmpty()) {
        contextCode = mentions.assembledContext;
        contextMeta = i18n("Mentioned: %1", mentions.resolvedFiles.join(QStringLiteral(", ")));
    }

    // Lightweight semantic enrichment: if the prompt references identifiers that
    // resolve to symbol definitions in the project, attach them automatically
    // (unless the user already pulled symbols in explicitly via @symbol:).
    if (!projPath.isEmpty() && !userText.contains(QStringLiteral("@symbol"))
        && !userText.contains(QStringLiteral("@sym"))) {
        const QStringList candidates = SymbolIndex::extractCandidateIdentifiers(userText, 4);
        QStringList symbolBlocks;
        QStringList enriched;
        for (const QString &cand : candidates) {
            const auto defs = SymbolIndex::findDefinitions(projPath, cand, 1);
            if (defs.isEmpty()) {
                continue;
            }
            const auto &sm = defs.first();
            const QString ext = QFileInfo(sm.fullPath).suffix().toLower();
            symbolBlocks << QStringLiteral("#### %1 (%2:%3)\n```%4\n%5\n```")
                                .arg(sm.name, sm.relativePath, QString::number(sm.line), ext, sm.snippet);
            enriched << sm.name;
            if (enriched.size() >= 2) {
                break; // keep enrichment small and cheap
            }
        }
        if (!symbolBlocks.isEmpty()) {
            const QString enrichmentContext =
                QStringLiteral("### Possibly-relevant symbol definitions (auto-detected)\n\n")
                + symbolBlocks.join(QStringLiteral("\n\n"));
            if (contextCode.isEmpty()) {
                contextCode = enrichmentContext;
            } else {
                contextCode += QStringLiteral("\n\n") + enrichmentContext;
            }
            const QString note = i18n("Auto-context: %1", enriched.join(QStringLiteral(", ")));
            contextMeta = contextMeta.isEmpty() ? note : (contextMeta + QStringLiteral(" | ") + note);
        }
    }

    if (m_session) {
        for (const QString &warning : mentions.warnings) {
            m_session->addSystemMessage(warning);
        }
    }

    // 3. Adjuntar archivo activo si está marcado
    if (m_includeContextCheck->isChecked() && m_mainWindow) {
        KTextEditor::View *activeView = m_mainWindow->activeView();
        if (activeView && activeView->document()) {
            const QString fileName = activeView->document()->url().fileName();
            const QString lang = activeView->document()->highlightingMode();

            QString activeCode;
            QString activeMeta;

            if (activeView->selection()) {
                activeCode = activeView->selectionText();
                activeMeta = i18n("Active File: %1 (%2), Selection: lines %3-%4",
                                   fileName, lang,
                                   activeView->selectionRange().start().line() + 1,
                                   activeView->selectionRange().end().line() + 1);
            } else {
                const int curLine = activeView->cursorPosition().line();
                const int startLine = qMax(0, curLine - 25);
                const int endLine = qMin(activeView->document()->lines() - 1, curLine + 25);

                QStringList snippetLines;
                for (int l = startLine; l <= endLine; ++l) {
                    snippetLines << activeView->document()->line(l);
                }
                activeCode = snippetLines.join(QLatin1Char('\n'));
                activeMeta = i18n("Active File: %1 (%2), Cursor at line %3", fileName, lang, curLine + 1);
            }

            if (!contextCode.isEmpty()) {
                contextCode += QStringLiteral("\n\n### Active Editor File\n") + activeCode;
                contextMeta += QStringLiteral(" | ") + activeMeta;
            } else {
                contextCode = activeCode;
                contextMeta = activeMeta;
            }
        }
    }

    m_inputEdit->clear();
    m_sendButton->setEnabled(false);
    m_stopButton->setVisible(true);

    m_session->sendMessage(userText, contextCode, contextMeta);
}

void ChatWidget::onStopClicked()
{
    m_session->cancelGeneration();
    m_stopButton->setVisible(false);
    m_sendButton->setEnabled(true);
}

int ChatWidget::visibleStart() const
{
    return m_session ? m_session->visibleStartIndex() : 0;
}

void ChatWidget::purgeHiddenMessagesConfirmed()
{
    // Permanently delete the hidden messages, keeping the agy conversation
    // context. Confirm first (destructive).
    if (!m_session || m_session->visibleStartIndex() <= 0) {
        return;
    }
    const int count = m_session->visibleStartIndex();
    const auto btn = QMessageBox::question(
        this, i18n("Delete hidden messages"),
        i18n("Permanently delete %1 hidden message(s)? "
             "The assistant keeps its own context; only the chat history is removed.",
             count),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn == QMessageBox::Yes) {
        m_session->purgeHiddenMessages();
        renderAllMessages();
        m_statusLabel->setText(i18n("Hidden messages deleted"));
    }
}

void ChatWidget::onClearClicked()
{
    if (m_session) {
        // Hide everything up to now; persisted per workspace in the session.
        m_session->setVisibleStartIndex(m_session->messages().size());
    }
    renderAllMessages();
    m_statusLabel->setText(i18n("Chat cleared (context preserved)"));
}

void ChatWidget::onResetContextClicked()
{
    if (m_session) {
        m_session->clearHistory();
    }
    m_browser->clear();
    m_stopButton->setVisible(false);
    m_sendButton->setEnabled(true);
    m_statusLabel->setText(i18n("Context reset (new conversation)"));
    renderAllMessages();
}

void ChatWidget::onMessageAdded()
{
    renderAllMessages();
}

void ChatWidget::onStreamingDelta()
{
    scheduleStreamRender();
}

void ChatWidget::scheduleStreamRender()
{
    // Batch rapid streaming deltas into a single deferred render.
    if (m_streamRenderTimer && !m_streamRenderTimer->isActive()) {
        m_streamRenderTimer->start();
    }
}

void ChatWidget::onGenerationFinished()
{
    if (m_streamRenderTimer) {
        m_streamRenderTimer->stop();
    }
    m_stopButton->setVisible(false);
    m_sendButton->setEnabled(true);
    renderAllMessages();

    // If this turn was the /commit message generation, hand off to the commit
    // confirmation dialog using the assistant's reply as the proposed message.
    if (m_awaitingCommitMessage) {
        m_awaitingCommitMessage = false;
        if (m_session) {
            const auto msgs = m_session->messages();
            if (!msgs.isEmpty() && msgs.last().role == ChatMessage::Role::Assistant) {
                confirmAndCreateCommit(msgs.last().text.trimmed());
            }
        }
    }
}

void ChatWidget::onGenerationError(const QString &errorMessage)
{
    if (m_streamRenderTimer) {
        m_streamRenderTimer->stop();
    }
    m_awaitingCommitMessage = false; // abort any pending /commit handoff
    m_stopButton->setVisible(false);
    m_sendButton->setEnabled(true);
    m_statusLabel->setText(i18n("Error: %1", errorMessage));
    renderAllMessages();
}

void ChatWidget::onStatusChanged(const QString &statusText)
{
    m_statusLabel->setText(statusText);
}

void ChatWidget::keepScrollAtBottomAfterSetHtml(bool wasAtBottom)
{
    if (wasAtBottom || (m_session && m_session->isGenerating())) {
        m_browser->verticalScrollBar()->setValue(m_browser->verticalScrollBar()->maximum());
    }
}

void ChatWidget::renderAllMessages()
{
    // A full render invalidates any cached streaming prefix.
    m_streaming = false;
    m_stableHtmlPrefix.clear();

    const auto messages = m_session ? m_session->messages() : QList<ChatMessage>();
    const int start = visibleStart();

    // "Cleared" state: messages exist, all of them are hidden, and no search is
    // filtering. Show the native panel (Restore / Delete) instead of the HTML.
    const bool clearedState = !messages.isEmpty()
        && start >= messages.size()
        && m_searchQuery.trimmed().isEmpty();

    if (m_historyStack) {
        m_historyStack->setCurrentIndex(clearedState ? 1 : 0);
    }
    if (clearedState) {
        return; // native panel handles the UI; no HTML render needed
    }

    ChatHtmlRenderer::Options opts;
    opts.visibleStartIndex = start;
    opts.searchQuery = m_searchQuery;
    const QString html = m_renderer.renderConversation(messages, opts);

    const bool atBottom = (m_browser->verticalScrollBar()->value() == m_browser->verticalScrollBar()->maximum());
    m_browser->setHtml(html);
    keepScrollAtBottomAfterSetHtml(atBottom);
}

void ChatWidget::renderStreamingUpdate()
{
    // While a search filter is active, fall back to the filtered full render.
    if (!m_searchQuery.trimmed().isEmpty()) {
        renderAllMessages();
        return;
    }

    const auto messages = m_session ? m_session->messages() : QList<ChatMessage>();
    if (messages.isEmpty()) {
        renderAllMessages();
        return;
    }

    const int start = visibleStart();
    const int lastIndex = messages.size() - 1;

    // On the first streaming tick, render everything except the final message
    // once and cache it (body only, no <html> wrapper). Its snippet ids are
    // assigned here and preserved for subsequent ticks so links stay stable.
    if (!m_streaming) {
        m_renderer.resetSnippets();
        QString prefixBody;
        if (start > 0 && lastIndex > start) {
            // Reuse the notice + prefix produced by renderConversation on the
            // slice that excludes the streaming message, stripping the wrapper.
            const QString wrapped = m_renderer.renderConversation(
                messages.mid(0, lastIndex),
                ChatHtmlRenderer::Options{ start });
            // Strip the <html><body ...> ... </body></html> shell.
            const int bodyOpen = wrapped.indexOf(QLatin1Char('>'), wrapped.indexOf(QLatin1String("<body")));
            const int bodyClose = wrapped.lastIndexOf(QLatin1String("</body>"));
            if (bodyOpen >= 0 && bodyClose > bodyOpen) {
                prefixBody = wrapped.mid(bodyOpen + 1, bodyClose - bodyOpen - 1);
            } else {
                prefixBody = m_renderer.renderMessageRange(messages.mid(0, lastIndex), start);
            }
        } else {
            prefixBody = m_renderer.renderMessageRange(messages.mid(0, lastIndex), start);
        }
        m_stableHtmlPrefix = prefixBody;
        m_streaming = true;
    }

    // Render only the final (streaming) message; its snippet ids continue after
    // the stable ones.
    const QString lastHtml = m_renderer.renderMessageRange(messages, lastIndex);
    const QString html = ChatHtmlRenderer::wrapDocument(m_stableHtmlPrefix + lastHtml);

    const bool atBottom = (m_browser->verticalScrollBar()->value() == m_browser->verticalScrollBar()->maximum());
    m_browser->setHtml(html);
    keepScrollAtBottomAfterSetHtml(atBottom);
}

void ChatWidget::onAnchorClicked(const QUrl &url)
{
    if (url.scheme() == QLatin1String("kateagy") || url.scheme() == QLatin1String("kate-agy")) {
        const QString action = url.host();
        if (action == QLatin1String("show-all-history")) {
            if (m_session) {
                m_session->setVisibleStartIndex(0); // un-hide, persisted
            }
            renderAllMessages();
            m_statusLabel->setText(i18n("Chat history restored"));
            return;
        }

        if (action == QLatin1String("purge-hidden")) {
            purgeHiddenMessagesConfirmed();
            return;
        }

        if (action == QLatin1String("openfile")) {
            QString relPath = url.path();
            if (relPath.startsWith(QLatin1Char('/'))) {
                relPath.remove(0, 1);
            }
            QString fullPath;
            if (QFileInfo(relPath).isAbsolute()) {
                fullPath = relPath;
            } else if (m_session && !m_session->projectPath().isEmpty()) {
                fullPath = QDir(m_session->projectPath()).filePath(relPath);
            } else {
                fullPath = relPath;
            }

            if (m_mainWindow) {
                m_mainWindow->openUrl(QUrl::fromLocalFile(fullPath));
                m_statusLabel->setText(i18n("Opened: %1", QFileInfo(fullPath).fileName()));
            }
            return;
        }

        if (action == QLatin1String("apply-edits")) {
            const int messageIndex = url.path().mid(1).toInt();
            reviewAndApplyEdits(messageIndex);
            return;
        }

        const int snippetId = url.path().mid(1).toInt();
        const QString code = m_renderer.snippets().value(snippetId);

        if (code.isEmpty()) {
            return;
        }

        if (action == QLatin1String("copy")) {
            QGuiApplication::clipboard()->setText(code);
            m_statusLabel->setText(i18n("Copied!"));
        } else if (action == QLatin1String("insert")) {
            if (m_mainWindow && m_mainWindow->activeView() && m_mainWindow->activeView()->document()) {
                auto *view = m_mainWindow->activeView();
                const QString fileName = view->document()->url().fileName();
                if (confirmEditorWrite(
                        i18n("Insert code"),
                        i18n("Insert this snippet at the cursor in %1?",
                             fileName.isEmpty() ? i18n("the current document") : fileName),
                        code)) {
                    view->document()->insertText(view->cursorPosition(), code);
                    view->setFocus();
                    m_statusLabel->setText(i18n("Inserted"));
                }
            }
        } else if (action == QLatin1String("replace")) {
            if (m_mainWindow && m_mainWindow->activeView() && m_mainWindow->activeView()->document()) {
                auto *view = m_mainWindow->activeView();
                const bool hasSelection = view->selection();
                const QString prompt = hasSelection
                    ? i18n("Replace the current selection with this snippet? This overwrites the selected text.")
                    : i18n("No selection: insert this snippet at the cursor?");
                if (confirmEditorWrite(hasSelection ? i18n("Replace selection") : i18n("Insert code"),
                                       prompt, code)) {
                    if (hasSelection) {
                        view->document()->replaceText(view->selectionRange(), code);
                    } else {
                        view->document()->insertText(view->cursorPosition(), code);
                    }
                    view->setFocus();
                    m_statusLabel->setText(hasSelection ? i18n("Replaced") : i18n("Inserted"));
                }
            }
        }
    } else if (url.scheme() == QLatin1String("file")) {
        if (m_mainWindow) {
            m_mainWindow->openUrl(url);
        }
    } else if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")) {
        QDesktopServices::openUrl(url);
    }
}

void ChatWidget::reviewAndApplyEdits(int messageIndex)
{
    if (!m_session) {
        return;
    }
    const auto messages = m_session->messages();
    if (messageIndex < 0 || messageIndex >= messages.size()) {
        return;
    }

    const QList<EditBlock> blocks = EditBlockParser::parse(messages.at(messageIndex).text);
    if (blocks.isEmpty()) {
        m_statusLabel->setText(i18n("No applicable edit blocks found."));
        return;
    }

    const QString projectRoot = m_session->projectPath();

    // Build a preview dialog: one selectable card per edit block, showing the
    // target file, whether it could be located, and the before/after text.
    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Review & Apply Changes"));
    dialog.resize(640, 560);
    auto *layout = new QVBoxLayout(&dialog);

    auto *intro = new QLabel(i18n("%1 change(s) proposed. Review and choose which to apply to your open documents.",
                                  QString::number(blocks.size())), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    auto *container = new QWidget(scroll);
    auto *containerLayout = new QVBoxLayout(container);

    struct BlockRow {
        QCheckBox *check = nullptr;
        EditBlock block;
        KTextEditor::Document *doc = nullptr;
        bool applicable = false;
    };
    QList<BlockRow> rows;

    for (const EditBlock &block : blocks) {
        BlockRow row;
        row.block = block;
        row.doc = EditBlockApplier::findOpenDocument(block.filePath, projectRoot);

        QString statusText;
        if (!row.doc) {
            statusText = i18n("⚠️ File not open in the editor — open it to apply this change.");
            row.applicable = false;
        } else {
            const auto loc = EditBlockApplier::locate(row.doc->text(), block);
            switch (loc.status) {
            case EditBlockApplier::MatchStatus::Exact:
                statusText = i18n("✅ Exact match at line %1.", QString::number(loc.startLine + 1));
                row.applicable = true;
                break;
            case EditBlockApplier::MatchStatus::Whitespace:
                statusText = i18n("✅ Match (whitespace-insensitive) at line %1.", QString::number(loc.startLine + 1));
                row.applicable = true;
                break;
            case EditBlockApplier::MatchStatus::Insertion:
                statusText = i18n("➕ Will insert new content.");
                row.applicable = true;
                break;
            case EditBlockApplier::MatchStatus::NotFound:
                statusText = i18n("⚠️ Original text not found — cannot apply automatically.");
                row.applicable = false;
                break;
            }
        }

        auto *group = new QGroupBox(block.filePath.isEmpty() ? i18n("(unspecified file)") : block.filePath, container);
        auto *groupLayout = new QVBoxLayout(group);

        row.check = new QCheckBox(statusText, group);
        row.check->setChecked(row.applicable);
        row.check->setEnabled(row.applicable);
        groupLayout->addWidget(row.check);

        auto *preview = new QTextBrowser(group);
        preview->setMaximumHeight(180);
        QString diffHtml = QStringLiteral("<pre style=\"font-family: monospace; font-size: 11px; margin:0;\">");
        for (const QString &l : block.searchText.split(QLatin1Char('\n'))) {
            diffHtml += QStringLiteral("<span style=\"color:#c0392b;\">- %1</span>\n").arg(l.toHtmlEscaped());
        }
        for (const QString &l : block.replaceText.split(QLatin1Char('\n'))) {
            diffHtml += QStringLiteral("<span style=\"color:#27ae60;\">+ %1</span>\n").arg(l.toHtmlEscaped());
        }
        diffHtml += QStringLiteral("</pre>");
        preview->setHtml(diffHtml);
        groupLayout->addWidget(preview);

        containerLayout->addWidget(group);
        rows.append(row);
    }

    containerLayout->addStretch();
    scroll->setWidget(container);
    layout->addWidget(scroll, 1);

    auto *buttonBox = new QDialogButtonBox(&dialog);
    auto *applyBtn = buttonBox->addButton(i18n("Apply Selected"), QDialogButtonBox::AcceptRole);
    buttonBox->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    Q_UNUSED(applyBtn);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    int applied = 0;
    int failed = 0;
    for (const BlockRow &row : rows) {
        if (!row.check->isChecked() || !row.doc) {
            continue;
        }
        QString error;
        if (EditBlockApplier::applyToDocument(row.doc, row.block, &error)) {
            ++applied;
        } else {
            ++failed;
        }
    }

    if (failed == 0) {
        m_statusLabel->setText(i18n("Applied %1 change(s).", QString::number(applied)));
    } else {
        m_statusLabel->setText(i18n("Applied %1 change(s), %2 failed.",
                                    QString::number(applied), QString::number(failed)));
    }
}

void ChatWidget::startCommitFlow()
{
    if (!m_session) {
        return;
    }
    const QString root = m_session->projectPath();
    if (root.isEmpty() || !GitCommitHelper::isGitRepository(root)) {
        m_session->addSystemMessage(i18n("⚠️ /commit requires an active project that is a git repository."));
        return;
    }

    const QString diff = GitCommitHelper::stagedDiff(root).trimmed();
    if (diff.isEmpty()) {
        m_session->addSystemMessage(i18n(
            "⚠️ Nothing staged to commit. Stage your changes first (e.g. `git add -p`), then run /commit."));
        return;
    }

    if (m_session->isGenerating()) {
        m_session->cancelGeneration();
    }

    // Ask the model for a commit message based on the staged diff. The reply is
    // intercepted in onGenerationFinished() to open the confirmation dialog.
    m_awaitingCommitMessage = true;

    const QString instruction = i18n(
        "Write a git commit message for the following staged diff. "
        "Use the Conventional Commits style: a concise subject line (max ~70 chars), "
        "then a blank line, then a short body explaining what changed and why. "
        "Output ONLY the commit message text, with no code fences or extra commentary.");

    // Bound the diff we send so a huge staged change doesn't blow up the prompt.
    QString diffForPrompt = diff;
    const int maxDiffChars = 12000;
    if (diffForPrompt.size() > maxDiffChars) {
        diffForPrompt = diffForPrompt.left(maxDiffChars)
            + QStringLiteral("\n... [diff truncated for prompt]");
    }

    m_includeContextCheck->setChecked(false);
    m_sendButton->setEnabled(false);
    m_stopButton->setVisible(true);
    m_statusLabel->setText(i18n("Generating commit message..."));

    m_session->sendMessage(instruction, diffForPrompt, i18n("Staged diff (git)"));
}

void ChatWidget::confirmAndCreateCommit(const QString &proposedMessage)
{
    if (!m_session) {
        return;
    }
    const QString root = m_session->projectPath();

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Create Git Commit"));
    dialog.resize(560, 420);
    auto *layout = new QVBoxLayout(&dialog);

    auto *intro = new QLabel(i18n(
        "Review and edit the commit message. Only already-staged changes will be committed. "
        "Hooks are respected; nothing is pushed."), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    const QString summary = GitCommitHelper::stagedSummary(root);
    if (!summary.isEmpty()) {
        auto *summaryLabel = new QLabel(i18n("Staged:\n%1", summary), &dialog);
        summaryLabel->setStyleSheet(QStringLiteral("font-family: monospace; font-size: 11px; color: palette(placeholder-text);"));
        summaryLabel->setWordWrap(true);
        layout->addWidget(summaryLabel);
    }

    auto *editor = new QTextEdit(&dialog);
    editor->setAcceptRichText(false);
    editor->setPlainText(proposedMessage);
    layout->addWidget(editor, 1);

    auto *buttonBox = new QDialogButtonBox(&dialog);
    buttonBox->addButton(i18n("Create Commit"), QDialogButtonBox::AcceptRole);
    buttonBox->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        m_session->addSystemMessage(i18n("Commit cancelled."));
        return;
    }

    const QString finalMessage = editor->toPlainText().trimmed();
    const GitCommitHelper::CommitResult res = GitCommitHelper::commit(root, finalMessage);
    if (res.success) {
        m_session->addSystemMessage(i18n("✅ Commit created:\n\n```\n%1\n```", res.message));
        m_statusLabel->setText(i18n("Commit created"));
    } else {
        m_session->addSystemMessage(i18n("⚠️ Commit failed:\n\n```\n%1\n```", res.message));
        m_statusLabel->setText(i18n("Commit failed"));
    }
}

bool ChatWidget::confirmEditorWrite(const QString &title, const QString &prompt, const QString &preview)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(title);
    box.setText(prompt);

    // Show a bounded preview of the code that would be written.
    QString shown = preview;
    const int maxChars = 1200;
    if (shown.size() > maxChars) {
        shown = shown.left(maxChars) + QStringLiteral("\n…");
    }
    box.setDetailedText(shown);

    box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Ok);
    box.button(QMessageBox::Ok)->setText(i18n("Apply"));
    return box.exec() == QMessageBox::Ok;
}

void ChatWidget::onAttachImageClicked()
{
    if (!m_session) {
        return;
    }

    // Multimodal input is only supported by the Direct Gemini API backend.
    if (m_session->backendMode() != AgyClient::BackendMode::DirectApi) {
        QMessageBox::information(this, i18n("Attach image"),
            i18n("Image attachments require the Direct Gemini API backend. "
                 "Switch the connection mode in Settings to attach images."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, i18n("Attach image"), QString(),
        i18n("Images (*.png *.jpg *.jpeg *.webp *.gif *.heic *.heif)"));
    if (path.isEmpty()) {
        return;
    }

    m_session->setPendingImage(path);
    m_pendingImageName = QFileInfo(path).fileName();
    m_statusLabel->setText(i18n("Image attached: %1 (sent with your next message)", m_pendingImageName));
    if (m_attachImageButton) {
        m_attachImageButton->setToolTip(i18n("Attached: %1 (click to change)", m_pendingImageName));
    }
    focusInput();
}
