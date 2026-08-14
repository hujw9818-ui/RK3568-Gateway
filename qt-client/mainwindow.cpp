// ======================================================================
// mainwindow.cpp - 主窗口实现 (参照 Web 端样式)
// ======================================================================
#include "mainwindow.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QTransform>
#include <QLabel>
#include <QListWidget>
#include <QNetworkReply>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(const QString& server, QWidget* parent)
    : QMainWindow(parent), server_(server) {
    apiBase_ = QString("http://%1:8080").arg(server_);
    mgr_ = new QNetworkAccessManager(this);

    buildUi();

    // 视频轮询 (last_photo, 1 秒)
    snapshotTimer_ = new QTimer(this);
    connect(snapshotTimer_, &QTimer::timeout,
            this, &MainWindow::refreshSnapshot);
    snapshotTimer_->start(1000);

    // 状态轮询 (1 秒, 无 WebSocket 模块, 用轮询实现同步)
    statusTimer_ = new QTimer(this);
    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::refreshStatus);
    statusTimer_->start(1000);

    setWindowTitle("RK3568 智能网关控制系统");
    resize(1000, 640);

    addLog("程序启动, 连接服务器 " + server_);
}

// ------------------------------------------------------------------
// 界面构建 (参照 Web 端布局)
// ------------------------------------------------------------------
void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    // ---------- 顶部 Header (紫色渐变) ----------
    auto* header = new QFrame;
    header->setObjectName("header");
    auto* hlay = new QHBoxLayout(header);
    auto* title = new QLabel("RK3568 智能网关控制系统");
    title->setObjectName("headerTitle");
    connLabel_ = new QLabel("连接中...");
    connLabel_->setObjectName("connStatus");
    btnMqtt_ = new QPushButton("MQTT");
    btnZigbee_ = new QPushButton("Zigbee");
    btnMqtt_->setObjectName("linkBtnActive");
    connect(btnMqtt_, &QPushButton::clicked, [this]() { sendTransport("mqtt"); });
    connect(btnZigbee_, &QPushButton::clicked, [this]() { sendTransport("zigbee"); });
    hlay->addWidget(title);
    hlay->addStretch();
    hlay->addWidget(btnMqtt_);
    hlay->addWidget(btnZigbee_);
    hlay->addWidget(connLabel_);
    root->addWidget(header);

    // ---------- 主体: 左(视频+传感器) 右(控制+日志) ----------
    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    root->addLayout(body, 1);

    // ===== 左侧 =====
    auto* left = new QVBoxLayout;
    left->setSpacing(12);
    body->addLayout(left, 3);

    // 视频区
    auto* videoCard = new QFrame;
    videoCard->setObjectName("card");
    auto* vlay = new QVBoxLayout(videoCard);
    auto* vtitle = new QLabel("实时画面监控");
    vtitle->setObjectName("cardTitle");
    videoLabel_ = new QLabel("视频未连接");
    videoLabel_->setObjectName("videoBox");
    videoLabel_->setAlignment(Qt::AlignCenter);
    videoLabel_->setMinimumHeight(300);
    vlay->addWidget(vtitle);
    vlay->addWidget(videoLabel_, 1);
    auto* vbar = new QHBoxLayout;
    btnStreamStart_ = new QPushButton("开始推流");
    btnStreamStop_ = new QPushButton("停止推流");
    btnSnapshot_ = new QPushButton("抓拍");
    btnRecord_ = new QPushButton("录像");
    vbar->addWidget(btnStreamStart_);
    vbar->addWidget(btnStreamStop_);
    vbar->addWidget(btnSnapshot_);
    vbar->addWidget(btnRecord_);
    vlay->addLayout(vbar);
    connect(btnStreamStart_, &QPushButton::clicked,
            this, &MainWindow::onStreamStart);
    connect(btnStreamStop_, &QPushButton::clicked,
            this, &MainWindow::onStreamStop);
    connect(btnSnapshot_, &QPushButton::clicked,
            this, &MainWindow::onSnapshot);
    connect(btnRecord_, &QPushButton::clicked,
            this, &MainWindow::onRecord);
    left->addWidget(videoCard, 3);

    // 传感器卡片 (4 个横排)
    auto* sensRow = new QHBoxLayout;
    sensRow->setSpacing(12);
    auto makeSensor = [&](const QString& name, const QString& unit) {
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* l = new QVBoxLayout(card);
        l->setAlignment(Qt::AlignCenter);
        auto* n = new QLabel(name);
        n->setObjectName("sensorName");
        auto* v = new QLabel("--");
        v->setObjectName("sensorValue");
        auto* u = new QLabel(unit);
        u->setObjectName("sensorUnit");
        l->addWidget(n);
        l->addWidget(v);
        l->addWidget(u);
        sensRow->addWidget(card);
        return v;
    };
    valTemp_ = makeSensor("温度", "°C");
    valHumi_ = makeSensor("湿度", "% RH");
    valLight_ = makeSensor("光照强度", "Lux");
    valIr_ = makeSensor("红外检测", "Status");
    left->addLayout(sensRow);

    // ===== 右侧 =====
    auto* right = new QVBoxLayout;
    right->setSpacing(12);
    body->addLayout(right, 2);

    // 控制卡片
    auto* ctrlCard = new QFrame;
    ctrlCard->setObjectName("card");
    auto* clay = new QVBoxLayout(ctrlCard);
    auto* ctitle = new QLabel("硬件外设控制");
    ctitle->setObjectName("cardTitle");
    clay->addWidget(ctitle);

    // LED
    ledSw_ = new QCheckBox("LED 照明灯");
    ledBr_ = new QSlider(Qt::Horizontal);
    ledBr_->setRange(0, 100);
    ledBr_->setValue(50);
    connect(ledSw_, &QCheckBox::toggled, this, &MainWindow::sendLed);
    connect(ledBr_, &QSlider::valueChanged, this, &MainWindow::sendLed);
    clay->addWidget(ledSw_);
    clay->addWidget(ledBr_);

    // 电机
    motorSw_ = new QCheckBox("直流电机控制");
    motorSp_ = new QSlider(Qt::Horizontal);
    motorSp_->setRange(0, 100);
    motorSp_->setValue(30);
    dirF_ = new QPushButton("正向旋转");
    dirR_ = new QPushButton("反向旋转");
    dirF_->setObjectName("dirActive");
    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(dirF_);
    dirRow->addWidget(dirR_);
    connect(motorSw_, &QCheckBox::toggled, this, &MainWindow::sendMotor);
    connect(motorSp_, &QSlider::valueChanged, this, &MainWindow::sendMotor);
    connect(dirF_, &QPushButton::clicked, [this]() { sendMotorDir(false); });
    connect(dirR_, &QPushButton::clicked, [this]() { sendMotorDir(true); });
    clay->addWidget(motorSw_);
    clay->addWidget(motorSp_);
    clay->addLayout(dirRow);

    // 舵机
    auto* servoRow = new QHBoxLayout;
    auto* servoName = new QLabel("舵机角度");
    servoAg_ = new QSlider(Qt::Horizontal);
    servoAg_->setRange(0, 180);
    servoAg_->setValue(90);
    servoVal_ = new QLabel("90°");
    servoVal_->setObjectName("sensorValue");
    connect(servoAg_, &QSlider::valueChanged, [this](int v) {
        servoVal_->setText(QString::number(v) + "°");
    });
    connect(servoAg_, &QSlider::sliderReleased, this, &MainWindow::sendServo);
    servoRow->addWidget(servoName);
    servoRow->addWidget(servoAg_, 1);
    servoRow->addWidget(servoVal_);
    clay->addLayout(servoRow);

    // 蜂鸣器
    buzzerSw_ = new QCheckBox("紧急蜂鸣报警");
    connect(buzzerSw_, &QCheckBox::toggled, this, &MainWindow::sendBuzzer);
    clay->addWidget(buzzerSw_);

    right->addWidget(ctrlCard);

    // 日志卡片
    auto* logCard = new QFrame;
    logCard->setObjectName("card");
    auto* llay = new QVBoxLayout(logCard);
    auto* ltitle = new QLabel("系统运行日志");
    ltitle->setObjectName("cardTitle");
    logList_ = new QListWidget;
    logList_->setObjectName("logBox");
    llay->addWidget(ltitle);
    llay->addWidget(logList_, 1);
    right->addWidget(logCard, 1);

    // ---------- QSS 样式 (参照 Web 端紫色主题) ----------
    setStyleSheet(R"(
        QMainWindow { background: #f0f2f5; }
        QFrame#header {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #4f46e5, stop:1 #7c3aed);
            border-radius: 14px; padding: 14px 20px;
        }
        QLabel#headerTitle { color: white; font-size: 20px; font-weight: 800; }
        QLabel#connStatus { color: rgba(255,255,255,220); font-size: 13px;
            background: rgba(255,255,255,50); border-radius: 14px; padding: 4px 12px; }
        QFrame#card { background: white; border-radius: 14px;
            border: 1px solid #edf2f7; }
        QLabel#cardTitle { font-size: 15px; font-weight: 700; color: #1f2937;
            padding: 4px; }
        QLabel#videoBox { background: #0f172a; color: #94a3b8;
            border-radius: 10px; font-size: 14px; }
        QLabel#sensorName { color: #64748b; font-size: 13px; }
        QLabel#sensorValue { color: #5c67f2; font-size: 24px; font-weight: 800; }
        QLabel#sensorUnit { color: #94a3b8; font-size: 11px; }
        QCheckBox { font-weight: 600; padding: 4px; spacing: 8px; }
        QCheckBox::indicator {
            width: 20px; height: 20px;
            border: 2px solid #cbd5e1;
            border-radius: 4px;
            background: white;
        }
        QCheckBox::indicator:checked {
            background: #5c67f2;
            border-color: #5c67f2;
        }
        QSlider::groove:horizontal { height: 6px; background: #e2e8f0;
            border-radius: 3px; }
        QSlider::handle:horizontal { width: 18px; margin: -7px 0;
            background: #5c67f2; border-radius: 9px; }
        QPushButton { background: #e2e8f0; color: #64748b; border: none;
            border-radius: 8px; padding: 8px 14px; font-weight: 600; }
        QPushButton#dirActive { background: #5c67f2; color: white; }
        QPushButton#linkBtnActive { background: white; color: #4f46e5; }
        QListWidget#logBox { background: #0f172a; color: #38bdf8;
            border-radius: 10px; font-family: monospace; font-size: 12px; }
    )");
}

