// ======================================================================
// mainwindow.h - 主窗口声明
// ======================================================================
#pragma once

#include <QJsonObject>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QHash>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString& server, QWidget* parent = nullptr);

private slots:
    // 控制
    void sendLed();
    void sendMotor();
    void sendMotorDir(bool reverse);
    void sendServo();
    void sendBuzzer();
    void sendTransport(const QString& transport);

    // 数据
    void refreshSnapshot();
    void refreshStatus();

private:
    void buildUi();
    void addLog(const QString& msg);
    void updateSensor(const QJsonObject& dev);
    void postJson(const QString& path, const QJsonObject& body);
    QJsonObject buildCmd(const QString& target, const QJsonObject& params);
    void updateTransportUi(const QString& transport);   // MQTT/Zigbee 按钮高亮

    // 网络
    QString server_;
    QString apiBase_;          // http://host:8080
    QNetworkAccessManager* mgr_;
    QHash<QString, qint64> lastCmdTime_;   // 各目标最近命令时间 (回显防抖)

    // 定时器
    QTimer* snapshotTimer_;     // 视频轮询 (last_photo)
    QTimer* statusTimer_;       // 状态兜底轮询

    // 传感器显示
    QLabel* valTemp_;
    QLabel* valHumi_;
    QLabel* valLight_;
    QLabel* valIr_;

    // 控制控件
    QCheckBox* ledSw_;
    QSlider* ledBr_;
    QCheckBox* motorSw_;
    QSlider* motorSp_;
    QPushButton* dirF_;
    QPushButton* dirR_;
    int motorDir_ = 0;
    QSlider* servoAg_;
    QLabel* servoVal_;
    QCheckBox* buzzerSw_;

    // 视频 + 日志 + 状态
    QLabel* videoLabel_;
    QNetworkReply* streamReply_ = nullptr;   // multipart 流连接
    QByteArray streamBuffer_;                // 流缓冲 (解析 multipart 用)
    QPushButton* btnStreamStart_;
    QPushButton* btnStreamStop_;
    QPushButton* btnSnapshot_;
    QPushButton* btnRecord_;
    bool recording_ = false;
    QListWidget* logList_;
    QLabel* connLabel_;
    QPushButton* btnMqtt_;
    QPushButton* btnZigbee_;

private slots:
    void onStreamStart();
    void onStreamStop();
    void onStreamData();
    void onSnapshot();
    void onRecord();
};
