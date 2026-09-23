#include "editblockapplier.h"

#include <QStringList>
#include <QDir>
#include <QFileInfo>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Cursor>
#include <KTextEditor/Range>
#include <KLocalizedString>

namespace EditBlockApplier {

namespace {

// Compare two line ranges for exact equality.
bool exactMatchAt(const QStringList &docLines, const QStringList &searchLines, int start)
{
    if (start + searchLines.size() > docLines.size()) {
        return false;
    }
    for (int i = 0; i < searchLines.size(); ++i) {
        if (docLines.at(start + i) != searchLines.at(i)) {
            return false;
        }
    }
    return true;
}

// Compare ignoring each line's leading/trailing whitespace (tolerates the model
// reflowing indentation slightly).
bool whitespaceMatchAt(const QStringList &docLines, const QStringList &searchLines, int start)
{
    if (start + searchLines.size() > docLines.size()) {
        return false;
    }
    for (int i = 0; i < searchLines.size(); ++i) {
        if (docLines.at(start + i).trimmed() != searchLines.at(i).trimmed()) {
            return false;
        }
    }
    return true;
}

} // namespace

LocateResult locate(const QString &documentText, const EditBlock &block)
{
    LocateResult res;

    if (block.searchText.isEmpty()) {
        res.status = MatchStatus::Insertion;
        res.startLine = 0;
        res.endLine = -1; // nothing to replace; caller inserts
        return res;
    }

    const QStringList docLines = documentText.split(QLatin1Char('\n'));
    const QStringList searchLines = block.searchText.split(QLatin1Char('\n'));
    if (searchLines.isEmpty()) {
        res.status = MatchStatus::NotFound;
        return res;
    }

    // Pass 1: exact match.
    for (int start = 0; start + searchLines.size() <= docLines.size(); ++start) {
        if (exactMatchAt(docLines, searchLines, start)) {
            res.status = MatchStatus::Exact;
            res.startLine = start;
            res.endLine = start + searchLines.size() - 1;
            return res;
        }
    }

    // Pass 2: whitespace-insensitive fallback.
    for (int start = 0; start + searchLines.size() <= docLines.size(); ++start) {
        if (whitespaceMatchAt(docLines, searchLines, start)) {
            res.status = MatchStatus::Whitespace;
            res.startLine = start;
            res.endLine = start + searchLines.size() - 1;
            return res;
        }
    }

    res.status = MatchStatus::NotFound;
    return res;
}

bool applyToDocument(KTextEditor::Document *doc, const EditBlock &block, QString *error)
{
    if (!doc) {
        if (error) *error = i18n("No document to apply changes to.");
        return false;
    }

    const QString text = doc->text();
    const LocateResult loc = locate(text, block);

    if (loc.status == MatchStatus::NotFound) {
        if (error) {
            *error = i18n("Could not locate the original text to replace in %1.",
                          doc->url().fileName().isEmpty() ? i18n("the document") : doc->url().fileName());
        }
        return false;
    }

    if (loc.status == MatchStatus::Insertion) {
        // Insert at the top of the document.
        doc->insertText(KTextEditor::Cursor(0, 0),
                        block.replaceText + (block.replaceText.endsWith(QLatin1Char('\n')) ? QString() : QStringLiteral("\n")));
        return true;
    }

    // Replace the located line range [startLine, endLine] with replaceText.
    const int lastCol = doc->lineLength(loc.endLine);
    KTextEditor::Range range(loc.startLine, 0, loc.endLine, lastCol);
    doc->replaceText(range, block.replaceText);
    return true;
}

KTextEditor::Document *findOpenDocument(const QString &filePath, const QString &projectRoot)
{
    if (!KTextEditor::Editor::instance()) {
        return nullptr;
    }

    // Candidate absolute path (resolve relative against project root).
    QString absolute = filePath;
    if (!QFileInfo(filePath).isAbsolute() && !projectRoot.isEmpty()) {
        absolute = QDir(projectRoot).filePath(filePath);
    }
    absolute = QDir::cleanPath(absolute);
    const QString baseName = QFileInfo(filePath).fileName();

    KTextEditor::Document *byName = nullptr;
    const auto docs = KTextEditor::Editor::instance()->documents();
    for (auto *doc : docs) {
        if (!doc || !doc->url().isLocalFile()) {
            continue;
        }
        const QString local = QDir::cleanPath(doc->url().toLocalFile());
        if (local == absolute) {
            return doc; // exact path wins
        }
        if (doc->url().fileName() == baseName) {
            byName = doc; // remember a filename match as fallback
        }
    }
    return byName;
}

} // namespace EditBlockApplier
