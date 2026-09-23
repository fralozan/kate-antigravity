#include "contextbuilder.h"

#include <KTextEditor/MainWindow>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <KTextEditor/Range>

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

CompletionContext ContextBuilder::extractContext(KTextEditor::View *view,
                                                int maxPrefixLines,
                                                int maxSuffixLines)
{
    CompletionContext ctx;
    if (!view || !view->document()) {
        return ctx;
    }

    KTextEditor::Document *doc = view->document();
    const KTextEditor::Cursor cur = view->cursorPosition();
    ctx.cursor = cur;

    // 1. Extract prefix
    int startLine = qMax(0, cur.line() - maxPrefixLines);
    KTextEditor::Range prefixRange(startLine, 0, cur.line(), cur.column());
    ctx.prefix = doc->text(prefixRange);

    // 2. Extract suffix
    int endLine = qMin(doc->lines() - 1, cur.line() + maxSuffixLines);
    int endCol = doc->lineLength(endLine);
    KTextEditor::Range suffixRange(cur.line(), cur.column(), endLine, endCol);
    ctx.suffix = doc->text(suffixRange);

    // 3. Top-of-file Pinning: If cursor is beyond line 35, capture the first 25 lines
    // containing imports, headers, and type definitions
    if (cur.line() > 35) {
        int headerEndLine = qMin(doc->lines() - 1, 25);
        KTextEditor::Range headerRange(0, 0, headerEndLine, doc->lineLength(headerEndLine));
        ctx.fileHeader = doc->text(headerRange).trimmed();
    }

    // 4. Analyze current line for indentation and mid-line status
    QString currentLine = doc->line(cur.line());
    int indentChars = 0;
    while (indentChars < currentLine.length() && currentLine[indentChars].isSpace()) {
        indentChars++;
    }
    ctx.indentation = currentLine.left(indentChars);

    QString lineAfterCursor = currentLine.mid(cur.column());
    ctx.isMidLine = !lineAfterCursor.trimmed().isEmpty();

    // 5. Extract metadata
    ctx.language = doc->highlightingMode();
    ctx.fileName = doc->url().fileName();
    if (ctx.fileName.isEmpty()) {
        ctx.fileName = QStringLiteral("buffer");
    }

    // 6. Neighboring Tabs: Extract relevant context from other open documents in Kate
    if (view->mainWindow()) {
        const auto otherViews = view->mainWindow()->views();
        int count = 0;
        QSet<QString> visitedUrls;

        QFileInfo curFi(doc->url().fileName());
        QString curBase = curFi.completeBaseName();

        for (auto *ov : otherViews) {
            if (!ov || !ov->document() || ov == view || ov->document() == doc) {
                continue;
            }

            QString otherUrl = ov->document()->url().toString();
            if (otherUrl.isEmpty() || visitedUrls.contains(otherUrl)) {
                continue;
            }
            visitedUrls.insert(otherUrl);

            QFileInfo otherFi(ov->document()->url().fileName());
            QString otherName = otherFi.fileName();

            // Check if it's a direct companion file (e.g. .h for a .cpp file)
            bool isCompanion = (otherFi.completeBaseName() == curBase);
            int maxLinesToExtract = isCompanion ? 50 : 25;

            int linesInDoc = ov->document()->lines();
            int extractLines = qMin(linesInDoc, maxLinesToExtract);
            if (extractLines > 0) {
                KTextEditor::Range r(0, 0, extractLines - 1, ov->document()->lineLength(extractLines - 1));
                QString snippet = ov->document()->text(r).trimmed();
                if (!snippet.isEmpty()) {
                    RelatedFileContext rfc;
                    rfc.fileName = otherName;
                    rfc.language = ov->document()->highlightingMode();
                    rfc.snippet = snippet;

                    if (isCompanion) {
                        ctx.relatedFiles.prepend(rfc); // Directly related header/file comes first
                    } else {
                        ctx.relatedFiles.append(rfc);
                    }
                    count++;
                }
            }

            if (count >= 2) {
                break;
            }
        }
    }

    return ctx;
}

