#include "AppController.h"
#include "InstanceLock.h"
#include "Utils.h"
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
int main(int argc, char *argv[])
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--data-dir") Utils::setDataDir(argv[i + 1]);
    QGuiApplication app(argc, argv);
    app.setOrganizationName("SecureVault"); app.setApplicationName("SecureVault");   // for UI-only QSettings
    app.setWindowIcon(QIcon(":/assets/icon/securevault.png"));
    for (const char *f : {"Inter-Regular", "Inter-Medium", "Inter-SemiBold", "JetBrainsMono-Regular", "JetBrainsMono-Medium"})
        QFontDatabase::addApplicationFont(QString(":/assets/fonts/%1.ttf").arg(f));   // bundled: no web fonts, no network
    QQuickStyle::setStyle("Basic");
    QQmlApplicationEngine engine;
    InstanceLock instanceLock;   // one writer per data folder (P1-9); shared with the CLI
    if (!instanceLock.tryAcquire()) {
        engine.loadData(R"(import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow { visible: true; width: 460; height: 160; color: "#0B0F14"; title: "SecureVault"
    Text { anchors.centerIn: parent; width: parent.width - 48; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: "#F1F5F9"; font.pixelSize: 14
        text: "SecureVault is already open for this data folder." } })");
        return engine.rootObjects().isEmpty() ? 1 : app.exec();
    }
    AppController controller;
    app.installEventFilter(&controller); // activity = input inside the app window
    engine.rootContext()->setContextProperty("app", &controller);
    engine.loadFromModule("SecureVault.Gui", "Main");
    return engine.rootObjects().isEmpty() ? 1 : app.exec();
}
