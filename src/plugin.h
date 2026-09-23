#ifndef AGY_PLUGIN_H
#define AGY_PLUGIN_H

#include <KTextEditor/Plugin>

class KateAntigravityPlugin : public KTextEditor::Plugin
{
    Q_OBJECT

public:
    explicit KateAntigravityPlugin(QObject *parent = nullptr, const QVariantList &args = {});
    ~KateAntigravityPlugin() override = default;

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;

    int configPages() const override;
    KTextEditor::ConfigPage *configPage(int number = 0, QWidget *parent = nullptr) override;
};

#endif // AGY_PLUGIN_H
