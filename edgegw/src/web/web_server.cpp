// ======================================================================
// web_server.cpp - Web 服务实现 (Mongoose 7.15)
// ======================================================================
#include "web/web_server.hpp"

#include <mongoose.h>

#include <cstdio>
#include <string>

namespace edgegw {
namespace web {

namespace {

// 从 Mongoose 连接取回上下文 (userdata 模式)
WebContext* GetContext(struct mg_connection* c) {
    return static_cast<WebContext*>(c->fn_data);
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
        // 简单解析: 从 body 提取 target 和 on 字段 (完整解析后续用 rapidjson)
        // 直接把原始 body 转发给设备 (TODO: 正式解析)
        const std::string topic = "iotgw/v1/dev/mcu01/cmd";
        bool ok = ctx->mqtt->Publish(topic, std::string(hm->body.buf, hm->body.len), 1);
        mg_http_reply(c, ok ? 200 : 500, "Content-Type: application/json\r\n",
                      ok ? "{\"ok\":true}" : "{\"ok\":false}");
        return;
    }

    // 其他 → 静态文件服务 (www/ 目录, 前端页面)
    // 例如: GET / → www/index.html
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