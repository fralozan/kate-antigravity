#ifndef AGY_EVENTFILTER_H
#define AGY_EVENTFILTER_H

#include <QObject>
#include <QPointer>

namespace KTextEditor {
class View;
}

class AgyInlineNoteProvider;

class AgyEventFilter : public QObject
{
    Q_OBJECT

public:
    explicit AgyEventFilter(KTextEditor::View *view, AgyInlineNoteProvider *provider, QObject *parent = nullptr);
    ~AgyEventFilter() override = default;

    void detachView();

Q_SIGNALS:
    void triggerRequested();
    void suggestionAccepted(const QString &text);
    void suggestionDismissed();
    void cycleNextRequested(); // Alt+]
    void cyclePrevRequested(); // Alt+[

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool handleKeyPress(class QKeyEvent *keyEvent);

    QPointer<KTextEditor::View> m_view;
    AgyInlineNoteProvider *m_provider = nullptr;
};

#endif // AGY_EVENTFILTER_H
