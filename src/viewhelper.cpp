#include "viewhelper.h"
#include "inlinenoteprovider.h"
#include "eventfilter.h"
#include "agyclient.h"
#include "contextbuilder.h"
#include "settings.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

AgyViewHelper::AgyViewHelper(KTextEditor::View *view, AgyClient *client, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_client(client)
    , m_provider(new AgyInlineNoteProvider(this))
    , m_eventFilter(new AgyEventFilter(view, m_provider, this))
    , m_debounceTimer(new QTimer(this))
{
    applySettings();
    connect(AgySettings::instance(), &AgySettings::settingsChanged,
            this, &AgyViewHelper::applySettings);

    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(m_debounceMs);
    connect(m_debounceTimer, &QTimer::timeout, this, &AgyViewHelper::onDebounceTimeout);

    if (m_view) {
        m_view->registerInlineNoteProvider(m_provider);

        connect(m_view, &KTextEditor::View::cursorPositionChanged,
                this, &AgyViewHelper::onCursorPositionChanged);

        if (KTextEditor::Document *doc = m_view->document()) {
            connect(doc, &KTextEditor::Document::textChanged,
                    this, &AgyViewHelper::onDocumentTextChanged);
        }

        connect(m_eventFilter, &AgyEventFilter::triggerRequested, this, [this]() {
            triggerCompletionNow();
        });

        connect(m_eventFilter, &AgyEventFilter::suggestionAccepted, this, [this](const QString &text) {
            m_insertingSuggestion = true;
            m_currentRequestId = 0;
            m_debounceTimer->stop();
            m_insertingSuggestion = false;
            resetCandidates();
            // Next auto-trigger should fire quickly to chain follow-on
            // suggestions right after an accept.
            m_justAcceptedSuggestion = true;
            Q_EMIT suggestionAccepted(m_view, text);
        });

        connect(m_eventFilter, &AgyEventFilter::cycleNextRequested,
                this, &AgyViewHelper::onCycleNext);
        connect(m_eventFilter, &AgyEventFilter::cyclePrevRequested,
                this, &AgyViewHelper::onCyclePrev);
    }

    if (m_client) {
        connect(m_client, &AgyClient::completionReady,
                this, &AgyViewHelper::onCompletionReady);
        connect(m_client, &AgyClient::completionFailed,
                this, &AgyViewHelper::onCompletionFailed);
    }
}

void AgyViewHelper::detachView()
{
    m_view = nullptr;
    if (m_eventFilter) {
        m_eventFilter->detachView();
    }
}

AgyViewHelper::~AgyViewHelper()
{
    if (m_view && m_provider) {
        m_view->unregisterInlineNoteProvider(m_provider);
    }
}

KTextEditor::View *AgyViewHelper::view() const
{
    return m_view.data();
}

AgyInlineNoteProvider *AgyViewHelper::provider() const
{
    return m_provider;
}

void AgyViewHelper::showSuggestion(const QString &text)
{
    if (!m_view || !m_provider) {
        return;
    }

    const KTextEditor::Cursor cur = m_view->cursorPosition();
    m_provider->setSuggestion(cur, text);
}

void AgyViewHelper::clearSuggestion()
{
    if (m_provider) {
        m_provider->clearSuggestion();
    }
    m_currentRequestId = 0;
    resetCandidates();
}

void AgyViewHelper::applySettings()
{
    AgySettings *s = AgySettings::instance();
    setAutoTriggerEnabled(s->autoTrigger);
    setDebounceMs(s->debounceMs);
}

void AgyViewHelper::triggerCompletionNow()
{
    if (!m_view || !m_client) {
        return;
    }

    m_debounceTimer->stop();
    AgySettings *s = AgySettings::instance();
    const CompletionContext ctx = ContextBuilder::extractContext(m_view, s->maxPrefixLines, s->maxSuffixLines);
    if (ctx.cursor.isValid()) {
        m_requestCursor = ctx.cursor;
        m_currentRequestId = m_client->requestCompletion(ctx);
    }
}

void AgyViewHelper::setAutoTriggerEnabled(bool enabled)
{
    m_autoTriggerEnabled = enabled;
    if (!m_autoTriggerEnabled) {
        m_debounceTimer->stop();
    }
}

bool AgyViewHelper::autoTriggerEnabled() const
{
    return m_autoTriggerEnabled;
}

void AgyViewHelper::setDebounceMs(int ms)
{
    m_debounceMs = qBound(50, ms, 2000);
    m_debounceTimer->setInterval(m_debounceMs);
}

int AgyViewHelper::debounceMs() const
{
    return m_debounceMs;
}

void AgyViewHelper::onCursorPositionChanged(KTextEditor::View *view, const KTextEditor::Cursor &newPosition)
{
    Q_UNUSED(view);
    if (m_provider && m_provider->hasSuggestion()) {
        if (newPosition != m_provider->cursor()) {
            m_provider->clearSuggestion();
            m_currentRequestId = 0;
            resetCandidates();
        }
    }
}

int AgyViewHelper::adaptiveDebounceMs()
{
    // Base: the user-configured debounce. Then adapt:
    //  - Right after accepting a suggestion, fire fast to chain follow-ons.
    //  - While the user is typing in a fast burst (short gaps between edits),
    //    lengthen the delay so we don't fire mid-word; when typing slows or
    //    pauses, shorten toward the base.
    int ms = m_debounceMs;

    if (m_justAcceptedSuggestion) {
        ms = qMin(ms, 90);
    } else if (m_sinceLastEdit.isValid()) {
        const qint64 gap = m_sinceLastEdit.elapsed();
        if (gap < 120) {
            // Fast burst typing: wait longer (up to ~1.6x base) for a pause.
            ms = qMin(2000, static_cast<int>(m_debounceMs * 1.6));
        } else if (gap > 600) {
            // Deliberate/slow typing: react a bit quicker.
            ms = qMax(50, static_cast<int>(m_debounceMs * 0.7));
        }
    }

    return qBound(50, ms, 2000);
}

void AgyViewHelper::onDocumentTextChanged(KTextEditor::Document *document)
{
    Q_UNUSED(document);
    if (m_insertingSuggestion) {
        return;
    }

    Q_EMIT textEdited(m_view);

    if (m_autoTriggerEnabled) {
        // Choose an adaptive interval, then (re)start the debounce timer.
        m_debounceTimer->setInterval(adaptiveDebounceMs());
        m_debounceTimer->start();
        m_justAcceptedSuggestion = false;
        m_sinceLastEdit.restart();
    }
}

void AgyViewHelper::onDebounceTimeout()
{
    if (!m_view || !m_client) {
        return;
    }

    // Do not request if view is no longer valid
    AgySettings *s = AgySettings::instance();
    const CompletionContext ctx = ContextBuilder::extractContext(m_view, s->maxPrefixLines, s->maxSuffixLines);
    if (!ctx.cursor.isValid()) {
        return;
    }

    m_requestCursor = ctx.cursor;
    m_currentRequestId = m_client->requestCompletion(ctx);
}

void AgyViewHelper::onCompletionReady(uint64_t requestId, const QString &completion)
{
    if (!m_view || !m_provider) {
        return;
    }

    // Ensure the response matches our active request and cursor hasn't moved
    if (requestId == m_currentRequestId && m_view->cursorPosition() == m_requestCursor) {
        if (completion.isEmpty()) {
            m_cyclingRequest = false;
            return;
        }

        if (m_cyclingRequest) {
            // A "give me another" result: append as a new candidate (dedup) and
            // show it.
            m_cyclingRequest = false;
            if (!m_candidates.contains(completion)) {
                m_candidates.append(completion);
            }
            m_candidateIndex = m_candidates.indexOf(completion);
            showCandidate(m_candidateIndex);
        } else {
            // Fresh suggestion for this position: seed the candidate list.
            m_candidates = { completion };
            m_candidateIndex = 0;
            showSuggestion(completion);
        }
    }
}

void AgyViewHelper::resetCandidates()
{
    m_candidates.clear();
    m_candidateIndex = -1;
    m_cyclingRequest = false;
}

void AgyViewHelper::showCandidate(int index)
{
    if (index < 0 || index >= m_candidates.size()) {
        return;
    }
    m_candidateIndex = index;
    showSuggestion(m_candidates.at(index));
}

void AgyViewHelper::onCycleNext()
{
    if (!m_view || !m_client) {
        return;
    }
    // If there is a next cached candidate, show it; otherwise request a new one.
    if (m_candidateIndex >= 0 && m_candidateIndex + 1 < m_candidates.size()) {
        showCandidate(m_candidateIndex + 1);
        return;
    }

    // Request an additional alternative for the current position.
    AgySettings *s = AgySettings::instance();
    const CompletionContext ctx = ContextBuilder::extractContext(m_view, s->maxPrefixLines, s->maxSuffixLines);
    if (!ctx.cursor.isValid()) {
        return;
    }
    m_requestCursor = ctx.cursor;
    m_cyclingRequest = true;
    m_currentRequestId = m_client->requestCompletion(ctx);
}

void AgyViewHelper::onCyclePrev()
{
    if (m_candidateIndex > 0) {
        showCandidate(m_candidateIndex - 1);
    }
}

void AgyViewHelper::onCompletionFailed(uint64_t requestId, const QString &errorMessage)
{
    Q_UNUSED(errorMessage);
    if (requestId == m_currentRequestId) {
        m_currentRequestId = 0;
    }
}
