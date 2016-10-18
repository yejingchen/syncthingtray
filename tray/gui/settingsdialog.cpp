#include "./settingsdialog.h"

#include "../../connector/syncthingconnection.h"
#include "../../connector/syncthingconfig.h"
#include "../../connector/syncthingprocess.h"

#include "ui_connectionoptionpage.h"
#include "ui_notificationsoptionpage.h"
#include "ui_appearanceoptionpage.h"
#include "ui_autostartoptionpage.h"
#include "ui_launcheroptionpage.h"
#include "ui_webviewoptionpage.h"

#include "resources/config.h"

#include <qtutilities/settingsdialog/optioncategory.h>
#include <qtutilities/settingsdialog/optioncategorymodel.h>
#include <qtutilities/settingsdialog/qtsettings.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QHostAddress>
#if defined(PLATFORM_LINUX) && !defined(Q_OS_ANDROID)
# include <QStandardPaths>
#elif defined(PLATFORM_WINDOWS)
# include <QSettings>
#endif
#include <QFontDatabase>
#include <QTextCursor>

#include <functional>

using namespace std;
using namespace std::placeholders;
using namespace Settings;
using namespace Dialogs;
using namespace Data;

namespace QtGui {

// ConnectionOptionPage
ConnectionOptionPage::ConnectionOptionPage(Data::SyncthingConnection *connection, QWidget *parentWidget) :
    ConnectionOptionPageBase(parentWidget),
    m_connection(connection),
    m_currentIndex(0)
{}

ConnectionOptionPage::~ConnectionOptionPage()
{}

QWidget *ConnectionOptionPage::setupWidget()
{
    auto *w = ConnectionOptionPageBase::setupWidget();
    ui()->certPathSelection->provideCustomFileMode(QFileDialog::ExistingFile);
    ui()->certPathSelection->lineEdit()->setPlaceholderText(QCoreApplication::translate("QtGui::ConnectionOptionPage", "Auto-detected for local instance"));
    QObject::connect(m_connection, &SyncthingConnection::statusChanged, bind(&ConnectionOptionPage::updateConnectionStatus, this));
    QObject::connect(ui()->connectPushButton, &QPushButton::clicked, bind(&ConnectionOptionPage::applyAndReconnect, this));
    QObject::connect(ui()->insertFromConfigFilePushButton, &QPushButton::clicked, bind(&ConnectionOptionPage::insertFromConfigFile, this));
    QObject::connect(ui()->selectionComboBox, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), bind(&ConnectionOptionPage::showConnectionSettings, this, _1));
    QObject::connect(ui()->selectionComboBox, static_cast<void(QComboBox::*)(const QString &)>(&QComboBox::editTextChanged), bind(&ConnectionOptionPage::saveCurrentConnectionName, this, _1));
    QObject::connect(ui()->addPushButton, &QPushButton::clicked, bind(&ConnectionOptionPage::addConnectionSettings, this));
    QObject::connect(ui()->removePushButton, &QPushButton::clicked, bind(&ConnectionOptionPage::removeConnectionSettings, this));
    return w;
}

