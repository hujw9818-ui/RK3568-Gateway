// ======================================================================
// web_server.cpp - Web 服务实现 (Mongoose 7.15)
// 含 MJPEG 流推帧 (multipart/x-mixed-replace, 浏览器 <img> 直接看)
// ======================================================================
#include "web/web_server.hpp"
#include "web/websocket_server.hpp"

#include "device/json_parser.hpp"

#include <mongoose.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace edgegw {
namespace web {

namespace {

// 全局 WebServer 指针 (Mongoose 回调是 C 函数, 通过它访问实例)
WebServer* g_web_server = nullptr;

// 从 Mongoose 连接取回上下文
WebContext* GetContext(struct mg_connection* c) {
    return static_cast<WebContext*>(c->fn_data);
}

bool IsPost(const struct mg_http_message* hm) {
    return mg_strcmp(hm->method, mg_str("POST")) == 0;
}

bool IsGet(const struct mg_http_message* hm) {
    return mg_strcmp(hm->method, mg_str("GET")) == 0;
}

// HTTP 请求回调 (Mongoose 调用)
void HttpHandler(struct mg_connection* c, int ev, void* ev_data) {
    // MG_EV_CLOSE: 连接即将释放 (此时指针仍有效), 从 MJPEG 客户端列表移除
    if (ev == MG_EV_CLOSE) {
        if (g_web_server != nullptr) g_web_server->OnStreamClientClose(c);
        return;
    }
    if (ev != MG_EV_HTTP_MSG) return;
    auto* hm = static_cast<struct mg_http_message*>(ev_data);
    WebContext* ctx = GetContext(c);
    if (ctx == nullptr || g_web_server == nullptr) return;

    // 路由分发
    // GET /api/health
    if (mg_match(hm->uri, mg_str("/api/health"), nullptr)) {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"status\":\"ok\"}");
        return;
    }

    // GET /api/status
    if (mg_match(hm->uri, mg_str("/api/status"), nullptr)) {
        std::string json = ctx->registry->ToJson();
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "%s", json.c_str());
        return;
    }

    // POST /api/control
    if (mg_match(hm->uri, mg_str("/api/control"), nullptr)) {
        if (ctx->mqtt == nullptr) {
            mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"mqtt_not_ready\"}");
            return;
        }
        const std::string topic = "iotgw/v1/dev/mcu01/cmd";
        std::string payload(hm->body.buf, hm->body.len);

        // ① 下发命令到 MQTT
        bool ok = ctx->mqtt->Publish(topic, payload, 1);

        // ② 乐观更新: 解析命令 → 更新状态表 (前端立即同步)
        if (ok && ctx->registry != nullptr) {
            edgegw::device::SensorData cmd;
            if (edgegw::device::ParseCommandJson(payload, cmd)) {
                ctx->registry->UpdateSensor(cmd);
                // ③ WS 推送最新状态 (前端控件实时变化)
                if (ctx->ws != nullptr) {
                    ctx->ws->Broadcast(ctx->registry->ToJson());
                }
            }
        }

