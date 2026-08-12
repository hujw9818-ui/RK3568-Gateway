// ======================================================================
// edgegw - 网关主程序入口
//
// 启动流程:
//   1. 加载 YAML 配置 (ConfigManager)
//   2. 初始化 SQLite (SqliteStore)
//   3. 初始化 MQTT 客户端, 订阅 iotgw/v1/dev/+/report
//   4. 启动 Web 服务 (Mongoose REST API, :8080)
//   5. 启动 WebSocket 推送 (:8082)
//   6. 主循环: Poll Web/WS + 等待退出信号
//
// 数据处理:
//   MQTT 收到消息 → OnMqttMessage → HandleSensorMessage
//     → 设备状态表更新 + SQLite 历史存储 + WebSocket 广播
//
// Web 接口:
//   GET  /api/health   → 健康检查
//   GET  /api/status   → 设备最新状态 (registry)
//   POST /api/control  → 下发控制命令 (mqtt)
//   WS   :8082         → 实时推送设备数据 (Web/Qt 同步)
//
// 用法: ./edgegw config/development.yaml
// ======================================================================
#include "config/config_manager.hpp"
#include "database/sqlite_store.hpp"
#include "device/device_registry.hpp"
#include "device/json_parser.hpp"
#include "device/sensor_data.hpp"
#include "mqtt/mqtt_client.hpp"
#include "web/web_server.hpp"
#include "web/websocket_server.hpp"

#include <cstdio>
#include <csignal>
#include <string>

namespace {

volatile std::sig_atomic_t g_running = 1;

void OnSignal(int /*sig*/) { g_running = 0; }

// 全局状态 (模块间共享, 各自内部有锁保护)
edgegw::device::DeviceRegistry g_registry;   // 设备状态表(内存)
edgegw::database::SqliteStore g_db;          // 历史数据库(磁盘)
edgegw::mqtt::MqttClient g_mqtt;             // MQTT 客户端(Web要发命令)
edgegw::web::WebContext g_web_ctx;           // Web 上下文(依赖注入)
edgegw::web::WebServer g_web;                // Web 服务 (HTTP REST)
edgegw::web::WebSocketServer g_ws;           // WebSocket 推送

// ------------------------------------------------------------------
// 处理一条解析成功的传感器数据
// 职责: 更新设备状态表 + 写入历史数据库 + WebSocket 推送 + 打印
// ------------------------------------------------------------------
void HandleSensorMessage(const edgegw::device::SensorData& data) {
    // 1. 更新设备状态表 (最新值)
    g_registry.UpdateSensor(data);

    // 2. 写入历史数据库 (每条记录)
    g_db.InsertTelemetry(data);

    // 3. WebSocket 推送 (数据变化实时推给 Web/Qt 实现双端同步)
    g_ws.Broadcast(g_registry.ToJson());

    // 4. 打印调试信息
    std::printf("[mqtt] dev=%s type=%s msg=%s ts=%lld\n",
                data.dev_id.c_str(), data.type.c_str(),
                data.msg_id.c_str(), static_cast<long long>(data.ts));
    if (data.has_temp) std::printf("        temp=%.1f\n", data.temp);
    if (data.has_humi) std::printf("        humi=%.1f\n", data.humi);
    if (data.has_light) std::printf("        light=%.1f\n", data.light);
    if (data.has_ir) std::printf("        ir=%.1f\n", data.ir);
    std::printf("[registry] %s\n", g_registry.ToJson().c_str());
}

// ------------------------------------------------------------------
// MQTT 消息回调入口
// 职责: 解析 JSON → 分发到数据处理
// ------------------------------------------------------------------
void OnMqttMessage(const edgegw::mqtt::Message& message) {
    edgegw::device::SensorData data;
    if (!edgegw::device::ParseSensorJson(message.payload, data)) {
        std::printf("[mqtt] 解析失败, topic=%s payload=%.80s\n",
                    message.topic.c_str(), message.payload.c_str());
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
        std::printf("[main] 加载配置失败: %s\n", config_path.c_str());
        return 1;
    }
    std::printf("[main] 配置加载成功: %s\n", config_path.c_str());

    const std::string mqtt_host = cfg.GetString("mqtt.host", "127.0.0.1");
    const int mqtt_port = cfg.GetInt("mqtt.port", 1883);
    const std::string mqtt_client_id =
        cfg.GetString("mqtt.client_id", "edgegw");
    const std::string db_path =
        cfg.GetString("database.path", "data/edgegw.db");
    const std::string web_host = cfg.GetString("web.host", "0.0.0.0");
    const int web_port = cfg.GetInt("web.port", 8080);
    const int ws_port = cfg.GetInt("web.ws_port", 8082);
    std::printf("[main] MQTT: %s:%d (%s)  DB: %s  Web: %s:%d  WS: %s:%d\n",
                mqtt_host.c_str(), mqtt_port, mqtt_client_id.c_str(),
                db_path.c_str(), web_host.c_str(), web_port,
                web_host.c_str(), ws_port);

    // ---------- 2. 初始化数据库 ----------
    if (!g_db.Init(db_path)) {
        std::printf("[main] 数据库初始化失败\n");
        return 1;
    }

    // ---------- 3. 初始化 MQTT ----------
    g_mqtt.SetMessageHandler(OnMqttMessage);

    edgegw::mqtt::MqttClient::Options mqtt_options;
    mqtt_options.host = mqtt_host;
    mqtt_options.port = mqtt_port;
    mqtt_options.client_id = mqtt_client_id;

    if (!g_mqtt.Start(mqtt_options)) {
        std::printf("[main] MQTT 启动失败\n");
        return 1;
    }
    g_mqtt.Subscribe("iotgw/v1/dev/+/report", 0);

    // ---------- 4. 启动 Web 服务 ----------
    const std::string web_addr = web_host + ":" + std::to_string(web_port);
    g_web_ctx.registry = &g_registry;
    g_web_ctx.mqtt = &g_mqtt;
    if (!g_web.Start(web_addr, &g_web_ctx)) {
        std::printf("[main] Web 服务启动失败\n");
        return 1;
    }

    // ---------- 5. 启动 WebSocket 推送 ----------
    const std::string ws_addr = web_host + ":" + std::to_string(ws_port);
    if (!g_ws.Start(ws_addr)) {
        std::printf("[main] WebSocket 启动失败\n");
        return 1;
    }

    // ---------- 6. 主循环 ----------
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    std::printf("[main] 网关运行中, Ctrl+C 退出\n");
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

    // ---------- 7. 退出清理 ----------
    std::printf("[main] 正在退出...\n");
    g_ws.Stop();
    g_web.Stop();
    g_mqtt.Stop();
    g_db.Close();
    std::printf("[main] 退出完成\n");
    return 0;
}
