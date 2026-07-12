#include <QApplication>
#include <QDebug>

#include "app/StyleLoader.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 为后续配置、日志和系统级 Qt 服务提供稳定的应用与组织标识。
    QApplication::setApplicationName(QStringLiteral("RtmpMonitor"));
    QApplication::setOrganizationName(QStringLiteral("RtmpProject"));

    // 启动阶段仅加载一次全局 QSS；外部样式缺失时由 StyleLoader 自动回退到内置资源。
    const StyleLoadResult styleResult = StyleLoader::instance().applyApplicationStyle(app);
    if (!styleResult.applied) {
        qWarning().noquote()
            << QStringLiteral("启动时未能应用默认 QSS：%1").arg(styleResult.errorMessage);
    }

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
