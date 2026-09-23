#ifndef AGYINFODIALOG_H
#define AGYINFODIALOG_H

#include <QDialog>
#include <QString>

class QTextBrowser;
class QDialogButtonBox;
class QPushButton;
class ChatSession;

class AgyInfoDialog : public QDialog
{
    Q_OBJECT

public:
    enum class DialogType {
        Help,
        Usage,
        Project
    };

    explicit AgyInfoDialog(DialogType type, ChatSession *session = nullptr, QWidget *parent = nullptr);
    ~AgyInfoDialog() override = default;

    static void showHelp(QWidget *parent = nullptr);
    static void showUsage(ChatSession *session, QWidget *parent = nullptr);
    static void showProject(ChatSession *session, QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private Q_SLOTS:
    void refreshContent();

private:
    void setupUi();
    QString buildHtmlContent() const;

    DialogType m_type;
    ChatSession *m_session = nullptr;
    QTextBrowser *m_browser = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
    QPushButton *m_refreshButton = nullptr;
};

#endif // AGYINFODIALOG_H
