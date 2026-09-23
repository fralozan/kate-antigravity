#ifndef MENTIONRESOLVER_H
#define MENTIONRESOLVER_H

#include <QString>
#include <QStringList>

namespace KTextEditor {
class MainWindow;
}

struct MentionResolution {
    QString assembledContext;
    QStringList resolvedFiles;
    QStringList warnings;
};

class MentionResolver
{
public:
    /**
     * Finds all @file mentions in prompt, reads the content from editor buffers or disk
     * (up to 500 lines or 20KB per file), and returns formatted context blocks.
     */
    static MentionResolution resolveMentions(const QString &prompt,
                                             const QString &projectPath,
                                             KTextEditor::MainWindow *mainWindow = nullptr);

    /**
     * Converts @path mentions in HTML-escaped text into clickable kateagy://openfile links.
     */
    static QString formatMentionsInHtml(const QString &escapedText);
};

#endif // MENTIONRESOLVER_H
