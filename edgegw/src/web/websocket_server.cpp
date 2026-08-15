// ======================================================================
// websocket_server.cpp - WebSocket 实时推送实现
// ======================================================================
#include "web/websocket_server.hpp"

#include <mongoose.h>

#include <algorithm>
#include <cstdio>
#include <mutex>

namespace edgegw {
namespace web {

namespace {

// 连接列表需要锁: Broadcast 可能从 MQTT 线程调用, 而连接事件在主线程
std::mutex g_clients_mutex;
std::vector<void*>* g_clients = nullptr;   // 指向 WebSocketServer 的 clients_

// ------------------------------------------------------------
// WS 事件回调 (Mongoose 调用)
// 注意: Mongoose 7.15 用 mg_http_listen 监听, WS 升级后触发 WS 事件
// ------------------------------------------------------------
void WsHandler(struct mg_connection* c, int ev, void* ev_data) {
    switch (ev) {
        case MG_EV_HTTP_MSG: {
            // WebSocket 升级握手: Mongoose 7.15 必须显式调用 mg_ws_upgrade
            // 该函数返回 void, 直接调用即可; Mongoose 内部判断是否 WS 升级
            auto* hm = static_cast<struct mg_http_message*>(ev_data);
            mg_ws_upgrade(c, hm, nullptr);
            break;
        }
        case MG_EV_WS_OPEN: {
            // 新客户端连接 → 加入列表
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            if (g_clients != nullptr) {
                g_clients->push_back(c);
                std::printf("[ws] 客户端连接, 当前 %zu 个\n", g_clients->size());
            }
            break;
        }
        case MG_EV_WS_MSG: {
            // 收到客户端消息 (目前忽略, 以后可做控制)
            break;
        }
        case MG_EV_CLOSE: {
            // 客户端断开 → 从列表移除
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            if (g_clients != nullptr) {
                auto it = std::find(g_clients->begin(), g_clients->end(), c);
                if (it != g_clients->end()) {
                    g_clients->erase(it);
                    std::printf("[ws] 客户端断开, 当前 %zu 个\n", g_clients->size());
                }
            }
            break;
        }
        default:
            break;
    }
}

}  // namespace

// ------------------------------------------------------------
// 启动 WS 服务
// Mongoose 7.15: 用 mg_http_listen 监听, WS 握手后触发 MG_EV_WS_OPEN
// ------------------------------------------------------------
bool WebSocketServer::Start(const std::string& listen_addr) {
    if (mgr_ != nullptr) {
        return false;
    }

    auto* mgr = new struct mg_mgr;
    mg_mgr_init(mgr);
    mgr_ = mgr;
    g_clients = &clients_;

    // 监听: 普通 HTTP 和 WS 升级都由 mg_http_listen 处理
    if (mg_http_listen(mgr, listen_addr.c_str(), WsHandler, nullptr) == nullptr) {
        std::printf("[ws] 监听失败: %s\n", listen_addr.c_str());
        mg_mgr_free(mgr);
        delete mgr;
        mgr_ = nullptr;
        g_clients = nullptr;
        return false;
    }

    std::printf("[ws] WebSocket 服务启动: %s\n", listen_addr.c_str());
    return true;
}

// ------------------------------------------------------------
// 停止服务
// ------------------------------------------------------------
void WebSocketServer::Stop() {
    if (mgr_ == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        clients_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_msgs_.clear();   // 清空未发送的广播
    }
    auto* mgr = static_cast<struct mg_mgr*>(mgr_);
    mg_mgr_free(mgr);
    delete mgr;
    mgr_ = nullptr;
    g_clients = nullptr;
    std::printf("[ws] WebSocket 服务已停止\n");
}

// ------------------------------------------------------------
// 轮询事件; 同时把广播队列中的消息发送出去
// (mongoose 连接操作必须在 mg_mgr_poll 同一线程, 队列保证跨线程安全)
// ------------------------------------------------------------
void WebSocketServer::Poll(int timeout_ms) {
    if (mgr_ == nullptr) {
        return;
    }
    mg_mgr_poll(static_cast<struct mg_mgr*>(mgr_), timeout_ms);

    // 取出广播批次 (跨线程入队 → 主线程发送)
    std::deque<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        batch.swap(pending_msgs_);
    }
    if (batch.empty()) return;

    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (const auto& msg : batch) {
        for (auto* c : clients_) {
            auto* conn = static_cast<struct mg_connection*>(c);
            if (conn->is_closing) continue;   // 跳过即将关闭的连接
            mg_ws_send(conn, msg.c_str(), msg.size(), WEBSOCKET_OP_TEXT);
        }
    }
}

// ------------------------------------------------------------
// 广播: 线程安全入队 (MQTT/Zigbee 线程可调用, 发送由主线程 Poll 执行)
// ------------------------------------------------------------
void WebSocketServer::Broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    // 限流: 生产快于消费时丢弃最老消息, 防止内存膨胀
    if (pending_msgs_.size() >= 256) {
        pending_msgs_.pop_front();
    }
    pending_msgs_.push_back(message);
}

// ------------------------------------------------------------
// 连接数
// ------------------------------------------------------------
int WebSocketServer::ClientCount() const {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    return static_cast<int>(clients_.size());
}

}  // namespace web
}  // namespace edgegw