void ConnectionOptionPage::insertFromConfigFile()
{
    if(hasBeenShown()) {
        QString configFile = SyncthingConfig::locateConfigFile();
        if(configFile.isEmpty()) {
            // allow user to select config file manually if it could not be located
            configFile = QFileDialog::getOpenFileName(widget(), QCoreApplication::translate("QtGui::ConnectionOptionPage", "Select Syncthing config file") + QStringLiteral(" - " APP_NAME));
        }
        if(configFile.isEmpty()) {
            return;
        }
        SyncthingConfig config;
        if(!config.restore(configFile)) {
            QMessageBox::critical(widget(), widget()->windowTitle() + QStringLiteral(" - " APP_NAME), QCoreApplication::translate("QtGui::ConnectionOptionPage", "Unable to parse the Syncthing config file."));
            return;
        }
        if(!config.guiAddress.isEmpty()) {
            ui()->urlLineEdit->selectAll();
            ui()->urlLineEdit->insert(((config.guiEnforcesSecureConnection || !QHostAddress(config.guiAddress.mid(0, config.guiAddress.indexOf(QChar(':')))).isLoopback()) ? QStringLiteral("https://") : QStringLiteral("http://")) + config.guiAddress);
        }
        if(!config.guiUser.isEmpty() || !config.guiPasswordHash.isEmpty()) {
            ui()->authCheckBox->setChecked(true);
            ui()->userNameLineEdit->selectAll();
            ui()->userNameLineEdit->insert(config.guiUser);
        } else {
            ui()->authCheckBox->setChecked(false);
        }
        if(!config.guiApiKey.isEmpty()) {
            ui()->apiKeyLineEdit->selectAll();
            ui()->apiKeyLineEdit->insert(config.guiApiKey);
        }
    }
}

void ConnectionOptionPage::updateConnectionStatus()
{
    if(hasBeenShown()) {
        ui()->statusLabel->setText(m_connection->statusText());
    }
}

bool ConnectionOptionPage::showConnectionSettings(int index)
{
    bool ok = true;
    if(index != m_currentIndex) {
        if((ok = cacheCurrentSettings(false))) {
            const SyncthingConnectionSettings &connectionSettings = (index == 0 ? m_primarySettings : m_secondarySettings[static_cast<size_t>(index - 1)]);
            ui()->urlLineEdit->setText(connectionSettings.syncthingUrl);
            ui()->authCheckBox->setChecked(connectionSettings.authEnabled);
            ui()->userNameLineEdit->setText(connectionSettings.userName);
            ui()->passwordLineEdit->setText(connectionSettings.password);
            ui()->apiKeyLineEdit->setText(connectionSettings.apiKey);
            ui()->certPathSelection->lineEdit()->setText(connectionSettings.httpsCertPath);
            ui()->pollTrafficSpinBox->setValue(connectionSettings.trafficPollInterval);
            ui()->pollDevStatsSpinBox->setValue(connectionSettings.devStatsPollInterval);
            ui()->reconnectSpinBox->setValue(connectionSettings.reconnectInterval);
            m_currentIndex = index;
        } else {
            ui()->selectionComboBox->setCurrentIndex(m_currentIndex);
        }

    }
    ui()->removePushButton->setEnabled(index);
    return ok;
}

bool ConnectionOptionPage::cacheCurrentSettings(bool applying)
{
    bool ok = true;
    if(m_currentIndex >= 0) {
        SyncthingConnectionSettings &connectionSettings = (m_currentIndex == 0 ? m_primarySettings : m_secondarySettings[static_cast<size_t>(m_currentIndex - 1)]);
        connectionSettings.syncthingUrl = ui()->urlLineEdit->text();
        connectionSettings.authEnabled = ui()->authCheckBox->isChecked();
        connectionSettings.userName = ui()->userNameLineEdit->text();
        connectionSettings.password = ui()->passwordLineEdit->text();
        connectionSettings.apiKey = ui()->apiKeyLineEdit->text().toUtf8();
        connectionSettings.expectedSslErrors.clear();
        connectionSettings.httpsCertPath = ui()->certPathSelection->lineEdit()->text();
        connectionSettings.trafficPollInterval = ui()->pollTrafficSpinBox->value();
        connectionSettings.devStatsPollInterval = ui()->pollDevStatsSpinBox->value();
        connectionSettings.reconnectInterval = ui()->reconnectSpinBox->value();
        if(!connectionSettings.loadHttpsCert()) {
            const QString errorMessage = QCoreApplication::translate("QtGui::ConnectionOptionPage", "Unable to load specified certificate \"%1\".").arg(connectionSettings.httpsCertPath);
            if(!applying) {
                QMessageBox::critical(widget(), QCoreApplication::applicationName(), errorMessage);
            } else {
                errors() << errorMessage;
            }
            ok = false;
        }
    }
    return ok;
}

