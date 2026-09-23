#include "settings.h"

#include <KSharedConfig>
#include <KConfigGroup>
#include <KWallet>

#include <QScopedPointer>

using KWallet::Wallet;

namespace {

// Folder and key names used inside the KDE wallet.
const QString kWalletFolder = QStringLiteral("kateantigravity");
const QString kWalletApiKeyEntry = QStringLiteral("GeminiApiKey");

// Open the wallet synchronously for the local application window.
// Returns nullptr if the wallet subsystem is unavailable (e.g. headless test runs).
Wallet *openWallet()
{
    if (!Wallet::isEnabled()) {
        return nullptr;
    }
    Wallet *wallet = Wallet::openWallet(Wallet::LocalWallet(), 0, Wallet::Synchronous);
    if (!wallet) {
        return nullptr;
    }
    if (!wallet->hasFolder(kWalletFolder)) {
        wallet->createFolder(kWalletFolder);
    }
    if (!wallet->setFolder(kWalletFolder)) {
        delete wallet;
        return nullptr;
    }
    return wallet;
}

} // namespace

AgySettings *AgySettings::instance()
{
    static AgySettings s_instance;
    return &s_instance;
}

AgySettings::AgySettings(QObject *parent)
    : QObject(parent)
{
    load();
}

QString AgySettings::readApiKeyFromStore(KConfigGroup &grp)
{
    // Preferred storage: KWallet (encrypted at rest).
    QScopedPointer<Wallet> wallet(openWallet());
    if (wallet) {
        if (wallet->hasEntry(kWalletApiKeyEntry)) {
            QString secret;
            if (wallet->readPassword(kWalletApiKeyEntry, secret) == 0) {
                return secret;
            }
        }

        // Migrate a legacy plaintext key from KConfig into the wallet, then scrub it.
        const QString legacyKey = grp.readEntry(QStringLiteral("ApiKey"), QString());
        if (!legacyKey.isEmpty()) {
            wallet->writePassword(kWalletApiKeyEntry, legacyKey);
            grp.deleteEntry(QStringLiteral("ApiKey"));
            grp.sync();
            return legacyKey;
        }
        return QString();
    }

    // Fallback when the wallet subsystem is unavailable: read the legacy plaintext entry.
    return grp.readEntry(QStringLiteral("ApiKey"), QString());
}

void AgySettings::writeApiKeyToStore(KConfigGroup &grp)
{
    QScopedPointer<Wallet> wallet(openWallet());
    if (wallet) {
        if (apiKey.isEmpty()) {
            if (wallet->hasEntry(kWalletApiKeyEntry)) {
                wallet->removeEntry(kWalletApiKeyEntry);
            }
        } else {
            wallet->writePassword(kWalletApiKeyEntry, apiKey);
        }
        // Ensure no plaintext copy lingers in the config file.
        if (grp.hasKey(QStringLiteral("ApiKey"))) {
            grp.deleteEntry(QStringLiteral("ApiKey"));
        }
        return;
    }

    // Fallback: no wallet available, persist in KConfig (best effort).
    grp.writeEntry(QStringLiteral("ApiKey"), apiKey);
}

void AgySettings::load()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kateantigravityrc"));
    KConfigGroup grp(config, QStringLiteral("General"));

    model = grp.readEntry(QStringLiteral("Model"), QStringLiteral("gemini-3.8-flash-low"));
    // Inline-completion model defaults to the chat model for backward compat.
    completionModel = grp.readEntry(QStringLiteral("CompletionModel"), model);
    debounceMs = grp.readEntry(QStringLiteral("DebounceMs"), 250);
    autoTrigger = grp.readEntry(QStringLiteral("AutoTrigger"), true);
    backendMode = grp.readEntry(QStringLiteral("BackendMode"), 0);
    apiKey = readApiKeyFromStore(grp);
    maxPrefixLines = grp.readEntry(QStringLiteral("MaxPrefixLines"), 80);
    maxSuffixLines = grp.readEntry(QStringLiteral("MaxSuffixLines"), 40);
    chatSidebarPosition = grp.readEntry(QStringLiteral("ChatSidebarPosition"), 1);
    autoSwitchProjectChat = grp.readEntry(QStringLiteral("AutoSwitchProjectChat"), true);
    lastWorkspacePath = grp.readEntry(QStringLiteral("LastWorkspacePath"), QString());
}

void AgySettings::save()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kateantigravityrc"));
    KConfigGroup grp(config, QStringLiteral("General"));

    grp.writeEntry(QStringLiteral("Model"), model);
    grp.writeEntry(QStringLiteral("CompletionModel"), completionModel);
    grp.writeEntry(QStringLiteral("DebounceMs"), debounceMs);
    grp.writeEntry(QStringLiteral("AutoTrigger"), autoTrigger);
    grp.writeEntry(QStringLiteral("BackendMode"), backendMode);
    writeApiKeyToStore(grp);
    grp.writeEntry(QStringLiteral("MaxPrefixLines"), maxPrefixLines);
    grp.writeEntry(QStringLiteral("MaxSuffixLines"), maxSuffixLines);
    grp.writeEntry(QStringLiteral("ChatSidebarPosition"), chatSidebarPosition);
    grp.writeEntry(QStringLiteral("AutoSwitchProjectChat"), autoSwitchProjectChat);
    grp.writeEntry(QStringLiteral("LastWorkspacePath"), lastWorkspacePath);
    grp.sync();

    Q_EMIT settingsChanged();
}

void AgySettings::resetToDefaults()
{
    model = QStringLiteral("gemini-3.8-flash-low");
    completionModel = QStringLiteral("gemini-3.8-flash-low");
    debounceMs = 250;
    autoTrigger = true;
    backendMode = 0;
    apiKey.clear();
    maxPrefixLines = 80;
    maxSuffixLines = 40;
    chatSidebarPosition = 1;
    autoSwitchProjectChat = true;
}
