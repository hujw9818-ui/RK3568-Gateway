// 配置模块独立测试(适配新版接口): 验证展平 map 方案
#include "config/config_manager.hpp"

#include <cstdio>
#include <string>

static void step(const char* msg) {
    std::printf("[STEP] %s\n", msg);
    std::fflush(stdout);
}

int main(int argc, char* argv[]) {
    const char* path = (argc > 1) ? argv[1] : "/tmp/development.yaml";
    step("start");

    edgegw::config::ConfigManager cfg;
    step("cfg created");

    if (!cfg.Load(path)) {
        std::printf("加载配置文件失败: %s\n", path);
        return 1;
    }
    step("Load OK");

    std::string name = cfg.GetString("name", "?");
    std::printf("name = [%s]\n", name.c_str());
    step("GetString name done");

    std::string host = cfg.GetString("mqtt.host", "?");
    std::printf("mqtt.host = [%s]\n", host.c_str());
    step("GetString mqtt.host done");

    int port = cfg.GetInt("mqtt.port", -1);
    std::printf("mqtt.port = [%d]\n", port);
    step("GetInt mqtt.port done");

    int web_port = cfg.GetInt("web.port", -1);
    std::printf("web.port = [%d]\n", web_port);
    step("GetInt web.port done");

    std::string dev = cfg.GetString("serial.device", "?");
    std::printf("serial.device = [%s]\n", dev.c_str());
    step("GetString serial.device done");

    int baud = cfg.GetInt("serial.baudrate", -1);
    std::printf("serial.baudrate = [%d]\n", baud);
    step("GetInt serial.baudrate done");

    std::string level = cfg.GetString("logging.level", "?");
    std::printf("logging.level = [%s]\n", level.c_str());
    step("GetString logging.level done");

    bool clean = cfg.GetBool("mqtt.clean_session", false);
    std::printf("mqtt.clean_session = [%d]\n", (int)clean);
    step("GetBool mqtt.clean_session done");

    // 不存在键应返回默认值
    int missing = cfg.GetInt("no.such.key", 42);
    std::printf("no.such.key = [%d] (应=42)\n", missing);
    step("missing key done");

    step("ALL OK");
    return 0;
}