// ------------------------------------------------------------------
// 日志
// ------------------------------------------------------------------
void MainWindow::addLog(const QString& msg) {
    const QString line = QString("[%1] %2")
        .arg(QTime::currentTime().toString("HH:mm:ss"), msg);
    logList_->addItem(line);
    logList_->scrollToBottom();
    while (logList_->count() > 200) delete logList_->takeItem(0);
}

// ------------------------------------------------------------------
// 状态更新
// ------------------------------------------------------------------
void MainWindow::updateSensor(const QJsonObject& dev) {
    if (dev.contains("temp"))
        valTemp_->setText(QString::number(dev.value("temp").toDouble(), 'f', 1));
    if (dev.contains("humi"))
        valHumi_->setText(QString::number(dev.value("humi").toDouble(), 'f', 1));
    if (dev.contains("light"))
        valLight_->setText(QString::number(dev.value("light").toDouble(), 'f', 0));
    if (dev.contains("ir")) {
        const bool has = dev.value("ir").toInt() != 0;
        valIr_->setText(has ? "有人" : "安全");
    }
    // 执行器状态回显 (不打断用户操作: 仅在值不同时更新)
    if (dev.contains("led_on")) {
        const bool on = dev.value("led_on").toBool();
        if (ledSw_->isChecked() != on) {
            ledSw_->blockSignals(true);
            ledSw_->setChecked(on);
            ledSw_->blockSignals(false);
        }
    }
    if (dev.contains("servo_angle")) {
        const int a = dev.value("servo_angle").toInt();
        if (servoAg_->value() != a) {
            servoAg_->blockSignals(true);
            servoAg_->setValue(a);
            servoAg_->blockSignals(false);
            servoVal_->setText(QString::number(a) + "°");
        }
    }
}

