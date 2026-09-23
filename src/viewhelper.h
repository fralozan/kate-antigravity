#ifndef AGY_VIEWHELPER_H
#define AGY_VIEWHELPER_H

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QStringList>
#include <KTextEditor/Cursor>

namespace KTextEditor {
class View;
class Document;
}

class AgyInlineNoteProvider;
class AgyEventFilter;
class AgyClient;

class AgyViewHelper : public QObject
{
    Q_OBJECT

public:
    explicit AgyViewHelper(KTextEditor::View *view, AgyClient *client, QObject *parent = nullptr);
    ~AgyViewHelper() override;

    void detachView();

    KTextEditor::View *view() const;
    AgyInlineNoteProvider *provider() const;

    void showSuggestion(const QString &text);
    void clearSuggestion();
    void triggerCompletionNow();

    void setAutoTriggerEnabled(bool enabled);
    bool autoTriggerEnabled() const;

    void setDebounceMs(int ms);
    int debounceMs() const;

Q_SIGNALS:
    void triggerRequested(KTextEditor::View *view);
    void suggestionAccepted(KTextEditor::View *view, const QString &text);
    void textEdited(KTextEditor::View *view);

public Q_SLOTS:
    void applySettings();

private Q_SLOTS:
    void onCursorPositionChanged(KTextEditor::View *view, const KTextEditor::Cursor &newPosition);
    void onDocumentTextChanged(KTextEditor::Document *document);
    void onDebounceTimeout();
    void onCompletionReady(uint64_t requestId, const QString &completion);
    void onCompletionFailed(uint64_t requestId, const QString &errorMessage);

private:
    QPointer<KTextEditor::View> m_view;
    AgyClient *m_client = nullptr;
    AgyInlineNoteProvider *m_provider = nullptr;
    AgyEventFilter *m_eventFilter = nullptr;

    QTimer *m_debounceTimer = nullptr;
    int m_debounceMs = 250;             // configured base debounce
    bool m_autoTriggerEnabled = true;
    bool m_insertingSuggestion = false;

    // Adaptive debounce state.
    QElapsedTimer m_sinceLastEdit;      // time between consecutive edits
    bool m_justAcceptedSuggestion = false; // shorten debounce right after accept
    // Compute the debounce for the next request based on typing cadence and
    // whether a suggestion was just accepted, clamped to [50, 2000] and around
    // the configured base value.
    int adaptiveDebounceMs();

    uint64_t m_currentRequestId = 0;
    KTextEditor::Cursor m_requestCursor = KTextEditor::Cursor::invalid();

    // Suggestion cycling (Alt+] / Alt+[). Candidates collected for the current
    // cursor position; new requests append, cursor moves reset.
    QStringList m_candidates;
    int m_candidateIndex = -1;
    bool m_cyclingRequest = false; // the in-flight request is a "give me another"
    void resetCandidates();
    void showCandidate(int index);

private Q_SLOTS:
    void onCycleNext();
    void onCyclePrev();
};

#endif // AGY_VIEWHELPER_H
