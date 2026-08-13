// ======================================================================
// web_server.cpp - Web 服务实现 (Mongoose 7.15)
// ======================================================================
#include "web/web_server.hpp"

#include <mongoose.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace edgegw {
namespace web {

namespace {

// 从 Mongoose 连接取回上下文 (userdata 模式)
WebContext* GetContext(struct mg_connection* c) {
    return static_cast<WebContext*>(c->fn_data);
}

// 检查请求方法是否为 POST
bool IsPost(const struct mg_http_message* hm) {
    // Mongoose 7.15: 用 mg_strcmp 比较 mg_str (mg_vcmp 不存在)
    return mg_strcmp(hm->method, mg_str("POST")) == 0;
}

// ------------------------------------------------------------
// HTTP 请求处理回调 (Mongoose 事件循环调用)
// ------------------------------------------------------------
void HttpHandler(struct mg_connection* c, int ev, void* ev_data) {
    if (ev != MG_EV_HTTP_MSG) {
        return;   // 只处理 HTTP 请求事件
    }
    auto* hm = static_cast<struct mg_http_message*>(ev_data);
    WebContext* ctx = GetContext(c);
    if (ctx == nullptr) {
        return;
    }

    // ---------- 路由分发 ----------
    // GET /api/health
    if (mg_match(hm->uri, mg_str("/api/health"), nullptr)) {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
        return;
    }

    // GET /api/status
    if (mg_match(hm->uri, mg_str("/api/status"), nullptr)) {
        std::string json = ctx->registry->ToJson();
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json.c_str());
        return;
    }

    // POST /api/control
    if (mg_match(hm->uri, mg_str("/api/control"), nullptr)) {
        if (ctx->mqtt == nullptr) {
            mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"mqtt_not_ready\"}");
            return;
        }
        // 选择通讯方式: MQTT 走 broker, Zigbee 走串口
        // 当前先统一走 MQTT, Zigbee 串口模块后续接入
        const std::string topic = "iotgw/v1/dev/mcu01/cmd";
        std::string payload(hm->body.buf, hm->body.len);
        bool ok = ctx->mqtt->Publish(topic, payload, 1);
        mg_http_reply(c, ok ? 200 : 500, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true}" : "{\"ok\":false}");
        return;
    }

    // ---------- 摄像头接口 ----------
    // GET /api/camera/status
    if (mg_match(hm->uri, mg_str("/api/camera/status"), nullptr)) {
        if (ctx->camera == nullptr) {
            mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"camera_not_ready\"}");
            return;
        }
        auto st = ctx->camera->GetStatus();
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"recording\":%s,\"record_file\":\"%s\",\"last_snapshot\":\"%s\"}",
                      st.recording ? "true" : "false",
                      st.record_file.c_str(), st.last_snapshot.c_str());
        return;
    }

    // POST /api/camera/snapshot
    if (mg_match(hm->uri, mg_str("/api/camera/snapshot"), nullptr)) {
        if (ctx->camera == nullptr || !IsPost(hm)) {
            mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"camera_not_ready\"}");
            return;
        }
        std::string path = ctx->camera->TakeSnapshot();
        if (path.empty()) {
            mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                          "{\"ok\":false,\"error\":\"snapshot_failed\"}");
            return;
        }
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"ok\":true,\"path\":\"%s\"}", path.c_str());
        return;
    }

    // POST /api/camera/start_record
    if (mg_match(hm->uri, mg_str("/api/camera/start_record"), nullptr)) {
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

    // POST /api/camera/stop_record
    if (mg_match(hm->uri, mg_str("/api/camera/stop_record"), nullptr)) {
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

    // ---------- 通讯方式切换 ----------
    // POST /api/transport  body: {"transport":"mqtt"} 或 {"transport":"zigbee"}
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
                      "{\"ok\":true,\"transport\":\"%s\"}", ctx->transport->c_str());
        return;
    }

    // 其他 → 静态文件服务 (www/ 目录, 前端页面)
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
    if (mgr_ != nullptr) {
        return false;   // 已经启动
    }

    auto* mgr = new struct mg_mgr;
    mg_mgr_init(mgr);
    mgr_ = mgr;

    // 监听 HTTP, ctx 作为 userdata 传给回调
    if (mg_http_listen(mgr, listen_addr.c_str(), HttpHandler, ctx) == nullptr) {
        std::printf("[web] 监听失败: %s\n", listen_addr.c_str());
        mg_mgr_free(mgr);
        delete mgr;
        mgr_ = nullptr;
        return false;
    }

    std::printf("[web] HTTP 服务启动: %s\n", listen_addr.c_str());
    return true;
}

// ------------------------------------------------------------
// 停止服务
// ------------------------------------------------------------
void WebServer::Stop() {
    if (mgr_ == nullptr) {
        return;
    }
    auto* mgr = static_cast<struct mg_mgr*>(mgr_);
    mg_mgr_free(mgr);
    delete mgr;
    mgr_ = nullptr;
    std::printf("[web] HTTP 服务已停止\n");
}

// ------------------------------------------------------------
// 轮询事件 (主循环调用, 非阻塞)
// ------------------------------------------------------------
void WebServer::Poll(int timeout_ms) {
    if (mgr_ == nullptr) {
        return;
    }
    mg_mgr_poll(static_cast<struct mg_mgr*>(mgr_), timeout_ms);
}

}  // namespace web
}  // namespace edgegw