// ------------------------------------------------------------------
// 控制命令 (格式和 Web 端一致)
// ------------------------------------------------------------------
QJsonObject MainWindow::buildCmd(const QString& target,
                                 const QJsonObject& params) {
    QJsonObject body;
    body["target"] = target;
    body["action"] = "set";
    body["params"] = params;

    QJsonObject cmd;
    cmd["type"] = "cmd";
    cmd["dev"] = "mcu01";
    cmd["msg_id"] = QString("qt-%1").arg(
        QDateTime::currentMSecsSinceEpoch());
    cmd["ts"] = QDateTime::currentSecsSinceEpoch();
    cmd["body"] = body;
    return cmd;
}

void MainWindow::postJson(const QString& path, const QJsonObject& body) {
    QNetworkRequest req(QUrl(apiBase_ + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = mgr_->post(
        req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply]() {
        reply->deleteLater();
    });
}

void MainWindow::sendLed() {
    QJsonObject p;
    p["on"] = ledSw_->isChecked() ? 1 : 0;
    p["brightness"] = ledBr_->value();
    postJson("/api/control", buildCmd("led", p));
    addLog(QString("LED: %1, 亮度 %2%")
        .arg(ledSw_->isChecked() ? "开" : "关").arg(ledBr_->value()));
}

void MainWindow::sendMotor() {
    QJsonObject p;
    p["on"] = motorSw_->isChecked() ? 1 : 0;
    p["speed"] = motorSp_->value();
    p["dir"] = motorDir_;
    postJson("/api/control", buildCmd("fan", p));
}

void MainWindow::sendMotorDir(bool reverse) {
    motorDir_ = reverse ? 1 : 0;
    dirF_->setObjectName(reverse ? "" : "dirActive");
    dirR_->setObjectName(reverse ? "dirActive" : "");
    // 重新应用样式
    style()->unpolish(dirF_); style()->polish(dirF_);
    style()->unpolish(dirR_); style()->polish(dirR_);
    sendMotor();
    addLog(QString("电机方向: %1").arg(reverse ? "反转" : "正转"));
}

void MainWindow::sendServo() {
    QJsonObject p;
    p["angle"] = servoAg_->value();
    postJson("/api/control", buildCmd("servo", p));
    addLog(QString("舵机: %1°").arg(servoAg_->value()));
}

void MainWindow::sendBuzzer() {
    QJsonObject p;
    p["on"] = buzzerSw_->isChecked() ? 1 : 0;
    postJson("/api/control", buildCmd("beep", p));
    addLog(QString("蜂鸣器: %1").arg(buzzerSw_->isChecked() ? "响" : "停"));
}

void MainWindow::sendTransport(const QString& transport) {
    QJsonObject body;
    body["transport"] = transport;
    postJson("/api/transport", body);
    btnMqtt_->setObjectName(transport == "mqtt" ? "linkBtnActive" : "");
    btnZigbee_->setObjectName(transport == "zigbee" ? "linkBtnActive" : "");
    style()->unpolish(btnMqtt_); style()->polish(btnMqtt_);
    style()->unpolish(btnZigbee_); style()->polish(btnZigbee_);
    addLog(QString("通讯方式切换: %1")
        .arg(transport == "mqtt" ? "MQTT (WiFi 直连)" : "Zigbee (DL-30)"));
}

// ------------------------------------------------------------------
// 视频 + 状态轮询
// ------------------------------------------------------------------
void MainWindow::refreshSnapshot() {
    QNetworkRequest req(QUrl(apiBase_ + QString("/api/camera/last_photo?t=%1")
        .arg(QDateTime::currentMSecsSinceEpoch())));
    QNetworkReply* reply = mgr_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QByteArray data = reply->readAll();
        if (data.size() < 100) return;
        QPixmap pm;
        if (pm.loadFromData(data)) {
            // 客户端旋转 -90° (服务端无旋转, 15fps 满帧)
            pm = pm.transformed(QTransform().rotate(-90));
            videoLabel_->setPixmap(pm.scaled(
                videoLabel_->size(), Qt::KeepAspectRatio,
                Qt::SmoothTransformation));
        }
    });
}

