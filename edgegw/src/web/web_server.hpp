// ======================================================================
// web_server.hpp - Web 服务 (Mongoose HTTP)
//
// 提供 REST API:
//   GET  /api/health       → {"status":"ok"}
//   GET  /api/status       → 设备最新状态 (来自 DeviceRegistry)
//   POST /api/control      → 下发控制命令 (通过 MqttClient)
//   GET  /api/camera/status    → 摄像头状态
//   POST /api/camera/snapshot  → 拍照
//   POST /api/camera/start_record → 开始录像
//   POST /api/camera/stop_record  → 停止录像
//   POST /api/transport    → 切换通讯方式 (mqtt/zigbee)
// ======================================================================
#ifndef EDGEGW_WEB_WEB_SERVER_HPP
#define EDGEGW_WEB_WEB_SERVER_HPP

#include <string>

#include "camera/camera_manager.hpp"
#include "device/device_registry.hpp"
#include "mqtt/mqtt_client.hpp"

namespace edgegw {
namespace web {

// 传给 Mongoose 回调的上下文 (依赖注入)
struct WebContext {
    device::DeviceRegistry* registry = nullptr;
    mqtt::MqttClient* mqtt = nullptr;
    camera::CameraManager* camera = nullptr;
    std::string* transport = nullptr;   // 当前通讯方式 ("mqtt"/"zigbee")
};

// Web 服务类: 封装 Mongoose 生命周期
class WebServer {
public:
    // 启动 HTTP 服务; 成功返回 true
    bool Start(const std::string& listen_addr, WebContext* ctx);

    // 停止服务
    void Stop();

    // 轮询事件 (主循环里调用, 非阻塞)
    void Poll(int timeout_ms);

private:
    void* mgr_ = nullptr;   // struct mg_mgr* (隐藏实现)
};

}  // namespace web
}  // namespace edgegw

#endif  // EDGEGW_WEB_WEB_SERVER_HPP