void ConnectionOptionPage::saveCurrentConnectionName(const QString &name)
{
    const int index = ui()->selectionComboBox->currentIndex();
    if(index == m_currentIndex && index >= 0) {
        (index == 0 ? m_primarySettings : m_secondarySettings[static_cast<size_t>(index - 1)]).label = name;
        ui()->selectionComboBox->setItemText(index, name);
    }
}

void ConnectionOptionPage::addConnectionSettings()
{
    m_secondarySettings.emplace_back();
    m_secondarySettings.back().label = QCoreApplication::translate("QtGui::ConnectionOptionPage", "Instance %1").arg(ui()->selectionComboBox->count() + 1);
    ui()->selectionComboBox->addItem(m_secondarySettings.back().label);
    ui()->selectionComboBox->setCurrentIndex(ui()->selectionComboBox->count() - 1);
}

void ConnectionOptionPage::removeConnectionSettings()
{
    int index = ui()->selectionComboBox->currentIndex();
    if(index > 0) {
        m_secondarySettings.erase(m_secondarySettings.begin() + (index - 1));
        m_currentIndex = -1;
        ui()->selectionComboBox->removeItem(index);
    }
}

bool ConnectionOptionPage::apply()
{
    bool ok = true;
    if(hasBeenShown()) {
        ok = cacheCurrentSettings(true);
        Settings::primaryConnectionSettings() = m_primarySettings;
        Settings::secondaryConnectionSettings() = m_secondarySettings;
    }
    return ok;
}

void ConnectionOptionPage::reset()
{
    if(hasBeenShown()) {
        m_primarySettings = primaryConnectionSettings();
        m_secondarySettings = secondaryConnectionSettings();
        m_currentIndex = -1;

        QStringList itemTexts;
        itemTexts.reserve(1 + static_cast<int>(m_secondarySettings.size()));
        itemTexts << m_primarySettings.label;
        for(const SyncthingConnectionSettings &settings : m_secondarySettings) {
            itemTexts << settings.label;
        }
        ui()->selectionComboBox->clear();
        ui()->selectionComboBox->addItems(itemTexts);
        ui()->selectionComboBox->setCurrentIndex(0);

        updateConnectionStatus();
    }
}

void ConnectionOptionPage::applyAndReconnect()
{
    apply();
    m_connection->reconnect((m_currentIndex == 0 ? m_primarySettings : m_secondarySettings[static_cast<size_t>(m_currentIndex - 1)]));
}

// NotificationsOptionPage
NotificationsOptionPage::NotificationsOptionPage(QWidget *parentWidget) :
    NotificationsOptionPageBase(parentWidget)
{}

NotificationsOptionPage::~NotificationsOptionPage()
{}

bool NotificationsOptionPage::apply()
{
    if(hasBeenShown()) {
        notifyOnDisconnect() = ui()->notifyOnDisconnectCheckBox->isChecked();
        notifyOnInternalErrors() = ui()->notifyOnErrorsCheckBox->isChecked();
        notifyOnSyncComplete() = ui()->notifyOnSyncCompleteCheckBox->isChecked();
        showSyncthingNotifications() = ui()->showSyncthingNotificationsCheckBox->isChecked();
    }
    return true;
}

void NotificationsOptionPage::reset()
{
    if(hasBeenShown()) {
        ui()->notifyOnDisconnectCheckBox->setChecked(notifyOnDisconnect());
        ui()->notifyOnErrorsCheckBox->setChecked(notifyOnInternalErrors());
        ui()->notifyOnSyncCompleteCheckBox->setChecked(notifyOnSyncComplete());
        ui()->showSyncthingNotificationsCheckBox->setChecked(showSyncthingNotifications());
    }
}

// AppearanceOptionPage
AppearanceOptionPage::AppearanceOptionPage(QWidget *parentWidget) :
    AppearanceOptionPageBase(parentWidget)
{}

