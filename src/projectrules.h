#ifndef AGY_PROJECTRULES_H
#define AGY_PROJECTRULES_H

#include <QString>

// Loads persistent, project-scoped instructions from a rules file at the
// project root (similar in spirit to .cursorrules / .kiro steering). The
// content is injected verbatim into the chat system prompt so a project can
// carry conventions, stack notes, preferred language, etc.
//
// Supported filenames (first match wins): ".antigravity", ".antigravityrules".
// Results are cached per (path, mtime, size) so repeated prompt builds don't
// re-read the disk unless the file changed.
namespace ProjectRules {

// Returns the trimmed rules text for the given project root, or an empty
// string if no rules file exists (or the root is empty). Reads are cached.
QString rulesForProject(const QString &projectRoot);

// The candidate filenames looked up at the project root, in priority order.
QStringList candidateFileNames();

// Clears the in-memory cache (used by tests).
void clearCache();

} // namespace ProjectRules

#endif // AGY_PROJECTRULES_H
