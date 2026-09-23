#ifndef AGY_EDITBLOCKAPPLIER_H
#define AGY_EDITBLOCKAPPLIER_H

#include <QString>
#include "editblockparser.h"

namespace KTextEditor {
class Document;
class MainWindow;
}

// Locates and applies EditBlocks (search/replace edits) against KTextEditor
// documents. Kept separate from the parser and UI so the matching logic is
// testable in isolation from widgets where practical.
namespace EditBlockApplier {

enum class MatchStatus {
    Exact,        // searchText found verbatim
    Whitespace,   // found ignoring leading/trailing whitespace per line
    Insertion,    // empty searchText -> insert at top / new content
    NotFound      // could not locate the search text
};

struct LocateResult {
    MatchStatus status = MatchStatus::NotFound;
    int startLine = -1;   // 0-based, inclusive
    int endLine = -1;     // 0-based, inclusive
};

// Find where `block.searchText` occurs in `documentText` (\n-joined lines).
// Uses exact match first, then a per-line whitespace-insensitive fallback.
LocateResult locate(const QString &documentText, const EditBlock &block);

// Apply an edit block to an open document. Returns true on success. On failure
// (search text not found) the document is left untouched and *error is set.
bool applyToDocument(KTextEditor::Document *doc, const EditBlock &block, QString *error = nullptr);

// Resolve a possibly-relative edit-block path to a document already open in the
// editor, matching by absolute path or file name. Returns nullptr if none.
KTextEditor::Document *findOpenDocument(const QString &filePath, const QString &projectRoot);

} // namespace EditBlockApplier

#endif // AGY_EDITBLOCKAPPLIER_H