AppearanceOptionPage::~AppearanceOptionPage()
{}

bool AppearanceOptionPage::apply()
{
    if(hasBeenShown()) {
        trayMenuSize().setWidth(ui()->widthSpinBox->value());
        trayMenuSize().setHeight(ui()->heightSpinBox->value());
        showTraffic() = ui()->showTrafficCheckBox->isChecked();
        int style;
        switch(ui()->frameShapeComboBox->currentIndex()) {
        case 0: style = QFrame::NoFrame; break;
        case 1: style = QFrame::Box; break;
        case 2: style = QFrame::Panel; break;
        default: style = QFrame::StyledPanel;
        }
        switch(ui()->frameShadowComboBox->currentIndex()) {
        case 0: style |= QFrame::Plain; break;
        case 1: style |= QFrame::Raised; break;
        default: style |= QFrame::Sunken;
        }
        frameStyle() = style;
        tabPosition() = ui()->tabPosComboBox->currentIndex();
    }
    return true;
}

void AppearanceOptionPage::reset()
{
    if(hasBeenShown()) {
        ui()->widthSpinBox->setValue(trayMenuSize().width());
        ui()->heightSpinBox->setValue(trayMenuSize().height());
        ui()->showTrafficCheckBox->setChecked(showTraffic());
        int index;
        switch(frameStyle() & QFrame::Shape_Mask) {
        case QFrame::NoFrame: index = 0; break;
        case QFrame::Box: index = 1; break;
        case QFrame::Panel: index = 2; break;
        default: index = 3;
        }
        ui()->frameShapeComboBox->setCurrentIndex(index);
        switch(frameStyle() & QFrame::Shadow_Mask) {
        case QFrame::Plain: index = 0; break;
        case QFrame::Raised: index = 1; break;
        default: index = 2;
        }
        ui()->frameShadowComboBox->setCurrentIndex(index);
        ui()->tabPosComboBox->setCurrentIndex(tabPosition());
    }
}

// AutostartOptionPage
AutostartOptionPage::AutostartOptionPage(QWidget *parentWidget) :
    AutostartOptionPageBase(parentWidget)
{}

AutostartOptionPage::~AutostartOptionPage()
{}

QWidget *AutostartOptionPage::setupWidget()
{
    auto *widget = AutostartOptionPageBase::setupWidget();
    ui()->infoIconLabel->setPixmap(QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation, nullptr, ui()->infoIconLabel).pixmap(ui()->infoIconLabel->size()));
#if defined(PLATFORM_LINUX) && !defined(Q_OS_ANDROID)
    ui()->platformNoteLabel->setText(QCoreApplication::translate("QtGui::AutostartOptionPage", "This is achieved by adding a *.desktop file under <i>~/.config/autostart</i> so the setting only affects the current user."));
#elif defined(PLATFORM_WINDOWS)
    ui()->platformNoteLabel->setText(QCoreApplication::translate("QtGui::AutostartOptionPage", "This is achieved by adding a registry key under <i>HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run</i> so the setting only affects the current user. Note that the startup entry is invalidated when moving <i>syncthingtray.exe</i>."));
#else
    ui()->platformNoteLabel->setText(QCoreApplication::translate("QtGui::AutostartOptionPage", "This feature has not been implemented for your platform (yet)."));
    ui()->autostartCheckBox->setEnabled(false);
#endif
    return widget;
}

/*!
 * \brief Returns whether the application is launched on startup.
 * \remarks
 * - Only implemented under Linux/Windows. Always returns false on other platforms.
 * - Does not check whether the startup entry is functional (eg. the specified path is still valid).
 * -
 */
