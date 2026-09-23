#include "plugin.h"
#include "pluginview.h"
#include "configpage.h"

#include <KPluginFactory>

K_PLUGIN_CLASS_WITH_JSON(KateAntigravityPlugin, "kateantigravity.json")

KateAntigravityPlugin::KateAntigravityPlugin(QObject *parent, const QVariantList &args)
    : KTextEditor::Plugin(parent)
{
    Q_UNUSED(args);
}

QObject *KateAntigravityPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    return new KateAntigravityPluginView(this, mainWindow);
}

int KateAntigravityPlugin::configPages() const
{
    return 1;
}

KTextEditor::ConfigPage *KateAntigravityPlugin::configPage(int number, QWidget *parent)
{
    if (number == 0) {
        return new AgyConfigPage(parent);
    }
    return nullptr;
}

#include "plugin.moc"
