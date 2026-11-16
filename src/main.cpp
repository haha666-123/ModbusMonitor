#include "common/Console.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QLoggingCategory>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    mb::initConsoleUtf8();          // 控制台中文乱码修复

    // Qt 的 HTTP/2 实现会把 401/407 这类业务错误包成
    //   qt.network.http2: stream N finished with error: "Host requires authentication"
    // 这种噪音日志，而且拿不到真实状态码。
    // 项目里已强制使用 HTTP/1.1 并自行翻译错误，这里关掉该分类避免干扰判断。
    QLoggingCategory::setFilterRules(QStringLiteral("qt.network.http2=false"));

    QApplication app(argc, argv);

    // QStandardPaths(AppLocalDataLocation) 依赖这两个属性，用于定位数据库目录
    QApplication::setOrganizationName(QStringLiteral("SongGuohui"));
    QApplication::setApplicationName(QStringLiteral("ModbusMonitor"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    mb::MainWindow window;
    window.show();

    const int rc = app.exec();
    mb::restoreConsole();
    return rc;
}
