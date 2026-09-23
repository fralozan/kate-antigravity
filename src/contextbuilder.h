#ifndef AGY_CONTEXTBUILDER_H
#define AGY_CONTEXTBUILDER_H

#include <QString>
#include <QList>
#include <KTextEditor/Cursor>

namespace KTextEditor {
class View;
}

struct RelatedFileContext
{
    QString fileName;
    QString language;
    QString snippet;
};

struct CompletionContext
{
    QString prefix;
    QString suffix;
    QString fileHeader;     // Primeras líneas fijas (imports, cabeceras) si el cursor está avanzado
    QString indentation;    // Indentación actual de la línea
    bool isMidLine = false; // Si el cursor está en medio de una sentencia en la misma línea
    QString language;
    QString fileName;
    QList<RelatedFileContext> relatedFiles; // Contexto de pestañas abiertas en Kate
    KTextEditor::Cursor cursor = KTextEditor::Cursor::invalid();
    uint64_t requestId = 0;
};

class ContextBuilder
{
public:
    static CompletionContext extractContext(KTextEditor::View *view,
                                            int maxPrefixLines = 80,
                                            int maxSuffixLines = 40);

    static QString buildPrompt(const CompletionContext &ctx);

    static QString sanitizeResponse(const QString &rawResponse,
                                   const CompletionContext &ctx);
};

#endif // AGY_CONTEXTBUILDER_H
