#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include "ccmanager.h"
#include "ripessettings.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QFontDialog>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpacerItem>
#include <QSpinBox>
#include <QStackedWidget>

namespace Ripes {

std::pair<QWidget*, QGridLayout*> constructPage() {
    auto* pageWidget = new QWidget();
    auto* pageLayout = new QVBoxLayout();
    auto* itemLayout = new QGridLayout();

    pageLayout->addLayout(itemLayout);
    pageWidget->setLayout(pageLayout);
    pageLayout->addStretch(1);

    return {pageWidget, itemLayout};
}

template <typename T_TriggerWidget, typename T_EditWidget = T_TriggerWidget>
std::pair<QLabel*, T_TriggerWidget*> createSettingsWidgets(const QString& settingName, const QString& labelText) {
    auto* label = new QLabel(labelText);

    T_TriggerWidget* widget = new T_TriggerWidget();

    auto* settingObserver = RipesSettings::getObserver(settingName);

    if constexpr (std::is_same<T_EditWidget, QSpinBox>()) {
        // Ensure that the current value can be represented in the spinbox. It is expected that the spinbox range will
        // be specified after being created in this function.
        widget->setRange(INT_MIN, INT_MAX);
        widget->setValue(settingObserver->value().toUInt());
        widget->connect(widget, QOverload<int>::of(&QSpinBox::valueChanged), settingObserver,
                        &SettingObserver::setValue);
    } else if constexpr (std::is_same<T_EditWidget, QLineEdit>()) {
        widget->connect(widget, &QLineEdit::textChanged, settingObserver, &SettingObserver::setValue);
        widget->setText(settingObserver->value().toString());
    } else if constexpr (std::is_same<T_EditWidget, QCheckBox>()) {
        widget->connect(widget, &QCheckBox::toggled, settingObserver, &SettingObserver::setValue);
        widget->setChecked(settingObserver->value().toBool());
    } else if constexpr (std::is_same<T_EditWidget, QColorDialog>()) {
        // Create a QPushButton which will trigger a QColorWidget when clicked. Changes in the color settings will
        // trigger a change in the pushbutton color
        auto colorSetterFunctor = [=] {
            QPalette pal = widget->palette();
            pal.setColor(QPalette::Button, settingObserver->value().value<QColor>());
            widget->setAutoFillBackground(true);
            widget->setPalette(pal);
        };

        // We want changes in the set color to propagate to the button, while in the dialog. But this connection must be
        // deleted once the dialog is closed, to avoid a dangling connection between the settings object and the trigger
        // widget.
        auto conn = settingObserver->connect(settingObserver, &SettingObserver::modified, colorSetterFunctor);
        widget->connect(widget, &QObject::destroyed, [=] { settingObserver->disconnect(conn); });

        widget->connect(widget, &QPushButton::clicked, [=](bool) {
            QColorDialog diag;
            diag.setCurrentColor(settingObserver->value().value<QColor>());
            if (diag.exec()) {
                settingObserver->setValue(diag.selectedColor());
            }
        });

        // Apply color of current setting
        colorSetterFunctor();
    } else if constexpr (std::is_same<T_EditWidget, QFontDialog>()) {
        // Create a QPushButton which will trigger a QFontDialog when clicked. Changes in the font settings will
        // trigger a change in the pushbutton text
        auto fontSetterFunctor = [=] {
            const auto& font = settingObserver->value().value<QFont>();
            const QString text = font.family() + " | " + QString::number(font.pointSize());
            widget->setText(text);
        };

        // We want changes in the set font to propagate to the button, while in the dialog. But this connection must be
        // deleted once the dialog is closed, to avoid a dangling connection between the settings object and the trigger
        // widget.
        auto conn = settingObserver->connect(settingObserver, &SettingObserver::modified, fontSetterFunctor);
        widget->connect(widget, &QObject::destroyed, [=] { settingObserver->disconnect(conn); });

        widget->connect(widget, &QPushButton::clicked, [=](bool) {
            QFontDialog diag;
            diag.setCurrentFont(settingObserver->value().value<QFont>());
            diag.setOption(QFontDialog::MonospacedFonts, true);
            if (diag.exec()) {
                settingObserver->setValue(diag.selectedFont());
            }
        });

        // Apply font of current setting
        fontSetterFunctor();
    }

    return {label, widget};
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), m_ui(new Ui::SettingsDialog) {
    m_ui->setupUi(this);
    m_ui->settingsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ui->buttonBox->setStandardButtons(QDialogButtonBox::Ok);
    setWindowTitle("Settings");

    // Create settings pages
    addPage("Editor", createEditorPage());
    addPage("Simulator", createSimulatorPage());
    addPage("Environment", createEnvironmentPage());

    m_ui->settingsList->setCurrentRow(RipesSettings::value(RIPES_SETTING_SETTING_TAB).toInt());
}

SettingsDialog::~SettingsDialog() {
    delete m_ui;
}

void SettingsDialog::accept() {
    QDialog::accept();
}

QWidget* SettingsDialog::createEditorPage() {
    auto [pageWidget, pageLayout] = constructPage();

    // Setting: RIPES_SETTING_CCPATH
    CCManager::get();

    auto* CCGroupBox = new QGroupBox("RISC-V C/C++ Compiler");
    auto* CCLayout = new QVBoxLayout();
    CCGroupBox->setLayout(CCLayout);
    auto* CCDesc = new QLabel(
        "Providing a compatible RISC-V C/C++ compiler enables editing, compilation "
        "and execution of C-language programs within Ripes.\n\n"
        "A compiler may be autodetected if availabe in PATH.");
    CCDesc->setWordWrap(true);
    CCLayout->addWidget(CCDesc);

    auto* CCHLayout = new QHBoxLayout();
    auto [cclabel, ccpath] = createSettingsWidgets<QLineEdit>(RIPES_SETTING_CCPATH, "Compiler path:");
    m_ccpath = ccpath;
    CCLayout->addLayout(CCHLayout);
    CCHLayout->addWidget(cclabel);
    CCHLayout->addWidget(ccpath);
    auto* pathBrowseButton = new QPushButton("Browse");
    connect(pathBrowseButton, &QPushButton::clicked, [=, ccpath = ccpath] {
        QFileDialog dialog(this);
        dialog.setAcceptMode(QFileDialog::AcceptOpen);
        if (dialog.exec()) {
            ccpath->setText(dialog.selectedFiles()[0]);
        }
    });

    // Make changes in the CC path trigger revalidation in the CCManager
    connect(ccpath, &QLineEdit::textChanged, &CCManager::get(), &CCManager::trySetCC);

    // Make CCManager compiler changes trigger background color of the ccpath, indicating whether the CC was determined
    // to be valid
    connect(&CCManager::get(), &CCManager::ccChanged, this, &SettingsDialog::CCPathChanged);

    CCHLayout->addWidget(pathBrowseButton);

    // Add compiler arguments widget
    auto [ccArgLabel, ccArgs] = createSettingsWidgets<QLineEdit>(RIPES_SETTING_CCARGS, "Compiler arguments:");
    auto* CCArgHLayout = new QHBoxLayout();
    CCArgHLayout->addWidget(ccArgLabel);
    CCArgHLayout->addWidget(ccArgs);
    CCLayout->addLayout(CCArgHLayout);
    // Make changes in argument reemit ccpath text changed. By doing so, we revalidate the compiler once new arguments
    // have been added, implicitely validating the arguments along with it.
    connect(ccArgs, &QLineEdit::textChanged, [=, ccpath = ccpath] { emit ccpath->textChanged(ccpath->text()); });

    // Add linker arguments widget
    auto [ldArgLabel, ldArgs] = createSettingsWidgets<QLineEdit>(RIPES_SETTING_LDARGS, "Linker arguments:");
    auto* LDArgHLayout = new QHBoxLayout();
    LDArgHLayout->addWidget(ldArgLabel);
    LDArgHLayout->addWidget(ldArgs);
    CCLayout->addLayout(LDArgHLayout);
    connect(ldArgs, &QLineEdit::textChanged, [=, ccpath = ccpath] { emit ccpath->textChanged(ccpath->text()); });

    // Add effective compile command line view
    auto* CCCLineHLayout = new QHBoxLayout();
    m_compileInfoHeader = new QLabel();
    CCCLineHLayout->addWidget(m_compileInfoHeader);
    m_compileInfo = new QLabel();
    m_compileInfo->setWordWrap(true);
    m_compileInfo->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    m_compileInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    CCCLineHLayout->addWidget(m_compileInfo);
    CCLayout->addLayout(CCCLineHLayout);

    pageLayout->addWidget(CCGroupBox);

    // Trigger initial rehighlighting
    CCManager::get().trySetCC(m_ccpath->text());

    return pageWidget;
}

void SettingsDialog::CCPathChanged(CCManager::CCRes res) {
    QPalette palette = this->palette();
    if (res.success) {
        palette.setColor(QPalette::Base, QColor(Qt::green).lighter());
        m_compileInfoHeader->setText("Compile command:");
    } else {
        palette.setColor(QPalette::Base, QColor(Qt::red).lighter());
        m_compileInfoHeader->setText("Error:");
    }
    m_ccpath->setPalette(palette);

    // Update compile command preview
    auto [cstring, cargs] = CCManager::get().createCompileCommand("${input}", "${output}");
    if (m_compileInfo) {
        if (res.success) {
            m_compileInfo->setText(cstring + " " + cargs.join(" "));
        } else {
            m_compileInfo->setText(res.errorMessage);
        }
    }
}

QWidget* SettingsDialog::createSimulatorPage() {
    auto [pageWidget, pageLayout] = constructPage();

    // Setting: RIPES_SETTING_REWINDSTACKSIZE
    auto [rewindLabel, rewindSpinbox] =
        createSettingsWidgets<QSpinBox>(RIPES_SETTING_REWINDSTACKSIZE, "Max. undo cycles:");
    rewindSpinbox->setRange(0, INT_MAX);

    pageLayout->addWidget(rewindLabel, 0, 0);
    pageLayout->addWidget(rewindSpinbox, 0, 1);

    return pageWidget;
}

QWidget* SettingsDialog::createEnvironmentPage() {
    auto* consoleGroupBox = new QGroupBox("Console");
    auto* consoleLayout = new QGridLayout();
    consoleGroupBox->setLayout(consoleLayout);

    auto [pageWidget, pageLayout] = constructPage();

    // Setting: RIPES_SETTING_CONSOLEECHO
    auto [echoLabel, echoCheckbox] = createSettingsWidgets<QCheckBox>(RIPES_SETTING_CONSOLEECHO, "Echo console input:");
    consoleLayout->addWidget(echoLabel, 0, 0);
    consoleLayout->addWidget(echoCheckbox, 0, 1);

    // Setting: RIPES_SETTING_CONSOLEFONT
    auto [fontLabel, fontButton] =
        createSettingsWidgets<QPushButton, QFontDialog>(RIPES_SETTING_CONSOLEFONT, "Console font:");
    consoleLayout->addWidget(fontLabel, 1, 0);
    consoleLayout->addWidget(fontButton, 1, 1);

    // Setting: RIPES_SETTING_CONSOLEFONTCOLOR
    auto [fontColorLabel, fontColorButton] =
        createSettingsWidgets<QPushButton, QColorDialog>(RIPES_SETTING_CONSOLEFONTCOLOR, "Console font color:");
    consoleLayout->addWidget(fontColorLabel, 2, 0);
    consoleLayout->addWidget(fontColorButton, 2, 1);

    // Setting: RIPES_SETTING_CONSOLEBG
    auto [bgColorLabel, bgColorButton] =
        createSettingsWidgets<QPushButton, QColorDialog>(RIPES_SETTING_CONSOLEBG, "Console background color:");
    consoleLayout->addWidget(bgColorLabel, 3, 0);
    consoleLayout->addWidget(bgColorButton, 3, 1);

    pageLayout->addWidget(consoleGroupBox);

    return pageWidget;
}

void SettingsDialog::addPage(const QString& name, QWidget* page) {
    const int index = m_ui->settingsPages->addWidget(page);
    m_pageIndex[name] = index;

    auto* item = new QListWidgetItem(name);
    m_ui->settingsList->addItem(item);

    connect(m_ui->settingsList, &QListWidget::currentItemChanged, [=](QListWidgetItem* current, QListWidgetItem*) {
        const QString name = current->text();
        Q_ASSERT(m_pageIndex.count(name));

        const int index = m_pageIndex.at(name);
        m_ui->settingsPages->setCurrentIndex(index);
        RipesSettings::setValue(RIPES_SETTING_SETTING_TAB, index);
    });
}

}  // namespace Ripes
