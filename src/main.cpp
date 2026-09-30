#include "MainWindow.h"
#include "AppStyle.h"
#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>

int main(int argc, char** argv)
{
    QSurfaceFormat format;
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    app.setStyle(new AppStyle);
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/assets/logo.png")));
    MainWindow window;
    window.show();
    return app.exec();
}
