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
    auto* mgr = static_cast<struct mg_mgr*>(mgr_);
    mg_mgr_free(mgr);
    delete mgr;
    mgr_ = nullptr;
    g_clients = nullptr;
    std::printf("[ws] WebSocket 服务已停止\n");
}

// ------------------------------------------------------------
// 轮询事件
// ------------------------------------------------------------
void WebSocketServer::Poll(int timeout_ms) {
    if (mgr_ == nullptr) {
        return;
    }
    mg_mgr_poll(static_cast<struct mg_mgr*>(mgr_), timeout_ms);
}

// ------------------------------------------------------------
// 向所有客户端广播
// ------------------------------------------------------------
void WebSocketServer::Broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (auto* c : clients_) {
        mg_ws_send(static_cast<struct mg_connection*>(c), message.c_str(),
                   message.size(), WEBSOCKET_OP_TEXT);
    }
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
