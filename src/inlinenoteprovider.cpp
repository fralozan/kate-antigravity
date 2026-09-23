#include "inlinenoteprovider.h"

#include <KTextEditor/InlineNote>
#include <KTextEditor/View>

#include <QFontMetrics>
#include <QPainter>
#include <QPalette>

AgyInlineNoteProvider::AgyInlineNoteProvider(QObject *parent)
    : KTextEditor::InlineNoteProvider()
{
    setParent(parent);
}

void AgyInlineNoteProvider::setSuggestion(const KTextEditor::Cursor &cursor, const QString &text)
{
    if (text.isEmpty() || !cursor.isValid()) {
        clearSuggestion();
        return;
    }

    int oldLine = m_cursor.isValid() ? m_cursor.line() : -1;
    m_cursor = cursor;
    m_suggestionText = text;
    m_active = true;

    if (oldLine >= 0 && oldLine != cursor.line()) {
        Q_EMIT inlineNotesChanged(oldLine);
    }
    Q_EMIT inlineNotesChanged(cursor.line());
}

void AgyInlineNoteProvider::clearSuggestion()
{
    if (!m_active) {
        return;
    }

    int oldLine = m_cursor.isValid() ? m_cursor.line() : -1;
    m_active = false;
    m_suggestionText.clear();
    m_cursor = KTextEditor::Cursor::invalid();

    if (oldLine >= 0) {
        Q_EMIT inlineNotesChanged(oldLine);
    }
}

void AgyInlineNoteProvider::advance(int chars)
{
    if (!m_active || chars <= 0 || chars > m_suggestionText.length()) {
        clearSuggestion();
        return;
    }

    m_suggestionText.remove(0, chars);
    if (m_suggestionText.isEmpty()) {
        clearSuggestion();
        return;
    }

    m_cursor.setColumn(m_cursor.column() + chars);
    Q_EMIT inlineNotesChanged(m_cursor.line());
}

bool AgyInlineNoteProvider::hasSuggestion() const
{
    return m_active && !m_suggestionText.isEmpty() && m_cursor.isValid();
}

QString AgyInlineNoteProvider::suggestionText() const
{
    return m_suggestionText;
}

KTextEditor::Cursor AgyInlineNoteProvider::cursor() const
{
    return m_cursor;
}

QList<int> AgyInlineNoteProvider::inlineNotes(int line) const
{
    if (m_active && m_cursor.isValid() && line == m_cursor.line() && !m_suggestionText.isEmpty()) {
        return { m_cursor.column() };
    }
    return {};
}

QSize AgyInlineNoteProvider::inlineNoteSize(const KTextEditor::InlineNote &note) const
{
    if (!m_active || m_suggestionText.isEmpty()) {
        return QSize(0, note.lineHeight());
    }

    QFontMetrics fm(note.font());
    QString firstLine = m_suggestionText.section(QLatin1Char('\n'), 0, 0);
    int width = fm.horizontalAdvance(firstLine);

    int lineCount = m_suggestionText.count(QLatin1Char('\n'));
    if (lineCount > 0) {
        QFont badgeFont = note.font();
        badgeFont.setPointSize(qMax(6, badgeFont.pointSize() - 2));
        QFontMetrics badgeFm(badgeFont);
        width += badgeFm.horizontalAdvance(QStringLiteral(" ⏎+%1").arg(lineCount)) + 6;
    }

    return QSize(width, note.lineHeight());
}

void AgyInlineNoteProvider::paintInlineNote(const KTextEditor::InlineNote &note, QPainter &painter, Qt::LayoutDirection direction) const
{
    Q_UNUSED(direction);

    if (!m_active || m_suggestionText.isEmpty()) {
        return;
    }

    painter.save();
    painter.setFont(note.font());

    // Compute dimmed ghost color based on active palette
    QColor ghostColor(128, 128, 128, 160);
    if (note.view()) {
        const QPalette &palette = note.view()->palette();
        QColor textColor = palette.color(QPalette::Disabled, QPalette::Text);
        if (textColor.alpha() == 255) {
            textColor.setAlpha(150);
        }
        ghostColor = textColor;
    }

    painter.setPen(ghostColor);

    QString firstLine = m_suggestionText.section(QLatin1Char('\n'), 0, 0);
    QFontMetrics fm(note.font());
    painter.drawText(0, fm.ascent(), firstLine);

    // Multiline hint badge
    int lineCount = m_suggestionText.count(QLatin1Char('\n'));
    if (lineCount > 0) {
        int firstLineWidth = fm.horizontalAdvance(firstLine);
        QFont badgeFont = note.font();
        badgeFont.setPointSize(qMax(6, badgeFont.pointSize() - 2));
        painter.setFont(badgeFont);

        QColor badgeColor = ghostColor;
        badgeColor.setAlpha(120);
        painter.setPen(badgeColor);

        QString badge = QStringLiteral(" ⏎+%1").arg(lineCount);
        painter.drawText(firstLineWidth + 2, fm.ascent(), badge);
    }

    painter.restore();
}
