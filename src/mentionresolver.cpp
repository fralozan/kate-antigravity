#include "mentionresolver.h"
#include "symbolindex.h"

#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QQueue>
#include <QPair>
#include <QFile>
#include <QProcess>
#include <QTextStream>
#include <QUrl>
#include <QPlainTextEdit>
#include <KTextEditor/Editor>
#include <KTextEditor/Document>
#include <KTextEditor/MainWindow>
#include <KTextEditor/View>
#include <KLocalizedString>

namespace {

// A mention captured from the prompt: the raw path/token plus an optional line
// range (@file:20-45 -> startLine=20, endLine=45; @file:30 -> startLine=endLine=30).
struct ParsedMention {
    QString token;      // e.g. "src/main.cpp" or "diagnostics"
    int startLine = -1; // 1-based, -1 if no range
    int endLine = -1;   // 1-based, -1 if open/omitted
};

// Returns true if `child` is the same as, or nested under, `root`.
// Both are treated as filesystem paths; comparison uses cleaned absolute paths.
bool isContainedIn(const QString &child, const QString &root)
{
    if (root.isEmpty()) {
        return true; // No project root to constrain against.
    }
    const QString cleanRoot = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString cleanChild = QDir::cleanPath(QFileInfo(child).absoluteFilePath());
    if (cleanChild == cleanRoot) {
        return true;
    }
    return cleanChild.startsWith(cleanRoot + QLatin1Char('/'));
}

// Extract the [startLine, endLine] slice (1-based, inclusive) from full text.
QString sliceLines(const QString &text, int startLine, int endLine)
{
    if (startLine < 0) {
        return text;
    }
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int from = qMax(1, startLine);
    const int to = (endLine < 0) ? from : qMax(from, endLine);
    QStringList out;
    for (int i = from; i <= to && i <= lines.size(); ++i) {
        out << lines.at(i - 1);
    }
    return out.join(QLatin1Char('\n'));
}

// Run `git` read-only in `workingDir` and return stdout (empty on failure).
QString runGit(const QString &workingDir, const QStringList &args)
{
    if (workingDir.isEmpty()) {
        return QString();
    }
    QProcess proc;
    proc.setWorkingDirectory(workingDir);
    proc.start(QStringLiteral("git"), args);
    if (!proc.waitForFinished(4000)) {
        proc.kill();
        return QString();
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        return QString();
    }
    return QString::fromUtf8(proc.readAllStandardOutput());
}

} // namespace

