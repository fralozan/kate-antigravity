#include "configpage.h"
#include "settings.h"
#include "agymodels.h"

#include <KLocalizedString>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QIcon>
#include <QDesktopServices>
#include <QUrl>

AgyConfigPage::AgyConfigPage(QWidget *parent)
    : KTextEditor::ConfigPage(parent)
{
    setupUi();
    loadSettings();
}

QString AgyConfigPage::name() const
{
    return i18n("Antigravity");
}

QString AgyConfigPage::fullName() const
{
    return i18n("Antigravity AI Ghost Text Settings");
}

QIcon AgyConfigPage::icon() const
{
    return QIcon::fromTheme(QStringLiteral("code-context"));
}

void AgyConfigPage::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Group: General
    auto *generalGroup = new QGroupBox(i18n("Inline Code Completion"), this);
    auto *generalForm = new QFormLayout(generalGroup);

    m_autoTriggerCheck = new QCheckBox(i18n("Enable automatic suggestions while typing"), this);
    generalForm->addRow(m_autoTriggerCheck);

    m_debounceSpin = new QSpinBox(this);
    m_debounceSpin->setRange(50, 2000);
    m_debounceSpin->setSingleStep(25);
    m_debounceSpin->setSuffix(QStringLiteral(" ms"));
    generalForm->addRow(i18n("Suggestion delay (Debounce):"), m_debounceSpin);

    mainLayout->addWidget(generalGroup);

    // Group: Modelo e Inteligencia Artificial
    auto *modelGroup = new QGroupBox(i18n("Model & AI"), this);
    auto *modelForm = new QFormLayout(modelGroup);

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setEditable(true);
    m_modelCombo->addItems(AgyModels::commonModels());
    modelForm->addRow(i18n("Chat model:"), m_modelCombo);

    m_completionModelCombo = new QComboBox(this);
    m_completionModelCombo->setEditable(true);
    m_completionModelCombo->addItems(AgyModels::commonModels());
    m_completionModelCombo->setToolTip(i18n("Model used for inline ghost-text completions (a fast model is recommended)."));
    modelForm->addRow(i18n("Inline completion model:"), m_completionModelCombo);

    m_backendCombo = new QComboBox(this);
    m_backendCombo->addItem(i18n("Antigravity CLI (stream-json, zero-config)"), 0);
    m_backendCombo->addItem(i18n("Direct Gemini API (HTTPS)"), 1);
    modelForm->addRow(i18n("Connection mode:"), m_backendCombo);

    auto *apiKeyLayout = new QHBoxLayout();
    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(i18n("Optional: only required for Direct API mode"));

    auto *manageApiKeyBtn = new QPushButton(i18n("Manage API Key..."), this);
    manageApiKeyBtn->setIcon(QIcon::fromTheme(QStringLiteral("internet-web-browser")));
    manageApiKeyBtn->setToolTip(i18n("Open Google AI Studio (https://aistudio.google.com/app/api-keys) in browser"));
    connect(manageApiKeyBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://aistudio.google.com/app/api-keys")));
    });

    apiKeyLayout->addWidget(m_apiKeyEdit);
    apiKeyLayout->addWidget(manageApiKeyBtn);
    modelForm->addRow(i18n("Gemini API Key:"), apiKeyLayout);

    mainLayout->addWidget(modelGroup);

    // Group: Contexto de Código (FIM)
    auto *contextGroup = new QGroupBox(i18n("Context Window (Lines)"), this);
    auto *contextForm = new QFormLayout(contextGroup);

    m_prefixSpin = new QSpinBox(this);
    m_prefixSpin->setRange(10, 300);
    m_prefixSpin->setSingleStep(10);
    contextForm->addRow(i18n("Lines before cursor (Prefix):"), m_prefixSpin);

    m_suffixSpin = new QSpinBox(this);
    m_suffixSpin->setRange(5, 150);
    m_suffixSpin->setSingleStep(5);
    contextForm->addRow(i18n("Lines after cursor (Suffix):"), m_suffixSpin);

    mainLayout->addWidget(contextGroup);

    // Group: Panel de Chat
    auto *chatGroup = new QGroupBox(i18n("Chat Window (Antigravity)"), this);
    auto *chatForm = new QFormLayout(chatGroup);

    m_chatPositionCombo = new QComboBox(this);
    m_chatPositionCombo->addItem(i18n("Right sidebar (Independent panel)"), 1);
    m_chatPositionCombo->addItem(i18n("Left sidebar (Next to Documents/Projects)"), 0);
    chatForm->addRow(i18n("Panel position:"), m_chatPositionCombo);

    m_autoSwitchProjectChatCheck = new QCheckBox(i18n("Automatically switch chat session to active file's project"), this);
    chatForm->addRow(m_autoSwitchProjectChatCheck);

    mainLayout->addWidget(chatGroup);

    mainLayout->addStretch();

    // Enable/disable API Key field according to backend mode
    connect(m_backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_apiKeyEdit->setEnabled(index == 1);
    });

    // Notify of changes
    connect(m_autoTriggerCheck, &QCheckBox::toggled, this, &AgyConfigPage::changed);
    connect(m_debounceSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AgyConfigPage::changed);
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, &AgyConfigPage::changed);
    connect(m_completionModelCombo, &QComboBox::currentTextChanged, this, &AgyConfigPage::changed);
    connect(m_backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AgyConfigPage::changed);
    connect(m_apiKeyEdit, &QLineEdit::textChanged, this, &AgyConfigPage::changed);
    connect(m_prefixSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AgyConfigPage::changed);
    connect(m_suffixSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AgyConfigPage::changed);
    connect(m_chatPositionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AgyConfigPage::changed);
    connect(m_autoSwitchProjectChatCheck, &QCheckBox::toggled, this, &AgyConfigPage::changed);
}

