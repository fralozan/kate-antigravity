#ifndef CHATCOMPLETIONPOPUP_H
#define CHATCOMPLETIONPOPUP_H

#include <QFrame>
#include <QList>
#include <QIcon>

class QListWidget;
class QListWidgetItem;
class QKeyEvent;

struct CompletionItem {
    enum class Type {
        SlashCommand,
        FileMention
    };

    Type type = Type::SlashCommand;
    QString text;         // e.g. "/settings" or "@src/main.cpp"
    QString hint;         // e.g. "Open Antigravity settings" or "[Open] (120 lines)"
    QString insertText;   // Text to replace the trigger with
    QIcon icon;
};

class ChatCompletionPopup : public QFrame
{
    Q_OBJECT

public:
    explicit ChatCompletionPopup(QWidget *parent = nullptr);
    ~ChatCompletionPopup() override = default;

    void setCompletions(const QList<CompletionItem> &items);
    void showAt(const QPoint &globalPos);

    bool handleKeyPress(QKeyEvent *event);
    bool hasItems() const;

Q_SIGNALS:
    void itemSelected(const CompletionItem &item);
    void dismissed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void onItemDoubleClicked(QListWidgetItem *item);

private:
    void setupUi();
    void selectNext();
    void selectPrevious();
    void acceptCurrent();

    QListWidget *m_listWidget = nullptr;
    QList<CompletionItem> m_items;
};

#endif // CHATCOMPLETIONPOPUP_H
