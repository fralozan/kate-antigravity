#ifndef AGY_CONFIGPAGE_H
#define AGY_CONFIGPAGE_H

#include <KTextEditor/ConfigPage>

class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QWidget;

class AgyConfigPage : public KTextEditor::ConfigPage
{
    Q_OBJECT

public:
    explicit AgyConfigPage(QWidget *parent = nullptr);
    ~AgyConfigPage() override = default;

    QString name() const override;
    QString fullName() const override;
    QIcon icon() const override;

public Q_SLOTS:
    void apply() override;
    void reset() override;
    void defaults() override;

private:
    void setupUi();
    void loadSettings();

    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_completionModelCombo = nullptr;
    QSpinBox *m_debounceSpin = nullptr;
    QCheckBox *m_autoTriggerCheck = nullptr;
    QComboBox *m_backendCombo = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QSpinBox *m_prefixSpin = nullptr;
    QSpinBox *m_suffixSpin = nullptr;
    QComboBox *m_chatPositionCombo = nullptr;
    QCheckBox *m_autoSwitchProjectChatCheck = nullptr;
};

#endif // AGY_CONFIGPAGE_H
