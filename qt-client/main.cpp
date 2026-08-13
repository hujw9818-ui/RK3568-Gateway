// ======================================================================
// main.cpp - Qt 上位机入口
//
// 用法:
//   板子上: ./qt-client              (默认连 127.0.0.1)
//   PC调试: ./qt-client 192.168.5.80 (连板子网线IP)
// ======================================================================
#include <QApplication>
#include <QString>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("RK3568 网关上位机");

    // 服务器地址: 命令行参数1, 默认 127.0.0.1 (板子自连)
    const QString server = (argc > 1) ? QString(argv[1]) : "127.0.0.1";

    MainWindow w(server);
    w.showFullScreen();   // 全屏覆盖桌面, 视觉上只剩 Qt 界面

    return app.exec();
}
