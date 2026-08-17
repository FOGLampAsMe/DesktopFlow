#include "App.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("DesktopFlow"));
    application.setApplicationDisplayName(QStringLiteral("DesktopFlow"));
    application.setOrganizationName(QStringLiteral("DesktopFlow"));
    application.setStyle(QStringLiteral("Fusion"));

    desktopflow::DesktopFlowApp window;
    window.show();
    return application.exec();
}
