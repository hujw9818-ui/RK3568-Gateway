// ======================================================================
// edgegw - 网关主程序入口
//
// 启动流程:
//   1. 加载 YAML 配置 (ConfigManager)
//   2. 初始化 MQTT 客户端, 订阅 iotgw/v1/dev/+/report
//   3. 主循环: 轮询 MQTT, 收到消息 → JSON 解析 → 打印/存设备表
//
// 用法: ./edgegw config/development.yaml
// ======================================================================
#include "config/config_manager.hpp"
#include "device/json_parser.hpp"
#include "device/sensor_data.hpp"
#include "mqtt/mqtt_client.hpp"

#include <cstdio>
#include <csignal>
#include <string>

namespace {

volatile std::sig_atomic_t g_running = 1;

void OnSignal(int /*sig*/) { g_running = 0; }

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
    std::printf("[main] MQTT 配置: %s:%d (%s)\n", mqtt_host.c_str(),
                mqtt_port, mqtt_client_id.c_str());

    // ---------- 2. 初始化 MQTT ----------
    edgegw::mqtt::MqttClient mqtt;

    mqtt.SetMessageHandler([](const edgegw::mqtt::Message& message) {
        // 收到消息 → 解析 JSON
        edgegw::device::SensorData data;
        if (!edgegw::device::ParseSensorJson(message.payload, data)) {
            std::printf("[mqtt] 解析失败, topic=%s payload=%.80s\n",
                        message.topic.c_str(), message.payload.c_str());
            return;
        }

        // 解析成功 → 打印(暂时代替设备表/数据库)
        std::printf("[mqtt] dev=%s type=%s msg=%s ts=%lld\n",
                    data.dev_id.c_str(), data.type.c_str(),
                    data.msg_id.c_str(), static_cast<long long>(data.ts));
        if (data.has_temp) std::printf("        temp=%.1f\n", data.temp);
        if (data.has_humi) std::printf("        humi=%.1f\n", data.humi);
        if (data.has_light) std::printf("        light=%.1f\n", data.light);
        if (data.has_ir) std::printf("        ir=%.1f\n", data.ir);
    });

    edgegw::mqtt::MqttClient::Options mqtt_options;
    mqtt_options.host = mqtt_host;
    mqtt_options.port = mqtt_port;
    mqtt_options.client_id = mqtt_client_id;

    if (!mqtt.Start(mqtt_options)) {
        std::printf("[main] MQTT 启动失败\n");
        return 1;
    }

    // 订阅设备上报主题
    mqtt.Subscribe("iotgw/v1/dev/+/report", 0);

    // ---------- 3. 主循环 ----------
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    std::printf("[main] 网关运行中, Ctrl+C 退出\n");
    while (g_running) {
        // MQTT 是后台线程, 主循环只需等待退出信号
        // 后续这里会加: Web 服务轮询 / 串口轮询 / 心跳日志
        if (g_running) {
            // 每 100ms 醒来一次检查退出信号
            // 用简单的忙等代替 sleep, 保证信号响应及时
            volatile int spin = 0;
            for (int i = 0; i < 1000000; ++i) { spin += i; }
        }
    }

    std::printf("[main] 正在退出...\n");
    mqtt.Stop();
    std::printf("[main] 退出完成\n");
    return 0;
}
