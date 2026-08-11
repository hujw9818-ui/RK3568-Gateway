// ======================================================================
// edgegw - 网关主程序入口
// 当前里程碑: 验证 CMake 构建 + Mongoose HTTP 服务
//   curl http://<ip>:8080/api/health  ->  {"status":"ok"}
// ======================================================================
#include "mongoose.h"

#include <cstdio>
#include <cstring>
#include <csignal>

static volatile sig_atomic_t g_running = 1;

static void on_signal(int /*sig*/) { g_running = 0; }

static void http_handler(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    // GET /api/health -> {"status":"ok"}
    if (mg_match(hm->uri, mg_str("/api/health"), NULL)) {
      mg_http_reply(c, 200, "application/json", "{\"status\":\"ok\"}");
      return;
    }

    // 其余请求: 静态文件 (www/)
    struct mg_http_serve_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.root_dir = "www";
    mg_http_serve_dir(c, hm, &opts);
  }
}

int main(void) {
  // 信号处理: Ctrl+C 优雅退出
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  struct mg_mgr mgr;
  mg_mgr_init(&mgr);

  const char *listen_addr = "http://0.0.0.0:8080";
  mg_http_listen(&mgr, listen_addr, http_handler, NULL);
  std::printf("edgegw listening on %s\n", listen_addr);

  while (g_running) {
    mg_mgr_poll(&mgr, 50);
  }

  mg_mgr_free(&mgr);
  std::printf("edgegw exit\n");
  return 0;
}