void MainWindow::refreshStatus() {
    QNetworkRequest req(QUrl(apiBase_ + "/api/status"));
    QNetworkReply* reply = mgr_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // 连接失败
            connLabel_->setText("连接断开");
            connLabel_->setStyleSheet("color: #fecaca; background: #7f1d1d;"
                                      "border-radius: 14px; padding: 4px 12px;");
            return;
        }
        // 连接成功
        connLabel_->setText("设备在线");
        connLabel_->setStyleSheet("color: #d1fae5; background: #065f46;"
                                  "border-radius: 14px; padding: 4px 12px;");
        const QJsonDocument doc =
            QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;
        const QJsonArray devices = doc.object().value("devices").toArray();
        for (const auto& d : devices) {
            const QJsonObject dev = d.toObject();
            if (dev.value("dev_id").toString() == "mcu01") {
                updateSensor(dev);
                break;
            }
        }
    });
}

// ------------------------------------------------------------------
// 视频控制
// ------------------------------------------------------------------
void MainWindow::onStreamStart() {
    // 连接 MJPEG multipart 流, 网关自动启动推流 (无客户端时自动停止)
    QNetworkRequest req(QUrl(apiBase_ + "/api/camera/stream"));
    streamReply_ = mgr_->get(req);
    connect(streamReply_, &QNetworkReply::readyRead,
            this, &MainWindow::onStreamData);
    snapshotTimer_->stop();   // 停轮询, 流接管
    addLog("已连接视频流 (MJPEG)");
}