bool isAutostartEnabled()
{
#if defined(PLATFORM_LINUX) && !defined(Q_OS_ANDROID)
    QFile desktopFile(QStandardPaths::locate(QStandardPaths::ConfigLocation, QStringLiteral("autostart/" PROJECT_NAME ".desktop")));
    // check whether the file can be opeed and whether it is enabled but prevent reading large files
    if(desktopFile.open(QFile::ReadOnly) && (desktopFile.size() > (5 * 1024) || !desktopFile.readAll().contains("Hidden=true"))) {
        return true;
    }
    return false;
#elif defined(PLATFORM_WINDOWS)
    QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    return settings.contains(QStringLiteral(PROJECT_NAME));
#else
    return false;
#endif
}

/*!
 * \brief Sets whether the application is launchedc on startup.
 * \remarks
 * - Only implemented under Linux/Windows. Does nothing on other platforms.
 * - If a startup entry already exists and \a enabled is true, this function will ensure the path of the existing entry is valid.
 * - If no startup entry could be detected via isAutostartEnabled() and \a enabled is false this function doesn't touch anything.
 */
bool setAutostartEnabled(bool enabled)
{
    if(!isAutostartEnabled() && !enabled) {
        return true;
    }
#if defined(PLATFORM_LINUX) && !defined(Q_OS_ANDROID)
    const QString configPath(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation));
    if(configPath.isEmpty()) {
        return !enabled;
    }
    if(enabled && !QDir().mkpath(configPath + QStringLiteral("/autostart"))) {
        return false;
    }
    QFile desktopFile(configPath + QStringLiteral("/autostart/" PROJECT_NAME ".desktop"));
    if(enabled) {
        if(desktopFile.open(QFile::WriteOnly | QFile::Truncate)) {
            desktopFile.write("[Desktop Entry]\n");
            desktopFile.write("Name=" APP_NAME "\n");
            desktopFile.write("Exec=");
            desktopFile.write(QCoreApplication::applicationFilePath().toLocal8Bit().data());
            desktopFile.write("\nComment=" APP_DESCRIPTION "\n");
            desktopFile.write("Icon=" PROJECT_NAME "\n");
            desktopFile.write("Type=Application\n");
            desktopFile.write("Terminal=false\n");
            desktopFile.write("X-GNOME-Autostart-Delay=0\n");
            desktopFile.write("X-GNOME-Autostart-enabled=true");
            return desktopFile.error() == QFile::NoError && desktopFile.flush();
        }
        return false;
    } else {
        return !desktopFile.exists() || desktopFile.remove();
    }

#elif defined(PLATFORM_WINDOWS)
    QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    if(enabled) {
        settings.setValue(QStringLiteral(PROJECT_NAME), QCoreApplication::applicationFilePath().replace(QChar('/'), QChar('\\')));
    } else {
        settings.remove(QStringLiteral(PROJECT_NAME));
    }
    settings.sync();
    return true;
#endif
}

bool AutostartOptionPage::apply()
{
    bool ok = true;
    if(hasBeenShown()) {
        if(!setAutostartEnabled(ui()->autostartCheckBox->isChecked())) {
            errors() << QCoreApplication::translate("QtGui::AutostartOptionPage", "unable to modify startup entry");
            ok = false;
        }
    }
    return ok;
}

void AutostartOptionPage::reset()
{
    if(hasBeenShown()) {
        ui()->autostartCheckBox->setChecked(isAutostartEnabled());
    }
}

// LauncherOptionPage
LauncherOptionPage::LauncherOptionPage(QWidget *parentWidget) :
    LauncherOptionPageBase(parentWidget),
    m_kill(false)
{}

LauncherOptionPage::~LauncherOptionPage()
{
    for(const QMetaObject::Connection &connection : m_connections) {
        QObject::disconnect(connection);
    }
}