void AgyConfigPage::loadSettings()
{
    AgySettings *s = AgySettings::instance();
    s->load();

    m_autoTriggerCheck->setChecked(s->autoTrigger);
    m_debounceSpin->setValue(s->debounceMs);

    int modelIdx = m_modelCombo->findText(s->model);
    if (modelIdx >= 0) {
        m_modelCombo->setCurrentIndex(modelIdx);
    } else {
        m_modelCombo->setEditText(s->model);
    }

    int complIdx = m_completionModelCombo->findText(s->completionModel);
    if (complIdx >= 0) {
        m_completionModelCombo->setCurrentIndex(complIdx);
    } else {
        m_completionModelCombo->setEditText(s->completionModel);
    }

    m_backendCombo->setCurrentIndex(s->backendMode);
    m_apiKeyEdit->setText(s->apiKey);
    m_apiKeyEdit->setEnabled(s->backendMode == 1);

    m_prefixSpin->setValue(s->maxPrefixLines);
    m_suffixSpin->setValue(s->maxSuffixLines);

    const int posIdx = (s->chatSidebarPosition == 0) ? 1 : 0;
    m_chatPositionCombo->setCurrentIndex(posIdx);
    m_autoSwitchProjectChatCheck->setChecked(s->autoSwitchProjectChat);
}

void AgyConfigPage::apply()
{
    AgySettings *s = AgySettings::instance();
    s->autoTrigger = m_autoTriggerCheck->isChecked();
    s->debounceMs = m_debounceSpin->value();
    s->model = m_modelCombo->currentText();
    s->completionModel = m_completionModelCombo->currentText();
    s->backendMode = m_backendCombo->currentData().toInt();
    s->apiKey = m_apiKeyEdit->text();
    s->maxPrefixLines = m_prefixSpin->value();
    s->maxSuffixLines = m_suffixSpin->value();
    s->chatSidebarPosition = (m_chatPositionCombo->currentIndex() == 1) ? 0 : 1;
    s->autoSwitchProjectChat = m_autoSwitchProjectChatCheck->isChecked();
    s->save();
}

void AgyConfigPage::reset()
{
    loadSettings();
}

void AgyConfigPage::defaults()
{
    AgySettings *s = AgySettings::instance();
    s->resetToDefaults();
    loadSettings();
    Q_EMIT changed();
}
