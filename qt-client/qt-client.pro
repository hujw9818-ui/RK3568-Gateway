# ======================================================================
# qt-client.pro - 边缘网关 Qt 上位机 (参照 Web 端功能)
# 功能: 传感器显示 / 外设控制 / 视频 / 日志 / MQTT-Zigbee切换 / WS实时推送
# ======================================================================
QT += core gui widgets network

TARGET = qt-client
TEMPLATE = app

CONFIG += c++14

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h
