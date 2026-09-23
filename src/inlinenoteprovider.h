#ifndef AGY_INLINENOTEPROVIDER_H
#define AGY_INLINENOTEPROVIDER_H

#include <KTextEditor/InlineNoteProvider>
#include <KTextEditor/Cursor>
#include <QString>

class AgyInlineNoteProvider : public KTextEditor::InlineNoteProvider
{
    Q_OBJECT

public:
    explicit AgyInlineNoteProvider(QObject *parent = nullptr);
    ~AgyInlineNoteProvider() override = default;

    void setSuggestion(const KTextEditor::Cursor &cursor, const QString &text);
    void clearSuggestion();
    void advance(int chars);

    bool hasSuggestion() const;
    QString suggestionText() const;
    KTextEditor::Cursor cursor() const;

    // KTextEditor::InlineNoteProvider interface
    QList<int> inlineNotes(int line) const override;
    QSize inlineNoteSize(const KTextEditor::InlineNote &note) const override;
    void paintInlineNote(const KTextEditor::InlineNote &note, QPainter &painter, Qt::LayoutDirection direction) const override;

private:
    bool m_active = false;
    KTextEditor::Cursor m_cursor = KTextEditor::Cursor::invalid();
    QString m_suggestionText;
};

#endif // AGY_INLINENOTEPROVIDER_H
