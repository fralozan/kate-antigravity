#ifndef AGY_PLUGINVIEW_H
#define AGY_PLUGINVIEW_H

#include <QObject>
#include <QMap>
#include <KXMLGUIClient>

namespace KTextEditor {
class MainWindow;
class View;
}

class QWidget;
class QMenu;
class KateAntigravityPlugin;
class AgyViewHelper;
class AgyClient;
class ChatSession;
class ChatWidget;

class KateAntigravityPluginView : public QObject, public KXMLGUIClient
{
    Q_OBJECT

public:
    explicit KateAntigravityPluginView(KateAntigravityPlugin *plugin, KTextEditor::MainWindow *mainWindow);
    ~KateAntigravityPluginView() override;

    KTextEditor::MainWindow *mainWindow() const;
    AgyClient *client() const;
    ChatSession *chatSession() const;
    ChatWidget *chatWidget() const;

private Q_SLOTS:
    void onViewCreated(KTextEditor::View *view);
    void onViewChanged(KTextEditor::View *view);
    void onViewDestroyed(QObject *obj);

    void triggerSuggestion();
    void clearSuggestion();
    void toggleChat();
    void attachTabBarButtons();
    void onContextMenuAboutToShow(KTextEditor::View *view, QMenu *menu);

private:
    void setupActions();
    void registerView(KTextEditor::View *view);
    void attachTabBarButton(KTextEditor::View *view);
    void sendSelectionToChat(KTextEditor::View *view, const QString &prompt);
    AgyViewHelper *helperForView(KTextEditor::View *view) const;

    KateAntigravityPlugin *m_plugin = nullptr;
    KTextEditor::MainWindow *m_mainWindow = nullptr;
    AgyClient *m_client = nullptr;
    QMap<KTextEditor::View *, AgyViewHelper *> m_helpers;

    QWidget *m_chatToolView = nullptr;
    ChatWidget *m_chatWidget = nullptr;
    ChatSession *m_chatSession = nullptr;
};

#endif // AGY_PLUGINVIEW_H