        mg_http_reply(c, ok ? 200 : 500, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true}" : "{\"ok\":false}");
        return;
    }

    // ---------- 摄像头 ----------
    // GET /api/camera/stream → MJPEG 流 (multipart)
    if (mg_match(hm->uri, mg_str("/api/camera/stream"), nullptr)) {
        if (!IsGet(hm)) {
            mg_http_reply(c, 405, "", "method not allowed");
            return;
        }
        // 启动推流 (如果没跑)
        if (ctx->camera != nullptr) ctx->camera->Start();
        mg_printf(c,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: multipart/x-mixed-replace; boundary=iotgwframe\r\n"
                  "Cache-Control: no-store\r\n"
                  "Connection: close\r\n"
                  "\r\n");
        g_web_server->AddStreamClient(c);
        return;
    }

    // GET /api/camera/status
    if (mg_match(hm->uri, mg_str("/api/camera/status"), nullptr)) {
        if (ctx->camera == nullptr) {
            mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"camera_not_ready\"}");
            return;
        }
        auto st = ctx->camera->GetStatus();
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"streaming\":%s,\"recording\":%s,\"frame_ready\":%s,"
                      "\"record_file\":\"%s\",\"last_snapshot\":\"%s\"}",
                      st.streaming ? "true" : "false",
                      st.recording ? "true" : "false",
                      st.frame_ready ? "true" : "false",
                      st.record_file.c_str(), st.last_snapshot.c_str());
        return;
    }

    // POST /api/camera/start
    if (mg_match(hm->uri, mg_str("/api/camera/start"), nullptr)) {
        if (ctx->camera == nullptr || !IsPost(hm)) {
            mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"camera_not_ready\"}");
            return;
        }
        bool ok = ctx->camera->Start();
        mg_http_reply(c, ok ? 200 : 503, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true,\"message\":\"推流已启动\"}"
                         : "{\"ok\":false,\"message\":\"推流启动失败\"}");
        return;
    }

    // POST /api/camera/stop
    if (mg_match(hm->uri, mg_str("/api/camera/stop"), nullptr)) {
        if (ctx->camera == nullptr || !IsPost(hm)) {
            mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"camera_not_ready\"}");
            return;
        }
        bool ok = ctx->camera->Stop();
        // 关键! 断开所有 MJPEG 流客户端 (清理死连接, 否则下次推流卡)
        g_web_server->ClearStreamClients();
        mg_http_reply(c, ok ? 200 : 500, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true,\"message\":\"推流已停止\"}"
                         : "{\"ok\":false,\"message\":\"停止失败\"}");
        return;
    }

    // POST /api/camera/snapshot
    if (mg_match(hm->uri, mg_str("/api/camera/snapshot"), nullptr)) {
        if (ctx->camera == nullptr || !IsPost(hm)) {
            mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"camera_not_ready\"}");
            return;
        }
        std::string b64 = ctx->camera->TakeSnapshotBase64();
        if (b64.empty()) {
            mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"snapshot_failed\"}");
            return;
        }
        // data 字段携带 base64, 前端 data URI 直接显示 (绕开文件伺服)
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"ok\":true,\"data\":\"%s\"}", b64.c_str());
        return;
    }

    // POST /api/camera/record/start
    if (mg_match(hm->uri, mg_str("/api/camera/record/start"), nullptr)) {
        if (ctx->camera == nullptr || !IsPost(hm)) {
            mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"camera_not_ready\"}");
            return;
        }
        std::string path = ctx->camera->StartRecord();
        if (path.empty()) {
            mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"record_start_failed\"}");
            return;
        }
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"ok\":true,\"path\":\"%s\"}", path.c_str());
        return;
    }

    // POST /api/camera/record/stop
    if (mg_match(hm->uri, mg_str("/api/camera/record/stop"), nullptr)) {
        if (ctx->camera == nullptr || !IsPost(hm)) {
            mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"camera_not_ready\"}");
            return;
        }
        bool ok = ctx->camera->StopRecord();
        mg_http_reply(c, ok ? 200 : 500, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true}" : "{\"ok\":false}");
        return;
    }

    // GET /api/camera/last_photo → 最新帧 JPEG (单帧)
    if (mg_match(hm->uri, mg_str("/api/camera/last_photo"), nullptr)) {
        if (ctx->camera == nullptr || !ctx->camera->HasFrame()) {
            mg_http_reply(c, 404, "", "no frame");
            return;
        }
        struct mg_http_serve_opts opts;
        memset(&opts, 0, sizeof(opts));
        mg_http_serve_file(c, hm, ctx->camera->FramePath().c_str(), &opts);
        return;
    }

    // ---------- 通讯方式切换 ----------
    if (mg_match(hm->uri, mg_str("/api/transport"), nullptr)) {
        if (ctx->transport == nullptr || !IsPost(hm)) {
            mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"bad_request\"}");
            return;
        }
        std::string body(hm->body.buf, hm->body.len);
        if (body.find("\"zigbee\"") != std::string::npos) {
            *ctx->transport = "zigbee";
        } else {
            *ctx->transport = "mqtt";
        }
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"ok\":true,\"transport\":\"%s\"}",
                      ctx->transport->c_str());
        return;
    }

    // 其他 → 静态文件 (www/)
    struct mg_http_serve_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.root_dir = "www";
    mg_http_serve_dir(c, hm, &opts);
}

}  // namespace

// ------------------------------------------------------------
// 启动 HTTP 服务
// ------------------------------------------------------------
bool WebServer::Start(const std::string& listen_addr, WebContext* ctx) {
    if (mgr_ != nullptr) return false;

    auto* mgr = new struct mg_mgr;
    mg_mgr_init(mgr);
    mgr_ = mgr;
    ctx_ = ctx;
    g_web_server = this;

    if (mg_http_listen(mgr, listen_addr.c_str(), HttpHandler, ctx) == nullptr) {
        std::printf("[web] 监听失败: %s\n", listen_addr.c_str());
        mg_mgr_free(mgr);
        delete mgr;
        mgr_ = nullptr;
        g_web_server = nullptr;
        return false;
    }

    std::printf("[web] HTTP 服务启动: %s\n", listen_addr.c_str());
    return true;
}

// ------------------------------------------------------------
// 停止服务
// ------------------------------------------------------------
void WebServer::Stop() {
    if (mgr_ == nullptr) return;
    auto* mgr = static_cast<struct mg_mgr*>(mgr_);
    mg_mgr_free(mgr);
    delete mgr;
    mgr_ = nullptr;
    g_web_server = nullptr;
    // 清空 stream 客户端 (避免死连接阻塞后续推帧)
    stream_clients_.clear();
    last_frame_at_ = std::chrono::steady_clock::time_point{};
    std::printf("[web] HTTP 服务已停止\n");
}

