#ifndef AGY_EDITBLOCKPARSER_H
#define AGY_EDITBLOCKPARSER_H

#include <QString>
#include <QList>

// Parses search/replace edit blocks emitted by the assistant into structured
// edits the UI can preview and apply. The format (documented to the model in
// the system prompt) is:
//
//   ```agy-edit
//   file: relative/or/absolute/path.ext
//   <<<<<<< SEARCH
//   exact text to find
//   =======
//   replacement text
//   >>>>>>> REPLACE
//   ```
//
// A single fenced block may contain multiple SEARCH/REPLACE pairs, and a
// message may contain multiple fenced blocks (for different files). An empty
// SEARCH section means "create the file / insert at top".
struct EditBlock {
    QString filePath;    // as written by the model (may be relative)
    QString searchText;  // text to locate (empty => insertion / new file)
    QString replaceText; // replacement
};

namespace EditBlockParser {

// Extract all edit blocks from an assistant message. Returns empty if none.
QList<EditBlock> parse(const QString &assistantText);

// True if the message appears to contain at least one edit block. Cheap check
// used to decide whether to show the "apply changes" affordance.
bool containsEditBlock(const QString &assistantText);

} // namespace EditBlockParser

#endif // AGY_EDITBLOCKPARSER_H
