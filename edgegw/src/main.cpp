// ======================================================================
// edgegw - 网关主程序入口
//
// 启动流程:
//   1. 加载 YAML 配置 (ConfigManager)
//   2. 初始化日志系统 (Logger)
//   3. 初始化 SQLite (SqliteStore)
//   4. 初始化摄像头 (CameraManager)
//   5. 初始化 MQTT 客户端, 订阅 report/ack/status topic
//   6. 启动 Web 服务 (Mongoose REST API, :8080)
//   7. 启动 WebSocket 推送 (:8082)
//   8. 主循环: Poll Web/WS + 等待退出信号
//
// 数据处理:
//   MQTT 收到消息 → OnMqttMessage → HandleSensorMessage
//     → 设备状态表更新 + SQLite 历史存储 + WebSocket 广播
//
// 用法: ./edgegw config/development.yaml
// ======================================================================
#include "camera/camera_manager.hpp"
#include "config/config_manager.hpp"
#include "database/sqlite_store.hpp"
#include "device/device_registry.hpp"
#include "device/json_parser.hpp"
#include "device/sensor_data.hpp"
#include "logger/logger.hpp"
#include "mqtt/mqtt_client.hpp"
#include "web/web_server.hpp"
#include "web/websocket_server.hpp"

#include <csignal>
#include <string>

namespace {

volatile std::sig_atomic_t g_running = 1;

void OnSignal(int /*sig*/) { g_running = 0; }

// 全局状态 (模块间共享, 各自内部有锁保护)
edgegw::device::DeviceRegistry g_registry;   // 设备状态表(内存)
edgegw::database::SqliteStore g_db;          // 历史数据库(磁盘)
edgegw::mqtt::MqttClient g_mqtt;             // MQTT 客户端(Web要发命令)
edgegw::camera::CameraManager g_camera;      // 摄像头管理
edgegw::web::WebContext g_web_ctx;           // Web 上下文(依赖注入)
edgegw::web::WebServer g_web;                // Web 服务 (HTTP REST)
edgegw::web::WebSocketServer g_ws;           // WebSocket 推送

// 当前通讯方式: "mqtt" 或 "zigbee" (前端可切换)
std::string g_transport = "mqtt";

// ------------------------------------------------------------------
// 处理一条解析成功的设备消息
// 职责: 更新设备状态表 + 写入历史数据库 + WebSocket 推送
// ------------------------------------------------------------------
void HandleSensorMessage(const edgegw::device::SensorData& data) {
    // 1. 更新设备状态表 (最新值)
    g_registry.UpdateSensor(data);

    // 2. 写入历史数据库 (仅传感器数据, ACK/状态不落库)
    if (data.type == "sensor") {
        g_db.InsertTelemetry(data);
    }

    // 3. WebSocket 推送 (数据变化实时推给 Web/Qt 实现双端同步)
    g_ws.Broadcast(g_registry.ToJson());

    // 4. 日志 (关键事件用 Info, 数据明细用 Debug)
    edgegw::logger::Logger::Info("[mqtt] dev=" + data.dev_id +
                                 " type=" + data.type +
                                 " msg=" + data.msg_id +
                                 " ts=" + std::to_string(data.ts));
    if (data.type == "ack") {
        edgegw::logger::Logger::Info("    ack req=" + data.req_id +
                                     " ok=" + std::to_string(data.ack_ok) +
                                     " code=" + std::to_string(data.ack_code) +
                                     " msg=" + data.ack_message);
        return;
    }
    if (data.has_temp)  edgegw::logger::Logger::Debug("    temp=" + std::to_string(data.temp));
    if (data.has_humi)  edgegw::logger::Logger::Debug("    humi=" + std::to_string(data.humi));
    if (data.has_light) edgegw::logger::Logger::Debug("    light=" + std::to_string(data.light));
    if (data.has_ir)    edgegw::logger::Logger::Debug("    ir=" + std::to_string(data.ir));
}

// ------------------------------------------------------------------
// MQTT 消息回调入口
// 职责: 解析 JSON → 分发到数据处理
// ------------------------------------------------------------------
void OnMqttMessage(const edgegw::mqtt::Message& message) {
    edgegw::device::SensorData data;
    if (!edgegw::device::ParseSensorJson(message.payload, data)) {
        edgegw::logger::Logger::Warn("[mqtt] 解析失败, topic=" +
                                     message.topic +
                                     " payload=" + message.payload);
        return;
    }
    HandleSensorMessage(data);
}

}  // namespace

