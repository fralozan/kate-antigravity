#include <QTest>
#include <QSignalSpy>
#include "inlinenoteprovider.h"

class TestGhostText : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testInitialState()
    {
        AgyInlineNoteProvider provider;
        QCOMPARE(provider.hasSuggestion(), false);
        QCOMPARE(provider.suggestionText(), QString());
        QCOMPARE(provider.inlineNotes(0).isEmpty(), true);
    }

    void testSetAndClearSuggestion()
    {
        AgyInlineNoteProvider provider;
        QSignalSpy resetSpy(&provider, &AgyInlineNoteProvider::inlineNotesReset);
        QSignalSpy changedSpy(&provider, &AgyInlineNoteProvider::inlineNotesChanged);

        KTextEditor::Cursor cur(5, 12);
        provider.setSuggestion(cur, QStringLiteral("auto x = 42;"));

        QCOMPARE(provider.hasSuggestion(), true);
        QCOMPARE(provider.suggestionText(), QStringLiteral("auto x = 42;"));
        QCOMPARE(provider.cursor(), cur);
        QCOMPARE(provider.inlineNotes(5), QList<int>{12});
        QCOMPARE(provider.inlineNotes(4).isEmpty(), true);
        QCOMPARE(provider.inlineNotes(6).isEmpty(), true);
        QVERIFY(changedSpy.count() >= 1);

        provider.clearSuggestion();
        QCOMPARE(provider.hasSuggestion(), false);
        QCOMPARE(provider.inlineNotes(5).isEmpty(), true);
    }

    void testAdvanceTypingThrough()
    {
        AgyInlineNoteProvider provider;
        KTextEditor::Cursor cur(2, 4);
        provider.setSuggestion(cur, QStringLiteral("const int answer = 42;"));

        // Advance 5 chars ("const")
        provider.advance(5);
        QCOMPARE(provider.hasSuggestion(), true);
        QCOMPARE(provider.cursor(), KTextEditor::Cursor(2, 9));
        QCOMPARE(provider.suggestionText(), QStringLiteral(" int answer = 42;"));
        QCOMPARE(provider.inlineNotes(2), QList<int>{9});

        // Advance remaining
        provider.advance(provider.suggestionText().length());
        QCOMPARE(provider.hasSuggestion(), false);
    }
};

QTEST_MAIN(TestGhostText)
#include "test_ghosttext.moc"
