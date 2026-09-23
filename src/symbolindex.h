#ifndef AGY_SYMBOLINDEX_H
#define AGY_SYMBOLINDEX_H

#include <QString>
#include <QList>

// Lightweight, dependency-free symbol lookup.
//
// Rather than relying on an LSP server (which may not be configured), this
// scans project source files with per-language regular expressions to locate
// definitions of functions, classes, structs, etc. It is deliberately
// approximate: good enough to pull the relevant definition into chat context,
// not a full parser.
struct SymbolMatch {
    QString name;         // symbol name matched
    QString relativePath; // path relative to project root
    QString fullPath;
    int line = 0;         // 1-based line of the definition
    QString kind;         // e.g. "function", "class", "struct"
    QString snippet;      // a few lines around/within the definition
};

namespace SymbolIndex {

// Find definitions of `symbolName` across the project's source files.
// `projectRoot` bounds the search; `maxResults` caps the returned matches.
QList<SymbolMatch> findDefinitions(const QString &projectRoot,
                                   const QString &symbolName,
                                   int maxResults = 5);

// True if the file extension is one we know how to scan.
bool isSupportedSourceFile(const QString &fileName);

// Heuristically pull identifier-like tokens out of free text (chat prompt) for
// automatic symbol enrichment. Filters out short tokens and common English/
// Spanish words and language keywords. Returns de-duplicated candidates,
// preferring CamelCase / snake_case / plausibly-code identifiers.
QStringList extractCandidateIdentifiers(const QString &text, int maxCandidates = 5);

} // namespace SymbolIndex

#endif // AGY_SYMBOLINDEX_H