QWidget *LauncherOptionPage::setupWidget()
{
    auto *widget = LauncherOptionPageBase::setupWidget();
    ui()->syncthingPathSelection->provideCustomFileMode(QFileDialog::ExistingFile);
    ui()->logTextEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_connections << QObject::connect(&syncthingProcess(), &SyncthingProcess::readyRead, bind(&LauncherOptionPage::handleSyncthingReadyRead, this));
    m_connections << QObject::connect(&syncthingProcess(), static_cast<void(SyncthingProcess::*)(int exitCode, QProcess::ExitStatus exitStatus)>(&SyncthingProcess::finished), bind(&LauncherOptionPage::handleSyncthingExited, this, _1, _2));
    QObject::connect(ui()->launchNowPushButton, &QPushButton::clicked, bind(&LauncherOptionPage::launch, this));
    QObject::connect(ui()->stopPushButton, &QPushButton::clicked, bind(&LauncherOptionPage::stop, this));
    const bool running = syncthingProcess().state() != QProcess::NotRunning;
    ui()->launchNowPushButton->setHidden(running);
    ui()->stopPushButton->setHidden(!running);
    return widget;
}

bool LauncherOptionPage::apply()
{
    if(hasBeenShown()) {
        Settings::launchSynchting() = ui()->enabledCheckBox->isChecked(),
        Settings::syncthingPath() = ui()->syncthingPathSelection->lineEdit()->text(),
        Settings::syncthingArgs() = ui()->argumentsLineEdit->text();
    }
    return true;
}

void LauncherOptionPage::reset()
{
    if(hasBeenShown()) {
        ui()->enabledCheckBox->setChecked(Settings::launchSynchting());
        ui()->syncthingPathSelection->lineEdit()->setText(Settings::syncthingPath());
        ui()->argumentsLineEdit->setText(Settings::syncthingArgs());
    }
}

void LauncherOptionPage::handleSyncthingReadyRead()
{
    if(hasBeenShown()) {
        QTextCursor cursor = ui()->logTextEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QString::fromLocal8Bit(syncthingProcess().readAll()));
        if(ui()->ensureCursorVisibleCheckBox->isChecked()) {
            ui()->logTextEdit->ensureCursorVisible();
        }
    }
}

void LauncherOptionPage::handleSyncthingExited(int exitCode, QProcess::ExitStatus exitStatus)
{
    if(hasBeenShown()) {
        QTextCursor cursor = ui()->logTextEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        switch(exitStatus) {
        case QProcess::NormalExit:
            cursor.insertText(QCoreApplication::translate("QtGui::LauncherOptionPage", "Syncthing existed with exit code %1\n").arg(exitCode));
            break;
        case QProcess::CrashExit:
            cursor.insertText(QCoreApplication::translate("QtGui::LauncherOptionPage", "Syncthing crashed with exit code %1\n").arg(exitCode));
            break;
        }
        ui()->stopPushButton->hide();
        ui()->launchNowPushButton->show();
    }
}

void LauncherOptionPage::launch()
{
    if(hasBeenShown()) {
        apply();
        if(syncthingProcess().state() == QProcess::NotRunning) {
            ui()->launchNowPushButton->hide();
            ui()->stopPushButton->show();
            m_kill = false;
            syncthingProcess().startSyncthing(Settings::syncthingCmd());
        }
    }
}

void LauncherOptionPage::stop()
{
    if(hasBeenShown()) {
        if(syncthingProcess().state() != QProcess::NotRunning) {
            if(m_kill) {
                syncthingProcess().kill();
            } else {
                m_kill = true;
                syncthingProcess().terminate();
            }
        }
    }
}

// WebViewOptionPage
WebViewOptionPage::WebViewOptionPage(QWidget *parentWidget) :
    WebViewOptionPageBase(parentWidget)
{}

WebViewOptionPage::~WebViewOptionPage()
{}

#ifdef SYNCTHINGTRAY_NO_WEBVIEW
QWidget *WebViewOptionPage::setupWidget()
{
    auto *label = new QLabel;
    label->setWindowTitle(QCoreApplication::translate("QtGui::WebViewOptionPage", "General"));
    label->setAlignment(Qt::AlignCenter);
    label->setText(QCoreApplication::translate("QtGui::WebViewOptionPage", "Syncthing Tray has not been built with vieb view support utilizing either Qt WebKit or Qt WebEngine.\nThe Web UI will be opened in the default web browser instead."));
    return label;
}
#endif