QString ContextBuilder::buildPrompt(const CompletionContext &ctx)
{
    QString prompt;
    prompt += QStringLiteral("You are an expert, ultra-fast inline code completion engine for the Kate text editor.\n");
    prompt += QStringLiteral("Your task is to generate ONLY the exact code that belongs directly at the <|cursor|> marker.\n\n");

    prompt += QStringLiteral("CRITICAL INSTRUCTIONS:\n");
    prompt += QStringLiteral("1. Output ONLY the raw code to be inserted. Do not write markdown code fences, backticks (no ```), or explanatory comments.\n");
    prompt += QStringLiteral("2. Do NOT repeat any code from the prefix or suffix.\n");
    if (ctx.isMidLine) {
        prompt += QStringLiteral("3. The cursor is inside an existing line. Output ONLY the immediate expression or arguments. DO NOT output newlines.\n");
    } else if (!ctx.indentation.isEmpty()) {
        prompt += QStringLiteral("3. Match the indentation of the target line exactly (%1 spaces/tabs).\n").arg(ctx.indentation.length());
    }
    prompt += QStringLiteral("4. If no completion is logical or helpful, output nothing.\n\n");

    // Few-Shot examples
    prompt += QStringLiteral("=== FEW-SHOT EXAMPLES ===\n\n");
    prompt += QStringLiteral("Example 1 (Mid-line expression):\n");
    prompt += QStringLiteral("<code>\nif (user != nullptr && user-><|cursor|>) {\n</code>\n");
    prompt += QStringLiteral("Completion:\nisActive()\n\n");

    prompt += QStringLiteral("Example 2 (Function body completion):\n");
    prompt += QStringLiteral("<code>\nbool isEven(int n) {\n    <|cursor|>\n}\n</code>\n");
    prompt += QStringLiteral("Completion:\nreturn n % 2 == 0;\n\n");

    prompt += QStringLiteral("=== CURRENT TARGET FILE ===\n");
    prompt += QStringLiteral("File: %1\n").arg(ctx.fileName);
    if (!ctx.language.isEmpty()) {
        prompt += QStringLiteral("Language: %1\n").arg(ctx.language);
    }
    if (!ctx.indentation.isEmpty()) {
        prompt += QStringLiteral("Line Indentation: \"%1\"\n").arg(ctx.indentation);
    }

    // Top-of-file imports if available
    if (!ctx.fileHeader.isEmpty()) {
        prompt += QStringLiteral("\n<file_imports_and_declarations>\n");
        prompt += ctx.fileHeader;
        prompt += QStringLiteral("\n</file_imports_and_declarations>\n");
    }

    // Related open tabs in Kate
    for (const auto &rel : ctx.relatedFiles) {
        prompt += QStringLiteral("\n<related_open_file name=\"%1\">\n").arg(rel.fileName);
        prompt += rel.snippet;
        prompt += QStringLiteral("\n</related_open_file>\n");
    }

    // Target code snippet
    prompt += QStringLiteral("\n<code>\n");
    prompt += ctx.prefix;
    prompt += QStringLiteral("<|cursor|>");
    prompt += ctx.suffix;
    prompt += QStringLiteral("\n</code>\n\nCompletion:");

    return prompt;
}

QString ContextBuilder::sanitizeResponse(const QString &rawResponse,
                                        const CompletionContext &ctx)
{
    QString result = rawResponse;

    // 1. Unwrap a single leading/trailing markdown code fence (```cpp ... ```),
    //    but only when the response is actually wrapped in one. We avoid a global
    //    remove of "```" so that backticks inside the suggested code (e.g. inside
    //    string literals) are preserved.
    {
        const QString trimmed = result.trimmed();
        if (trimmed.startsWith(QStringLiteral("```"))) {
            // Drop the opening fence line (```lang) and the closing fence, if present.
            static const QRegularExpression openFence(QStringLiteral("^```[a-zA-Z0-9_+-]*[ \\t]*\\r?\\n"));
            QString unwrapped = trimmed;
            unwrapped.remove(openFence);
            static const QRegularExpression closeFence(QStringLiteral("\\r?\\n?```\\s*$"));
            unwrapped.remove(closeFence);
            result = unwrapped;
        }
    }

    // 2. Remove stray XML/FIM tags
    result.remove(QStringLiteral("<|cursor|>"));
    result.remove(QStringLiteral("<|prefix|>"));
    result.remove(QStringLiteral("<|suffix|>"));
    result.remove(QStringLiteral("<code>"));
    result.remove(QStringLiteral("</code>"));
    result.remove(QRegularExpression(QStringLiteral("</?(file_imports_and_declarations|related_open_file)[^>]*>")));

    // 4. If cursor was mid-line, force single-line completion
    if (ctx.isMidLine && result.contains(QLatin1Char('\n'))) {
        result = result.section(QLatin1Char('\n'), 0, 0);
    }

    // 5. Trim trailing whitespace while keeping necessary leading spaces
    while (result.endsWith(QLatin1Char('\r')) || result.endsWith(QLatin1Char('\n'))) {
        result.chop(1);
    }

    // 6. Overlap trimming: If suggestion repeats the immediate suffix, truncate overlap
    const QString trimmedSuffix = ctx.suffix.trimmed();
    if (!trimmedSuffix.isEmpty() && result.endsWith(trimmedSuffix)) {
        result.chop(trimmedSuffix.length());
    } else if (!ctx.suffix.isEmpty()) {
        const QString firstSuffixLine = ctx.suffix.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (!firstSuffixLine.isEmpty() && result.endsWith(firstSuffixLine)) {
            result.chop(firstSuffixLine.length());
        }
    }

    return result;
}
