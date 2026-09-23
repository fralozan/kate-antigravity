#ifndef AGY_SETTINGS_H
#define AGY_SETTINGS_H

#include <QObject>
#include <QString>

class KConfigGroup;

class AgySettings : public QObject
{
    Q_OBJECT

public:
    static AgySettings *instance();

    void load();
    void save();
    void resetToDefaults();

    QString model = QStringLiteral("gemini-3.8-flash-low");           // chat / actions model
    QString completionModel = QStringLiteral("gemini-3.8-flash-low"); // inline ghost-text model
    int debounceMs = 250;
    bool autoTrigger = true;
    int backendMode = 0; // 0: AgyCli (stream-json), 1: DirectApi
    QString apiKey;
    int maxPrefixLines = 80;
    int maxSuffixLines = 40;
    int chatSidebarPosition = 1; // 0: Left, 1: Right
    bool autoSwitchProjectChat = true;
    QString lastWorkspacePath; // root of the workspace active when Kate last closed

Q_SIGNALS:
    void settingsChanged();

private:
    explicit AgySettings(QObject *parent = nullptr);

    // API key persistence via KWallet (with KConfig fallback and legacy migration).
    QString readApiKeyFromStore(KConfigGroup &grp);
    void writeApiKeyToStore(KConfigGroup &grp);
};

#endif // AGY_SETTINGS_H