void MainWindow::onStreamStop() {
    // 只断开自己的流连接, 网关检测到无客户端后自动停推流
    if (streamReply_) {
        streamReply_->disconnect();
        streamReply_->abort();
        streamReply_->deleteLater();
        streamReply_ = nullptr;
    }
    streamBuffer_.clear();
    snapshotTimer_->start();
    addLog("已断开视频流");
}

// MJPEG 流帧解析: SOI(FF D8) 帧开始, EOI(FF D9) 帧结束
// 只解析最新一帧 (丢弃中间帧, 避免事件堆积导致画面延迟)
void MainWindow::onStreamData() {
    streamBuffer_.append(streamReply_->readAll());

    int lastStart = -1, lastEnd = -1;
    int s = streamBuffer_.indexOf("\xff\xd8");
    while (s >= 0) {
        const int e = streamBuffer_.indexOf("\xff\xd9", s);
        if (e > s) {
            lastStart = s;
            lastEnd = e;
            s = e + 2;
        } else {
            break;
        }
    }
    if (lastStart >= 0 && lastEnd > lastStart) {
        const QByteArray jpeg =
            streamBuffer_.mid(lastStart, lastEnd - lastStart + 2);
        streamBuffer_.remove(0, lastEnd + 2);
        QImage img;
        if (img.loadFromData(jpeg)) {
            // 客户端旋转 -90° (服务端无旋转, 15fps 满帧)
            img = img.transformed(QTransform().rotate(-90));
            videoLabel_->setPixmap(QPixmap::fromImage(img).scaled(
                videoLabel_->size(), Qt::KeepAspectRatio,
                Qt::FastTransformation));
        }
    } else if (streamBuffer_.size() > 600000) {
        streamBuffer_.clear();
    }
}

void MainWindow::onSnapshot() {
    // 抓拍: 网关返回 base64 照片, 解析并显示
    QNetworkRequest req(QUrl(apiBase_ + "/api/camera/snapshot"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = mgr_->post(req, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            addLog("抓拍失败: " + reply->errorString());
            return;
        }
        const QJsonDocument doc =
            QJsonDocument::fromJson(reply->readAll());
        const QString b64 = doc.object().value("data").toString();
        if (b64.isEmpty()) {
            addLog("抓拍失败 (无数据)");
            return;
        }
        QImage img;
        if (!img.loadFromData(QByteArray::fromBase64(b64.toUtf8()))) {
            addLog("抓拍照片解析失败");
            return;
        }
        // 客户端旋转 -90° (与服务端无旋转推流一致)
        img = img.transformed(QTransform().rotate(-90));
        videoLabel_->setPixmap(QPixmap::fromImage(img).scaled(
            videoLabel_->size(), Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
        addLog("抓拍成功, 照片已显示");
    });
}

void MainWindow::onRecord() {
    QJsonObject body;
    if (!recording_) {
        postJson("/api/camera/record/start", body);
        btnRecord_->setText("停止录像");
        addLog("开始录像");
        recording_ = true;
    } else {
        postJson("/api/camera/record/stop", body);
        btnRecord_->setText("录像");
        addLog("录像已停止");
        recording_ = false;
    }
}
