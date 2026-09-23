#ifndef AGY_MODELS_H
#define AGY_MODELS_H

#include <QString>
#include <QStringList>

// Single source of truth for the AI models offered across the plugin UI
// (config page, chat model badge) and for mapping arbitrary model names to
// the concrete models exposed by the Gemini REST (Direct API) endpoint.
namespace AgyModels {

// Models shown in selectors (config page and chat model badge dropdown).
QStringList commonModels();

// Default completion/chat model.
QString defaultModel();

// Map an arbitrary model name to a model the Gemini REST API actually serves.
// The Antigravity CLI accepts a broader set of names (Claude, GPT, internal
// Gemini variants) than the public generativelanguage.googleapis.com endpoint,
// so Direct API requests must fall back to a supported Gemini model.
QString restApiModelFor(const QString &model);

} // namespace AgyModels

#endif // AGY_MODELS_H
