// ======================================================================
// web_server.hpp - Web 服务 (Mongoose HTTP)
//
// 提供 REST API:
//   GET  /api/health       → 健康检查
//   GET  /api/status       → 设备最新状态
//   POST /api/control      → 下发控制命令
//   GET  /api/camera/status    → 摄像头状态
//   POST /api/camera/start     → 启动推流
//   POST /api/camera/stop      → 停止推流
//   POST /api/camera/snapshot  → 抓拍
//   POST /api/camera/record/start → 开始录像
//   POST /api/camera/record/stop  → 停止录像
//   GET  /api/camera/stream    → MJPEG 流 (multipart, 浏览器 <img> 直接看)
//   POST /api/transport    → 切换通讯方式
// ======================================================================
#ifndef EDGEGW_WEB_WEB_SERVER_HPP
#define EDGEGW_WEB_WEB_SERVER_HPP

#include <chrono>
#include <string>
#include <vector>

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
    std::string* transport = nullptr;   // 当前通讯方式
};

// Web 服务类: 封装 Mongoose 生命周期
class WebServer {
public:
    // 启动 HTTP 服务; 成功返回 true
    bool Start(const std::string& listen_addr, WebContext* ctx);

    // 停止服务
    void Stop();

    // 轮询事件 (主循环里调用, 非阻塞)
    // 同时驱动 MJPEG 流推帧
    void Poll(int timeout_ms);

    // 注册 MJPEG 流客户端 (HttpHandler 回调内部使用)
    void AddStreamClient(void* conn);

    // 断开所有 MJPEG 流客户端 (停止推流时调用, 清理死连接)
    void ClearStreamClients();

private:
    void* mgr_ = nullptr;   // struct mg_mgr*
    WebContext* ctx_ = nullptr;

    // MJPEG 流客户端列表 + 帧率控制
    std::vector<void*> stream_clients_;
    std::chrono::steady_clock::time_point last_frame_at_{};

    // 向 MJPEG 客户端推一帧 (内部)
    void PushStreamFrame();
};

}  // namespace web
}  // namespace edgegw

#endif  // EDGEGW_WEB_WEB_SERVER_HPP
