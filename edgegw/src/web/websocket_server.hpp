// ======================================================================
// websocket_server.hpp - WebSocket 实时推送
//
// 用途: Web/Qt 通过 WS 长连接, 网关主动推送设备数据变化
// 实现: 维护连接列表, Broadcast 向所有客户端发 JSON
// ======================================================================
#ifndef EDGEGW_WEB_WEBSOCKET_SERVER_HPP
#define EDGEGW_WEB_WEBSOCKET_SERVER_HPP

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace edgegw {
namespace web {

class WebSocketServer {
public:
    // 启动 WS 服务 (绑定在指定端口); 成功返回 true
    bool Start(const std::string& listen_addr);

    // 停止服务
    void Stop();

    // 轮询事件 (主循环调用); 同时把广播队列发出去
    void Poll(int timeout_ms);

    // 向所有已连接客户端广播文本消息 (线程安全: 仅入队, 主线程发送)
    void Broadcast(const std::string& message);

    // 当前连接数
    int ClientCount() const;

private:
    void* mgr_ = nullptr;              // struct mg_mgr*
    std::vector<void*> clients_;       // 已连接的 WS 连接 (mg_connection*)
    std::deque<std::string> pending_msgs_;   // 广播队列 (跨线程入队)
    std::mutex pending_mutex_;               // 队列锁
};

}  // namespace web
}  // namespace edgegw

#endif  // EDGEGW_WEB_WEBSOCKET_SERVER_HPP
