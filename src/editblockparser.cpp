#include "editblockparser.h"

#include <QStringList>

namespace EditBlockParser {

namespace {

const QString kSearchMarker = QStringLiteral("<<<<<<< SEARCH");
const QString kSeparator = QStringLiteral("=======");
const QString kReplaceMarker = QStringLiteral(">>>>>>> REPLACE");
const QString kFilePrefix = QStringLiteral("file:");

// Join lines [from, to) with '\n' (no trailing newline).
QString joinRange(const QStringList &lines, int from, int to)
{
    QStringList slice;
    for (int i = from; i < to && i < lines.size(); ++i) {
        slice << lines.at(i);
    }
    return slice.join(QLatin1Char('\n'));
}

} // namespace

QList<EditBlock> parse(const QString &assistantText)
{
    QList<EditBlock> blocks;
    if (assistantText.isEmpty()) {
        return blocks;
    }

    // Work line-by-line so the search/replace payloads keep exact whitespace.
    const QStringList lines = assistantText.split(QLatin1Char('\n'));

    QString currentFile;
    int i = 0;
    const int n = lines.size();

    while (i < n) {
        const QString trimmed = lines.at(i).trimmed();

        // Track the most recent "file:" directive; it applies to the blocks
        // that follow until another file: line appears.
        if (trimmed.startsWith(kFilePrefix, Qt::CaseInsensitive)) {
            currentFile = trimmed.mid(kFilePrefix.length()).trimmed();
            // Strip surrounding backticks if the model quoted the path.
            if (currentFile.startsWith(QLatin1Char('`')) && currentFile.endsWith(QLatin1Char('`')) && currentFile.length() >= 2) {
                currentFile = currentFile.mid(1, currentFile.length() - 2).trimmed();
            }
            ++i;
            continue;
        }

        if (trimmed == kSearchMarker) {
            // Collect SEARCH lines until the separator.
            const int searchStart = i + 1;
            int j = searchStart;
            while (j < n && lines.at(j).trimmed() != kSeparator) {
                ++j;
            }
            if (j >= n) {
                break; // Malformed: no separator; stop parsing.
            }
            const int searchEnd = j; // exclusive
            const int replaceStart = j + 1;
            int k = replaceStart;
            while (k < n && lines.at(k).trimmed() != kReplaceMarker) {
                ++k;
            }
            if (k >= n) {
                break; // Malformed: no REPLACE marker.
            }
            const int replaceEnd = k; // exclusive

            EditBlock block;
            block.filePath = currentFile;
            block.searchText = joinRange(lines, searchStart, searchEnd);
            block.replaceText = joinRange(lines, replaceStart, replaceEnd);
            blocks.append(block);

            i = k + 1; // continue after the REPLACE marker
            continue;
        }

        ++i;
    }

    return blocks;
}

bool containsEditBlock(const QString &assistantText)
{
    return assistantText.contains(kSearchMarker) && assistantText.contains(kReplaceMarker);
}

} // namespace EditBlockParser