// ------------------------------------------------------------
// 轮询事件 + MJPEG 推帧
// ------------------------------------------------------------
void WebServer::Poll(int timeout_ms) {
    if (mgr_ == nullptr) return;
    mg_mgr_poll(static_cast<struct mg_mgr*>(mgr_), timeout_ms);
    PushStreamFrame();
    CleanupStreamClients();
}

// ------------------------------------------------------------
// 清理死连接; 客户端全部断开后自动停推流
// (推流生命周期由网关管理, 前端断开自己的连接即可, 不影响其他客户端)
// ------------------------------------------------------------
void WebServer::CleanupStreamClients() {
    // 清理死连接 (is_closing 的移除)
    for (auto it = stream_clients_.begin(); it != stream_clients_.end();) {
        auto* c = static_cast<struct mg_connection*>(*it);
        if (c->is_closing) {
            it = stream_clients_.erase(it);
        } else {
            ++it;
        }
    }
    // 曾经有客户端, 现在全部断开 → 自动停推流 (推流生命周期由网关管理)
    if (had_stream_clients_ && stream_clients_.empty() && ctx_ != nullptr &&
        ctx_->camera != nullptr) {
        had_stream_clients_ = false;
        std::printf("[web] 所有 MJPEG 客户端已断开, 自动停止推流\n");
        ctx_->camera->Stop();
    }
}

// 注册 MJPEG 流客户端 (HttpHandler 回调)
void WebServer::AddStreamClient(void* conn) {
    stream_clients_.push_back(conn);
    had_stream_clients_ = true;
    std::printf("[web] MJPEG 客户端已连接, 当前 %zu 个\n",
                stream_clients_.size());
}

// MJPEG 客户端断开 (MG_EV_CLOSE 时调用, 连接尚未释放, 指针安全)
void WebServer::OnStreamClientClose(void* conn) {
    for (auto it = stream_clients_.begin(); it != stream_clients_.end(); ++it) {
        if (*it == conn) {
            stream_clients_.erase(it);
            std::printf("[web] MJPEG 客户端断开, 剩余 %zu 个\n",
                        stream_clients_.size());
            return;
        }
    }
}

// 断开所有 MJPEG 流客户端 (停止推流时调用)
void WebServer::ClearStreamClients() {
    for (auto* p : stream_clients_) {
        auto* conn = static_cast<struct mg_connection*>(p);
        conn->is_draining = 1;   // 标记连接关闭
        conn->is_closing = 1;
    }
    stream_clients_.clear();
    had_stream_clients_ = false;   // 显式停推流, 不触发自动停流
    last_frame_at_ = std::chrono::steady_clock::time_point{};
    std::printf("[web] 已断开 %zu 个 MJPEG 客户端\n",
                static_cast<size_t>(stream_clients_.size()));
}

// ------------------------------------------------------------
// 向 MJPEG 客户端推一帧 (15fps 帧率控制)
// ------------------------------------------------------------
void WebServer::PushStreamFrame() {
    if (stream_clients_.empty() || ctx_ == nullptr || ctx_->camera == nullptr)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (last_frame_at_ != std::chrono::steady_clock::time_point{} &&
        now - last_frame_at_ < std::chrono::milliseconds(100))
        return;

    // 读最新帧; 文件消失窗口 (multifilesink unlink 间隙) 时用缓存帧兜底
    std::string data;
    if (ctx_->camera->HasFrame()) {
        std::ifstream file(ctx_->camera->FramePath(), std::ios::binary);
        if (file.is_open()) {
            data.assign((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
        }
    }
    if (data.empty()) data = last_frame_data_;
    if (data.empty()) return;
    last_frame_data_ = data;

    last_frame_at_ = now;

    for (auto it = stream_clients_.begin(); it != stream_clients_.end();) {
        auto* c = static_cast<struct mg_connection*>(*it);
        if (c->is_closing) {
            it = stream_clients_.erase(it);
            continue;
        }
        // 发送失败 (连接已死) 立即移除, 避免阻塞后续推帧
        const int hdr_n = mg_printf(c,
                  "--iotgwframe\r\n"
                  "Content-Type: image/jpeg\r\n"
                  "Content-Length: %lu\r\n\r\n",
                  static_cast<unsigned long>(data.size()));
        const int body_n = mg_send(c, data.data(), data.size());
        if (hdr_n <= 0 || body_n <= 0) {
            it = stream_clients_.erase(it);
            continue;
        }
        ++it;
    }
}

}  // namespace web
}  // namespace edgegw