int main(int argc, char* argv[]) {
    // ---------- 1. 加载配置 ----------
    const std::string config_path =
        (argc > 1) ? argv[1] : "config/development.yaml";

    edgegw::config::ConfigManager cfg;
    if (!cfg.Load(config_path)) {
        std::fprintf(stderr, "[main] 加载配置失败: %s\n", config_path.c_str());
        return 1;
    }

    const std::string mqtt_host = cfg.GetString("mqtt.host", "127.0.0.1");
    const int mqtt_port = cfg.GetInt("mqtt.port", 1883);
    const std::string mqtt_client_id =
        cfg.GetString("mqtt.client_id", "edgegw");
    const std::string db_path =
        cfg.GetString("database.path", "data/edgegw.db");
    const std::string log_file =
        cfg.GetString("logging.file", "data/logs/edgegw.log");
    const std::string log_level =
        cfg.GetString("logging.level", "info");
    const std::string data_dir =
        cfg.GetString("data.dir", "data");
    const std::string web_host = cfg.GetString("web.host", "0.0.0.0");
    const int web_port = cfg.GetInt("web.port", 8080);
    const int ws_port = cfg.GetInt("web.ws_port", 8082);

    // ---------- 2. 初始化日志系统 ----------
    if (!edgegw::logger::Logger::Init(log_file, log_level)) {
        std::fprintf(stderr, "[main] 日志初始化失败\n");
        return 1;
    }
    edgegw::logger::Logger::Info("[main] 配置加载成功: " + config_path);
    edgegw::logger::Logger::Info("[main] MQTT: " + mqtt_host + ":" +
                                 std::to_string(mqtt_port) +
                                 "  DB: " + db_path +
                                 "  Log: " + log_file +
                                 "  Web: " + web_host + ":" +
                                 std::to_string(web_port));

    // ---------- 3. 初始化数据库 ----------
    if (!g_db.Init(db_path)) {
        edgegw::logger::Logger::Error("[main] 数据库初始化失败");
        return 1;
    }

    // ---------- 4. 初始化摄像头 ----------
    if (!g_camera.Init(data_dir)) {
        edgegw::logger::Logger::Error("[main] 摄像头初始化失败");
        return 1;
    }

    // ---------- 5. 初始化 MQTT ----------
    g_mqtt.SetMessageHandler(OnMqttMessage);

    edgegw::mqtt::MqttClient::Options mqtt_options;
    mqtt_options.host = mqtt_host;
    mqtt_options.port = mqtt_port;
    mqtt_options.client_id = mqtt_client_id;

    if (!g_mqtt.Start(mqtt_options)) {
        edgegw::logger::Logger::Error("[main] MQTT 启动失败");
        return 1;
    }
    g_mqtt.Subscribe("iotgw/v1/dev/+/report", 0);   // 传感器上报
    g_mqtt.Subscribe("iotgw/v1/dev/+/ack", 0);      // 命令回执
    g_mqtt.Subscribe("iotgw/v1/dev/+/status", 0);   // 状态上报

    // ---------- 6. 启动 Web 服务 ----------
    const std::string web_addr = web_host + ":" + std::to_string(web_port);
    g_web_ctx.registry = &g_registry;
    g_web_ctx.mqtt = &g_mqtt;
    g_web_ctx.camera = &g_camera;
    g_web_ctx.transport = &g_transport;
    if (!g_web.Start(web_addr, &g_web_ctx)) {
        edgegw::logger::Logger::Error("[main] Web 服务启动失败");
        return 1;
    }

    // ---------- 7. 启动 WebSocket 推送 ----------
    const std::string ws_addr = web_host + ":" + std::to_string(ws_port);
    if (!g_ws.Start(ws_addr)) {
        edgegw::logger::Logger::Error("[main] WebSocket 启动失败");
        return 1;
    }

    // ---------- 8. 主循环 ----------
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    edgegw::logger::Logger::Info("[main] 网关运行中, Ctrl+C 退出");
    while (g_running) {
        // Web 服务事件轮询 (处理 HTTP 请求)
        g_web.Poll(50);
        g_ws.Poll(50);

        // 检查退出信号 (简单忙等)
        if (g_running) {
            volatile int spin = 0;
            for (int i = 0; i < 1000000; ++i) { spin += i; }
        }
    }

    // ---------- 9. 退出清理 ----------
    edgegw::logger::Logger::Info("[main] 正在退出...");
    g_camera.Shutdown();
    g_ws.Stop();
    g_web.Stop();
    g_mqtt.Stop();
    g_db.Close();
    edgegw::logger::Logger::Shutdown();
    return 0;
}
