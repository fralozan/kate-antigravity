#include "symbolindex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQueue>
#include <QPair>
#include <QSet>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QTextStream>

namespace SymbolIndex {

namespace {

const QSet<QString> &supportedExtensions()
{
    static const QSet<QString> s_exts = {
        QStringLiteral("c"), QStringLiteral("h"), QStringLiteral("cpp"), QStringLiteral("cc"),
        QStringLiteral("cxx"), QStringLiteral("hpp"), QStringLiteral("hh"),
        QStringLiteral("py"),
        QStringLiteral("js"), QStringLiteral("jsx"), QStringLiteral("ts"), QStringLiteral("tsx"),
        QStringLiteral("rs"), QStringLiteral("go"), QStringLiteral("java"), QStringLiteral("kt"),
    };
    return s_exts;
}

const QSet<QString> &prunedDirs()
{
    static const QSet<QString> s_dirs = {
        QStringLiteral(".git"), QStringLiteral("node_modules"), QStringLiteral("vendor"),
        QStringLiteral("build"), QStringLiteral("dist"), QStringLiteral(".cache"),
        QStringLiteral("__pycache__"), QStringLiteral(".idea"), QStringLiteral(".vscode"),
        QStringLiteral("target"), QStringLiteral("bin"), QStringLiteral("obj"),
        QStringLiteral("venv"), QStringLiteral(".venv"),
    };
    return s_dirs;
}

// Build the set of {kind, regex} definition patterns for `name`. The name is
// regex-escaped and matched with a word boundary so "foo" doesn't match "foobar".
struct DefPattern {
    QString kind;
    QRegularExpression re;
};

QList<DefPattern> definitionPatterns(const QString &name)
{
    const QString n = QRegularExpression::escape(name);
    QList<DefPattern> pats;

    // C/C++/Java/JS function or method: `... name(`  (heuristic)
    pats.append({ QStringLiteral("function"),
        QRegularExpression(QStringLiteral("^[\\w:<>\\*&\\s]*\\b%1\\s*\\(").arg(n)) });
    // class / struct / interface / enum
    pats.append({ QStringLiteral("type"),
        QRegularExpression(QStringLiteral("\\b(class|struct|interface|enum|trait|union)\\s+%1\\b").arg(n)) });
    // Python def / class
    pats.append({ QStringLiteral("python-def"),
        QRegularExpression(QStringLiteral("^\\s*(def|class)\\s+%1\\b").arg(n)) });
    // JS/TS declarations: const/let/var/function name, or `name =` arrow
    pats.append({ QStringLiteral("js"),
        QRegularExpression(QStringLiteral("\\b(function|const|let|var)\\s+%1\\b").arg(n)) });
    // Rust / Go: fn name / func name / type name
    pats.append({ QStringLiteral("rust-go"),
        QRegularExpression(QStringLiteral("\\b(fn|func|type)\\s+%1\\b").arg(n)) });

    return pats;
}

QString extractSnippet(const QStringList &lines, int matchIndex)
{
    // A small window: the definition line plus up to the following lines, until
    // a blank-line gap or a cap. This keeps the snippet focused and bounded.
    const int start = matchIndex;
    const int maxSpan = 24;
    int end = matchIndex;
    int braceDepth = 0;
    bool sawBrace = false;

    for (int i = matchIndex; i < lines.size() && (i - start) < maxSpan; ++i) {
        const QString &l = lines.at(i);
        for (const QChar c : l) {
            if (c == QLatin1Char('{')) { braceDepth++; sawBrace = true; }
            else if (c == QLatin1Char('}')) { braceDepth--; }
        }
        end = i;
        if (sawBrace && braceDepth <= 0) {
            break; // Closed the definition body.
        }
        // Python-style: stop at a dedent to column 0 after the first line.
        if (!sawBrace && i > matchIndex) {
            const QString trimmed = l.trimmed();
            if (!trimmed.isEmpty() && !l.at(0).isSpace()) {
                end = i - 1;
                break;
            }
        }
    }

    QStringList out;
    for (int i = start; i <= end && i < lines.size(); ++i) {
        out << lines.at(i);
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace

bool isSupportedSourceFile(const QString &fileName)
{
    return supportedExtensions().contains(QFileInfo(fileName).suffix().toLower());
}

QStringList extractCandidateIdentifiers(const QString &text, int maxCandidates)
{
    static const QSet<QString> stop = {
        // Common English words / keywords that are not symbols worth resolving.
        QStringLiteral("the"), QStringLiteral("this"), QStringLiteral("that"),
        QStringLiteral("with"), QStringLiteral("from"), QStringLiteral("into"),
        QStringLiteral("code"), QStringLiteral("function"), QStringLiteral("class"),
        QStringLiteral("method"), QStringLiteral("return"), QStringLiteral("value"),
        QStringLiteral("please"), QStringLiteral("explain"), QStringLiteral("refactor"),
        QStringLiteral("fix"), QStringLiteral("test"), QStringLiteral("tests"),
        QStringLiteral("file"), QStringLiteral("files"), QStringLiteral("error"),
        QStringLiteral("errors"), QStringLiteral("bug"), QStringLiteral("add"),
        // Spanish equivalents.
        QStringLiteral("esta"), QStringLiteral("este"), QStringLiteral("como"),
        QStringLiteral("para"), QStringLiteral("codigo"), QStringLiteral("funcion"),
        QStringLiteral("clase"), QStringLiteral("metodo"), QStringLiteral("archivo"),
        QStringLiteral("explica"), QStringLiteral("corrige"), QStringLiteral("prueba"),
    };

    static const QRegularExpression tokenRe(QStringLiteral("[A-Za-z_][A-Za-z0-9_]{2,}"));

    QStringList out;
    QSet<QString> seen;
    auto it = tokenRe.globalMatch(text);
    while (it.hasNext() && out.size() < maxCandidates) {
        const QString tok = it.next().captured(0);
        if (seen.contains(tok)) {
            continue;
        }
        // Skip all-lowercase common words; keep tokens that look like code
        // (contain '_', an uppercase after the first char, or a digit).
        const bool looksLikeCode = tok.contains(QLatin1Char('_'))
            || tok.mid(1).contains(QRegularExpression(QStringLiteral("[A-Z0-9]")));
        if (!looksLikeCode) {
            if (stop.contains(tok.toLower()) || tok.length() < 4) {
                continue;
            }
        }
        seen.insert(tok);
        out.append(tok);
    }
    return out;
}

QList<SymbolMatch> findDefinitions(const QString &projectRoot,
                                   const QString &symbolName,
                                   int maxResults)
{
    QList<SymbolMatch> results;
    const QString name = symbolName.trimmed();
    if (projectRoot.isEmpty() || name.isEmpty()) {
        return results;
    }

    const QDir rootDir(projectRoot);
    if (!rootDir.exists()) {
        return results;
    }

    const QList<DefPattern> patterns = definitionPatterns(name);

    // Bounded BFS over the project tree (mirrors the indexer's pruning), with a
    // wall-clock guard so a huge tree can't stall the chat.
    QQueue<QPair<QString, int>> queue;
    queue.enqueue({projectRoot, 0});
    const int maxDepth = 6;
    const int maxFilesScanned = 1500;
    int filesScanned = 0;
    QElapsedTimer timer;
    timer.start();

    while (!queue.isEmpty() && results.size() < maxResults && filesScanned < maxFilesScanned) {
        if (timer.elapsed() > 1500) {
            break;
        }
        const auto current = queue.dequeue();
        QDir dir(current.first);
        const int depth = current.second;

        const auto entries = dir.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::NoSort);

        for (const QFileInfo &fi : entries) {
            if (fi.isDir()) {
                const QString dirName = fi.fileName().toLower();
                if (prunedDirs().contains(dirName) || dirName.startsWith(QLatin1Char('.'))) {
                    continue;
                }
                if (depth < maxDepth) {
                    queue.enqueue({fi.filePath(), depth + 1});
                }
                continue;
            }

            if (!isSupportedSourceFile(fi.fileName())) {
                continue;
            }
            if (filesScanned >= maxFilesScanned) {
                break;
            }
            filesScanned++;

            QFile file(fi.filePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }

            // Read bounded content (avoid pulling in huge generated files).
            const QByteArray raw = file.read(512 * 1024);
            file.close();
            const QString content = QString::fromUtf8(raw);
            const QStringList lines = content.split(QLatin1Char('\n'));

            for (int i = 0; i < lines.size(); ++i) {
                const QString &line = lines.at(i);
                // Cheap pre-filter: the name must appear on the line at all.
                if (!line.contains(name)) {
                    continue;
                }
                for (const DefPattern &p : patterns) {
                    if (p.re.match(line).hasMatch()) {
                        SymbolMatch m;
                        m.name = name;
                        m.fullPath = fi.filePath();
                        m.relativePath = rootDir.relativeFilePath(fi.filePath());
                        m.line = i + 1;
                        m.kind = p.kind;
                        m.snippet = extractSnippet(lines, i);
                        results.append(m);
                        break; // one match kind per line is enough
                    }
                }
                if (results.size() >= maxResults) {
                    break;
                }
            }
        }
    }

    return results;
}

} // namespace SymbolIndex
