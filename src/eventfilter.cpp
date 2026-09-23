#include "eventfilter.h"
#include "inlinenoteprovider.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QEvent>
#include <QKeyEvent>
#include <QWidget>

AgyEventFilter::AgyEventFilter(KTextEditor::View *view, AgyInlineNoteProvider *provider, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_provider(provider)
{
    if (m_view) {
        m_view->installEventFilter(this);
        if (QWidget *proxy = m_view->focusProxy()) {
            proxy->installEventFilter(this);
        }
    }
}

void AgyEventFilter::detachView()
{
    m_view = nullptr;
}

bool AgyEventFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (handleKeyPress(keyEvent)) {
            return true;
        }
    }

    return QObject::eventFilter(watched, event);
}

bool AgyEventFilter::handleKeyPress(QKeyEvent *keyEvent)
{
    if (!m_view || !m_provider) {
        return false;
    }

    // Manual trigger shortcut: Alt+\ (Alt + Backslash)
    if (keyEvent->key() == Qt::Key_Backslash && keyEvent->modifiers() == Qt::AltModifier) {
        Q_EMIT triggerRequested();
        return true;
    }

    // Cycle through alternative suggestions: Alt+] (next) / Alt+[ (previous).
    if (keyEvent->modifiers() == Qt::AltModifier) {
        if (keyEvent->key() == Qt::Key_BracketRight) {
            Q_EMIT cycleNextRequested();
            return true;
        }
        if (keyEvent->key() == Qt::Key_BracketLeft) {
            Q_EMIT cyclePrevRequested();
            return true;
        }
    }

    // Check if there is an active ghost text suggestion
    if (m_provider->hasSuggestion()) {
        const KTextEditor::Cursor cur = m_view->cursorPosition();
        const KTextEditor::Cursor notePos = m_provider->cursor();

        if (cur == notePos) {
            // 1. Tab key: Accept suggestion
            if (keyEvent->key() == Qt::Key_Tab && (keyEvent->modifiers() == Qt::NoModifier || keyEvent->modifiers() == Qt::KeypadModifier)) {
                const QString suggestion = m_provider->suggestionText();
                const KTextEditor::Cursor at = m_provider->cursor();

                m_provider->clearSuggestion();

                if (m_view->document()) {
                    m_view->document()->insertText(at, suggestion);

                    // Move cursor to end of inserted text
                    int lineCount = suggestion.count(QLatin1Char('\n'));
                    KTextEditor::Cursor targetCursor;
                    if (lineCount == 0) {
                        targetCursor = KTextEditor::Cursor(at.line(), at.column() + suggestion.length());
                    } else {
                        QString lastLine = suggestion.section(QLatin1Char('\n'), -1);
                        targetCursor = KTextEditor::Cursor(at.line() + lineCount, lastLine.length());
                    }
                    m_view->setCursorPosition(targetCursor);
                }

                Q_EMIT suggestionAccepted(suggestion);
                return true;
            }

            // 2. Escape key: Dismiss suggestion
            if (keyEvent->key() == Qt::Key_Escape) {
                m_provider->clearSuggestion();
                Q_EMIT suggestionDismissed();
                return true;
            }

            // 3. Typing-through: If user typed the exact next character
            const QString typedText = keyEvent->text();
            if (!typedText.isEmpty()) {
                if (m_provider->suggestionText().startsWith(typedText)) {
                    m_provider->advance(typedText.length());
                    return false; // Allow typed character into document
                } else {
                    m_provider->clearSuggestion();
                }
            }

            // 4. Cursor movement / deletion keys dismiss the ghost text
            switch (keyEvent->key()) {
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Up:
            case Qt::Key_Down:
            case Qt::Key_Home:
            case Qt::Key_End:
            case Qt::Key_PageUp:
            case Qt::Key_PageDown:
            case Qt::Key_Backspace:
            case Qt::Key_Delete:
                m_provider->clearSuggestion();
                break;
            default:
                break;
            }
        } else {
            // Cursor is not at suggestion position
            m_provider->clearSuggestion();
        }
    }

    return false;
}
