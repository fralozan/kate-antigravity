#ifndef AGY_CHATWIDGET_H
#define AGY_CHATWIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QMap>
#include <QString>
#include <QUrl>
#include "projectdetector.h"
#include "chathtmlrenderer.h"

class QTimer;

class QTextBrowser;
class QPushButton;
class QToolButton;
class QLabel;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QStackedWidget;
class QWidget;
class ChatSession;
class ChatCompletionPopup;
struct CompletionItem;
class KateAntigravityPlugin;

namespace KTextEditor {
class MainWindow;
class View;
class Document;
}

class ChatInputEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit ChatInputEdit(QWidget *parent = nullptr);

    void setCompletionPopup(ChatCompletionPopup *popup);
    void setProjectPath(const QString &projectPath);
    void setMainWindow(KTextEditor::MainWindow *mainWindow);
    void insertCompletion(const QString &text);

Q_SIGNALS:
    void submitRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private Q_SLOTS:
    void onTextChanged();
    void onCompletionItemSelected(const CompletionItem &item);

private:
    void checkTriggerCompletion();

    ChatCompletionPopup *m_popup = nullptr;
    QString m_projectPath;
    KTextEditor::MainWindow *m_mainWindow = nullptr;
    int m_triggerPosition = -1;
};

class ChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWidget(KTextEditor::MainWindow *mainWindow, ChatSession *session, QWidget *parent = nullptr);
    ~ChatWidget() override;

    void setPlugin(KateAntigravityPlugin *plugin);
    KateAntigravityPlugin *plugin() const { return m_plugin; }

    void focusInput();
    void updateEditorContext();
    void sendDirectQuery(const QString &prompt, const QString &code, const QString &meta);
    void prepareContextQuery(const QString &initialPrompt = QString());

    void setSession(ChatSession *session);
    ChatSession *session() const { return m_session; }
    void switchToProject(const ProjectInfo &info);
    void refreshProjectCombo();

    // Auto-switch entry point called when the active editor document changes.
    // Applies precedence rules: does nothing if the workspace is pinned; keeps
    // the current workspace when the document has no real project (avoids
    // degrading to "General"); switches only to a different real project.
    void maybeAutoSwitchToDocument(KTextEditor::Document *doc);

    // Whether the active workspace is pinned (auto-switch suspended).
    bool isWorkspacePinned() const { return m_workspacePinned; }

    // Prompt for a folder and register it as a new workspace, then switch to it.
    void addWorkspaceInteractive();
    // Rename / remove the currently active workspace (shared by the combo's
    // right-click menu and the header's workspace-actions button).
    void renameCurrentWorkspace();
    void removeCurrentWorkspace();

    QSize sizeHint() const override { return QSize(360, 650); }
    QSize minimumSizeHint() const override { return QSize(280, 250); }

public Q_SLOTS:
    void onSendClicked();
    void onStopClicked();
    void onClearClicked();
    void onResetContextClicked();
    void onAnchorClicked(const QUrl &url);
    // Confirm + permanently delete the hidden messages (keeps agy context).
    void purgeHiddenMessagesConfirmed();

private Q_SLOTS:
    void onProjectComboChanged(int index);
    void onProjectComboContextMenu(const QPoint &pos);
    void onAttachImageClicked();
    void onMessageAdded();
    void onStreamingDelta();
    void onGenerationFinished();
    void onGenerationError(const QString &errorMessage);
    void onStatusChanged(const QString &statusText);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    // The hidden-messages boundary lives in the session (persisted per
    // workspace); the widget reads it through here.
    int visibleStart() const;

    void setupUi();
    void renderAllMessages();
    // Re-render only the last (streaming) message on top of a cached stable
    // prefix, avoiding a full-history rebuild on every token.
    void renderStreamingUpdate();
    void updateAccountAndModelDisplay();

    // Parse the assistant message at messageIndex for search/replace edit
    // blocks and show a preview dialog to apply them per-block to the editor.
    void reviewAndApplyEdits(int messageIndex);

    // Confirmation dialog shown before writing a snippet into the editor
    // (insert/replace). Returns true if the user confirms. `preview` is the
    // code that would be written.
    bool confirmEditorWrite(const QString &title, const QString &prompt, const QString &preview);

    // /commit workflow: generate a message from the staged diff, then (after an
    // editable confirmation dialog) create the commit. Public so the slash
    // command router can trigger it.
public:
    void startCommitFlow();
private:
    // Show the editable confirmation dialog for `proposedMessage` and, if the
    // user confirms, run the commit against the current project root.
    void confirmAndCreateCommit(const QString &proposedMessage);
    bool m_awaitingCommitMessage = false;

    KTextEditor::MainWindow *m_mainWindow = nullptr;
    KateAntigravityPlugin *m_plugin = nullptr;
    ChatSession *m_session = nullptr;
    ProjectInfo m_currentProject;

    ChatCompletionPopup *m_popup = nullptr;
    QComboBox *m_projectCombo = nullptr;
    QToolButton *m_accountButton = nullptr;
    QTextBrowser *m_browser = nullptr;
    // Lower area stack: page 0 = browser, page 1 = native "cleared" panel.
    QStackedWidget *m_historyStack = nullptr;
    QWidget *m_clearedPanel = nullptr;
    QPushButton *m_restoreButton = nullptr;
    QPushButton *m_purgeButton = nullptr;
    ChatInputEdit *m_inputEdit = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QToolButton *m_clearButton = nullptr;
    QToolButton *m_resetButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QToolButton *m_modelBadge = nullptr;
    QCheckBox *m_includeContextCheck = nullptr;
    QLabel *m_contextDetailLabel = nullptr;
    QToolButton *m_attachImageButton = nullptr;
    QString m_pendingImageName; // display name of the currently attached image

    // History search.
    QToolButton *m_searchButton = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QString m_searchQuery;

    // Workspace pinning: when true, the active-document auto-switch is
    // suspended so the manually chosen workspace stays put.
    QToolButton *m_pinButton = nullptr;
    bool m_workspacePinned = false;
    void updatePinButton();

    // Explicit workspace-actions menu button (add/rename/remove).
    QToolButton *m_workspaceMenuButton = nullptr;

    // Renders messages to HTML and owns the snippet id -> code table used to
    // resolve copy/insert/replace links.
    ChatHtmlRenderer m_renderer;

    // Coalesce streaming deltas so the re-render runs at most once per interval
    // instead of once per token.
    QTimer *m_streamRenderTimer = nullptr;
    void scheduleStreamRender();

    // Incremental streaming state: HTML for all messages except the last one,
    // cached at the start of a generation so each delta only re-renders the
    // final streaming message.
    bool m_streaming = false;
    QString m_stableHtmlPrefix;
    void keepScrollAtBottomAfterSetHtml(bool wasAtBottom);
};

#endif // AGY_CHATWIDGET_H