bool WebViewOptionPage::apply()
{
#ifndef SYNCTHINGTRAY_NO_WEBVIEW
    if(hasBeenShown()) {
        webViewDisabled() = ui()->disableCheckBox->isChecked();
        webViewZoomFactor() = ui()->zoomDoubleSpinBox->value();
        webViewKeepRunning() = ui()->keepRunningCheckBox->isChecked();
    }
#endif
    return true;
}

void WebViewOptionPage::reset()
{
#ifndef SYNCTHINGTRAY_NO_WEBVIEW
    if(hasBeenShown()) {
        ui()->disableCheckBox->setChecked(webViewDisabled());
        ui()->zoomDoubleSpinBox->setValue(webViewZoomFactor());
        ui()->keepRunningCheckBox->setChecked(webViewKeepRunning());
    }
#endif
}

SettingsDialog::SettingsDialog(Data::SyncthingConnection *connection, QWidget *parent) :
    Dialogs::SettingsDialog(parent)
{
    // setup categories
    QList<Dialogs::OptionCategory *> categories;
    Dialogs::OptionCategory *category;

    category = new OptionCategory(this);
    category->setDisplayName(tr("Tray"));
    category->assignPages(QList<Dialogs::OptionPage *>() << new ConnectionOptionPage(connection) << new NotificationsOptionPage << new AppearanceOptionPage);
    category->setIcon(QIcon(QStringLiteral(":/icons/hicolor/scalable/app/syncthingtray.svg")));
    categories << category;

    category = new OptionCategory(this);
    category->setDisplayName(tr("Web view"));
    category->assignPages(QList<Dialogs::OptionPage *>() << new WebViewOptionPage);
    category->setIcon(QIcon::fromTheme(QStringLiteral("internet-web-browser"), QIcon(QStringLiteral(":/icons/hicolor/scalable/apps/internet-web-browser.svg"))));
    categories << category;

    category = new OptionCategory(this);
    category->setDisplayName(tr("Startup"));
    category->assignPages(QList<Dialogs::OptionPage *>() << new AutostartOptionPage << new LauncherOptionPage);
    category->setIcon(QIcon::fromTheme(QStringLiteral("system-run"), QIcon(QStringLiteral(":/icons/hicolor/scalable/apps/system-run.svg"))));
    categories << category;

    categories << Settings::qtSettings().category();

    categoryModel()->setCategories(categories);

    setMinimumSize(800, 550);
    setWindowTitle(tr("Settings") + QStringLiteral(" - " APP_NAME));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("preferences-other"), QIcon(QStringLiteral(":/icons/hicolor/scalable/apps/preferences-other.svg"))));

    // some settings could be applied without restarting the application, good idea?
    //connect(this, &Dialogs::SettingsDialog::applied, bind(&Dialogs::QtSettings::apply, &Settings::qtSettings()));
}

SettingsDialog::~SettingsDialog()
{}

}

INSTANTIATE_UI_FILE_BASED_OPTION_PAGE_NS(QtGui, ConnectionOptionPage)
INSTANTIATE_UI_FILE_BASED_OPTION_PAGE_NS(QtGui, NotificationsOptionPage)
INSTANTIATE_UI_FILE_BASED_OPTION_PAGE_NS(QtGui, AppearanceOptionPage)
INSTANTIATE_UI_FILE_BASED_OPTION_PAGE_NS(QtGui, AutostartOptionPage)
INSTANTIATE_UI_FILE_BASED_OPTION_PAGE_NS(QtGui, LauncherOptionPage)
#ifndef SYNCTHINGTRAY_NO_WEBVIEW
INSTANTIATE_UI_FILE_BASED_OPTION_PAGE_NS(QtGui, WebViewOptionPage)
#endif