MentionResolution MentionResolver::resolveMentions(const QString &prompt,
                                                   const QString &projectPath,
                                                   KTextEditor::MainWindow *mainWindow)
{
    MentionResolution result;
    if (prompt.isEmpty()) {
        return result;
    }

    // @token with an optional :start[-end] line range.
    static const QRegularExpression mentionRegex(
        QStringLiteral("(?:^|\\s)@([a-zA-Z0-9_\\-\\./\\\\]+)(?::(\\d+)(?:-(\\d+))?)?"));
    auto it = mentionRegex.globalMatch(prompt);

    QSet<QString> processedPaths;
    QStringList contextBlocks;

    // Cache open documents in Kate
    QMap<QString, KTextEditor::Document *> openDocsByPath;
    QMap<QString, KTextEditor::Document *> openDocsByName;
    if (KTextEditor::Editor::instance()) {
        for (auto *doc : KTextEditor::Editor::instance()->documents()) {
            if (doc && doc->url().isLocalFile()) {
                const QString local = doc->url().toLocalFile();
                openDocsByPath.insert(local, doc);
                openDocsByName.insert(doc->url().fileName(), doc);
            }
        }
    }

    const int maxLinesPerFile = 500;
    const qint64 maxBytesPerFile = 20 * 1024; // 20 KB

    // Symbol mentions: @symbol:Name / @sym:Name / @def:Name. Handled up front
    // with a dedicated regex because the name part isn't a numeric line range.
    {
        static const QRegularExpression symbolRegex(
            QStringLiteral("(?:^|\\s)@(?:symbol|sym|def):([A-Za-z_][A-Za-z0-9_]*)"));
        auto symIt = symbolRegex.globalMatch(prompt);
        QSet<QString> processedSymbols;
        while (symIt.hasNext()) {
            const auto m = symIt.next();
            const QString symName = m.captured(1);
            if (symName.isEmpty() || processedSymbols.contains(symName)) {
                continue;
            }
            processedSymbols.insert(symName);
            // Prevent the generic pass from treating the "symbol" keyword as a file.
            processedPaths.insert(QStringLiteral("symbol"));
            processedPaths.insert(QStringLiteral("sym"));
            processedPaths.insert(QStringLiteral("def"));

            if (projectPath.isEmpty()) {
                result.warnings.append(i18n("⚠️ @symbol:%1 needs a project root to search in.").arg(symName));
                continue;
            }

            const auto matches = SymbolIndex::findDefinitions(projectPath, symName, 5);
            if (matches.isEmpty()) {
                result.warnings.append(i18n("⚠️ No definition found for symbol @symbol:%1.").arg(symName));
                continue;
            }

            result.resolvedFiles.append(QStringLiteral("@symbol:%1").arg(symName));
            QString block = QStringLiteral("#### Symbol Definitions: %1\n").arg(symName);
            for (const auto &sm : matches) {
                const QString ext = QFileInfo(sm.fullPath).suffix().toLower();
                block += QStringLiteral("**%1:%2** (%3)\n```%4\n%5\n```\n")
                    .arg(sm.relativePath, QString::number(sm.line), sm.kind, ext, sm.snippet);
            }
            contextBlocks.append(block);
        }
    }

    while (it.hasNext()) {
        auto match = it.next();
        ParsedMention mention;
        mention.token = match.captured(1).trimmed();
        if (!match.captured(2).isEmpty()) {
            mention.startLine = match.captured(2).toInt();
            mention.endLine = match.captured(3).isEmpty() ? mention.startLine : match.captured(3).toInt();
        }
        const QString rawPath = mention.token;
        if (rawPath.isEmpty() || processedPaths.contains(rawPath)) {
            continue;
        }

        const QString lower = rawPath.toLower();

        // Special mention: @diagnostics, @errors, etc.
        if (lower == QLatin1String("diagnostics") || lower == QLatin1String("errors") ||
            lower == QLatin1String("errores") || lower == QLatin1String("warnings") ||
            lower == QLatin1String("problemas")) {
            processedPaths.insert(rawPath);
            result.resolvedFiles.append(QStringLiteral("@%1").arg(rawPath));

            QString diagContent;
            int problemCount = 0;

            QList<KTextEditor::Document *> docsToCheck;
            if (mainWindow && mainWindow->activeView() && mainWindow->activeView()->document()) {
                docsToCheck.append(mainWindow->activeView()->document());
            } else if (KTextEditor::Editor::instance()) {
                docsToCheck = KTextEditor::Editor::instance()->documents();
            }

            for (auto *doc : docsToCheck) {
                if (!doc) continue;
                const QString docName = doc->url().fileName().isEmpty() ? doc->documentName() : doc->url().fileName();
                const auto marks = doc->marks();
                for (auto itMark = marks.constBegin(); itMark != marks.constEnd(); ++itMark) {
                    const auto *mark = itMark.value();
                    if (!mark) continue;
                    if (mark->type & (KTextEditor::Document::markType06 | KTextEditor::Document::markType07)) {
                        problemCount++;
                        const QString typeStr = (mark->type & KTextEditor::Document::markType07)
                            ? QStringLiteral("ERROR")
                            : QStringLiteral("WARNING");
                        const int lineNum = mark->line + 1;
                        const QString lineCode = doc->line(mark->line).trimmed();
                        diagContent += QStringLiteral("- **%1:%2** [%3]:\n  `%4`\n")
                            .arg(docName, QString::number(lineNum), typeStr, lineCode);
                    }
                }
            }

            if (problemCount == 0) {
                diagContent = i18n("No compiler errors or warnings currently marked in open documents.");
            }

            contextBlocks.append(QStringLiteral("### 🩺 Editor Diagnostics & Compiler Marks\n%1").arg(diagContent));
            continue;
        }

        // Special mention: @selection -> the active editor selection.
        if (lower == QLatin1String("selection") || lower == QLatin1String("seleccion")) {
            processedPaths.insert(rawPath);
            KTextEditor::View *view = mainWindow ? mainWindow->activeView() : nullptr;
            if (view && view->selection() && !view->selectionText().isEmpty()) {
                result.resolvedFiles.append(QStringLiteral("@%1").arg(rawPath));
                const QString fileName = view->document() ? view->document()->url().fileName() : QString();
                const QString lang = view->document() ? view->document()->highlightingMode() : QString();
                const int startLine = view->selectionRange().start().line() + 1;
                const int endLine = view->selectionRange().end().line() + 1;
                contextBlocks.append(
                    QStringLiteral("#### Editor Selection: %1 (lines %2-%3)\n```%4\n%5\n```")
                        .arg(fileName.isEmpty() ? i18n("Untitled") : fileName,
                             QString::number(startLine), QString::number(endLine),
                             lang, view->selectionText()));
            } else {
                result.warnings.append(i18n("⚠️ @selection: no active text selection in the editor."));
            }
            continue;
        }

        // Special mention: @terminal -> best-effort capture of the embedded
        // terminal's visible text. Kate's KonsolePart does not expose a public
        // API to read its scrollback, so this only works if a plain-text view
        // is reachable; otherwise it degrades with a clear explanation.
        if (lower == QLatin1String("terminal") || lower == QLatin1String("term")) {
            processedPaths.insert(rawPath);
            QString captured;
            if (mainWindow && mainWindow->window()) {
                const auto textEdits = mainWindow->window()->findChildren<QPlainTextEdit *>();
                for (auto *te : textEdits) {
                    // Heuristic: the terminal view is typically a monospace,
                    // read-only plain text area with non-trivial content.
                    if (te && te->isReadOnly() && !te->toPlainText().trimmed().isEmpty()) {
                        captured = te->toPlainText();
                        break;
                    }
                }
            }
            if (captured.isEmpty()) {
                result.warnings.append(i18n(
                    "⚠️ @terminal: could not read the embedded terminal output. "
                    "Kate's terminal does not expose its buffer; paste the output manually, "
                    "or use @git-diff for repository changes."));
            } else {
                // Keep only the tail; terminals accumulate a lot of scrollback.
                const QStringList allLines = captured.split(QLatin1Char('\n'));
                const int keep = 120;
                const QStringList tail = allLines.mid(qMax(0, allLines.size() - keep));
                result.resolvedFiles.append(QStringLiteral("@%1").arg(rawPath));
                contextBlocks.append(QStringLiteral("### 🖥️ Terminal Output (last %1 lines)\n```\n%2\n```")
                                         .arg(QString::number(tail.size()), tail.join(QLatin1Char('\n')).trimmed()));
            }
            continue;
        }

        // Special mention: @git-diff / @diff / @git -> uncommitted changes.
        if (lower == QLatin1String("git-diff") || lower == QLatin1String("gitdiff") ||
            lower == QLatin1String("diff") || lower == QLatin1String("git")) {
            processedPaths.insert(rawPath);
            if (projectPath.isEmpty()) {
                result.warnings.append(i18n("⚠️ @%1: no project root to run git in.").arg(rawPath));
                continue;
            }
            const QString unstaged = runGit(projectPath, {QStringLiteral("diff")}).trimmed();
            const QString staged = runGit(projectPath, {QStringLiteral("diff"), QStringLiteral("--staged")}).trimmed();
            if (unstaged.isEmpty() && staged.isEmpty()) {
                contextBlocks.append(i18n("### 🔀 Git Diff\nNo uncommitted changes (working tree clean)."));
                result.resolvedFiles.append(QStringLiteral("@%1").arg(rawPath));
                continue;
            }
            QString block = QStringLiteral("### 🔀 Git Diff\n");
            if (!staged.isEmpty()) {
                block += QStringLiteral("#### Staged changes\n```diff\n%1\n```\n").arg(staged);
            }
            if (!unstaged.isEmpty()) {
                block += QStringLiteral("#### Unstaged changes\n```diff\n%1\n```\n").arg(unstaged);
            }
            contextBlocks.append(block);
            result.resolvedFiles.append(QStringLiteral("@%1").arg(rawPath));
            continue;
        }

        QString resolvedFullPath;
        QString fileContent;
        int lineCount = 0;
        bool isTruncated = false;

        // Check if open in Kate
        KTextEditor::Document *targetDoc = nullptr;
        if (openDocsByName.contains(rawPath)) {
            targetDoc = openDocsByName.value(rawPath);
        } else {
            QString candPath = rawPath;
            if (!QFileInfo(candPath).isAbsolute() && !projectPath.isEmpty()) {
                candPath = QDir(projectPath).filePath(rawPath);
            }
            if (openDocsByPath.contains(candPath)) {
                targetDoc = openDocsByPath.value(candPath);
            }
        }

        if (targetDoc) {
            resolvedFullPath = targetDoc->url().toLocalFile();
            // Constrain to the project tree (open buffers may live elsewhere;
            // only enforce when a project root is set).
            if (!isContainedIn(resolvedFullPath, projectPath)) {
                result.warnings.append(i18n("⚠️ @%1 is outside the project directory and was skipped.").arg(rawPath));
                continue;
            }
            const int totalLines = targetDoc->lines();
            lineCount = qMin(totalLines, maxLinesPerFile);

            QStringList lines;
            qint64 currentBytes = 0;
            for (int l = 0; l < lineCount; ++l) {
                const QString lineStr = targetDoc->line(l);
                currentBytes += lineStr.toUtf8().size() + 1;
                lines << lineStr;
                if (currentBytes >= maxBytesPerFile) {
                    isTruncated = true;
                    break;
                }
            }
            if (totalLines > maxLinesPerFile) {
                isTruncated = true;
            }
            fileContent = lines.join(QLatin1Char('\n'));
        } else {
            // Read from disk
            QString diskPath = rawPath;
            if (!QFileInfo(diskPath).isAbsolute() && !projectPath.isEmpty()) {
                diskPath = QDir(projectPath).filePath(rawPath);
            }

            QFileInfo fi(diskPath);
            if (!fi.exists()) {
                result.warnings.append(i18n("⚠️ File or folder not found: @%1").arg(rawPath));
                continue;
            }

            // Path containment check: never read outside the project tree.
            if (!isContainedIn(fi.absoluteFilePath(), projectPath)) {
                result.warnings.append(i18n("⚠️ @%1 is outside the project directory and was skipped.").arg(rawPath));
                continue;
            }

            if (fi.isDir()) {
                resolvedFullPath = fi.canonicalFilePath();
                const QString relPath = (!projectPath.isEmpty() && resolvedFullPath.startsWith(projectPath))
                    ? QDir(projectPath).relativeFilePath(resolvedFullPath)
                    : fi.fileName();

                QStringList treeLines;
                const int maxTreeEntries = 80;
                const int maxFolderDepth = 3;
                bool dirTruncated = false;

                static const QSet<QString> s_prunedDirs = {
                    QStringLiteral(".git"), QStringLiteral("node_modules"),
                    QStringLiteral("vendor"), QStringLiteral("build"),
                    QStringLiteral("dist"), QStringLiteral(".cache"),
                    QStringLiteral("__pycache__"), QStringLiteral(".idea"),
                    QStringLiteral(".vscode"), QStringLiteral("target"),
                    QStringLiteral("bin"), QStringLiteral("obj")
                };

                QQueue<QPair<QString, int>> dirQueue;
                dirQueue.enqueue({resolvedFullPath, 0});

                while (!dirQueue.isEmpty() && treeLines.size() < maxTreeEntries) {
                    const auto current = dirQueue.dequeue();
                    const QString currentPath = current.first;
                    const int depth = current.second;

                    QDir cDir(currentPath);
                    const auto list = cDir.entryInfoList(
                        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                        QDir::DirsFirst | QDir::Name
                    );

                    for (const QFileInfo &childFi : list) {
                        const QString itemRel = QDir(resolvedFullPath).relativeFilePath(childFi.filePath());
                        if (childFi.isDir()) {
                            const QString dirName = childFi.fileName().toLower();
                            if (s_prunedDirs.contains(dirName) || dirName.startsWith(QLatin1Char('.'))) {
                                continue;
                            }
                            treeLines.append(itemRel + QStringLiteral("/"));
                            if (depth < maxFolderDepth && treeLines.size() < maxTreeEntries) {
                                dirQueue.enqueue({childFi.filePath(), depth + 1});
                            }
                        } else {
                            treeLines.append(itemRel);
                            if (treeLines.size() >= maxTreeEntries) {
                                break;
                            }
                        }
                    }
                }

                if (!dirQueue.isEmpty()) {
                    dirTruncated = true;
                }

                processedPaths.insert(rawPath);
                result.resolvedFiles.append(rawPath);

                QString block = QStringLiteral("#### Mentioned Folder: %1/ (Directory Tree)\n```\n%2/\n%3\n```")
                    .arg(relPath, relPath, treeLines.join(QLatin1Char('\n')));

                if (dirTruncated) {
                    block += QStringLiteral("\n// ... [Truncated to %1 directory entries]\n").arg(maxTreeEntries);
                    result.warnings.append(i18n("⚠️ Folder @%1 has too many items and was truncated to the first %2 items.").arg(rawPath, QString::number(maxTreeEntries)));
                }

                contextBlocks.append(block);
                continue;
            }

            resolvedFullPath = fi.canonicalFilePath();
            QFile file(resolvedFullPath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                QStringList lines;
                qint64 currentBytes = 0;
                while (!in.atEnd() && lines.size() < maxLinesPerFile) {
                    const QString lineStr = in.readLine();
                    currentBytes += lineStr.toUtf8().size() + 1;
                    lines << lineStr;
                    if (currentBytes >= maxBytesPerFile) {
                        isTruncated = true;
                        break;
                    }
                }
                if (!in.atEnd()) {
                    isTruncated = true;
                }
                fileContent = lines.join(QLatin1Char('\n'));
                lineCount = lines.size();
                file.close();
            } else {
                result.warnings.append(i18n("⚠️ Could not open file for reading: @%1").arg(rawPath));
                continue;
            }
        }

        if (!resolvedFullPath.isEmpty() && !fileContent.isEmpty()) {
            processedPaths.insert(rawPath);
            result.resolvedFiles.append(rawPath);

            const QString relPath = (!projectPath.isEmpty() && resolvedFullPath.startsWith(projectPath))
                ? QDir(projectPath).relativeFilePath(resolvedFullPath)
                : QFileInfo(resolvedFullPath).fileName();

            const QString ext = QFileInfo(resolvedFullPath).suffix().toLower();

            // Apply an explicit @file:start-end line range if requested.
            QString shownContent = fileContent;
            QString rangeSuffix;
            if (mention.startLine > 0) {
                shownContent = sliceLines(fileContent, mention.startLine, mention.endLine);
                rangeSuffix = (mention.endLine > mention.startLine)
                    ? QStringLiteral(" (lines %1-%2)").arg(mention.startLine).arg(mention.endLine)
                    : QStringLiteral(" (line %1)").arg(mention.startLine);
            }

            QString block = QStringLiteral("#### Mentioned File: %1%2 (%3 lines)\n```%4\n%5\n```")
                .arg(relPath, rangeSuffix, QString::number(lineCount), ext, shownContent);

            if (isTruncated && mention.startLine <= 0) {
                block += QStringLiteral("\n// ... [Truncated to %1 lines / 20 KB]\n").arg(maxLinesPerFile);
                result.warnings.append(i18n("⚠️ File @%1 exceeds the limit (%2 lines / 20 KB) and was truncated.").arg(rawPath, QString::number(maxLinesPerFile)));
            }

            contextBlocks.append(block);
        }
    }

    if (!contextBlocks.isEmpty()) {
        result.assembledContext = QStringLiteral("### Context: Mentioned Files\n\n")
            + contextBlocks.join(QStringLiteral("\n\n"));
    }

    return result;
}

QString MentionResolver::formatMentionsInHtml(const QString &escapedText)
{
    static const QRegularExpression mentionRegex(QStringLiteral("(?:^|\\s)@([a-zA-Z0-9_\\-\\./\\\\]+)"));
    QString result = escapedText;

    // Replace @path with clickable badge
    result.replace(mentionRegex, QStringLiteral(" <a href=\"kateagy://openfile/\\1\" style=\"background-color: palette(alternate-base); color: palette(highlight); border: 1px solid palette(midlight); border-radius: 3px; padding: 1px 5px; font-weight: bold; text-decoration: none;\">@\\1</a>"));

    return result;
}
