// 配置模块独立测试: 精确定位段错误位置
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

    // 逐个测试, 每个都打印
    {
        std::string v = cfg.Get("name", std::string("?"));
        std::printf("name = [%s]\n", v.c_str());
        step("Get name done");
    }
    {
        std::string v = cfg.Get("mqtt.host", std::string("?"));
        std::printf("mqtt.host = [%s]\n", v.c_str());
        step("Get mqtt.host done");
    }
    {
        int v = cfg.Get("mqtt.port", -1);
        std::printf("mqtt.port = [%d]\n", v);
        step("Get mqtt.port done");
    }
    {
        int v = cfg.Get("web.port", -1);
        std::printf("web.port = [%d]\n", v);
        step("Get web.port done");
    }
    {
        std::string v = cfg.Get("serial.device", std::string("?"));
        std::printf("serial.device = [%s]\n", v.c_str());
        step("Get serial.device done");
    }
    {
        std::string v = cfg.Get("logging.level", std::string("?"));
        std::printf("logging.level = [%s]\n", v.c_str());
        step("Get logging.level done");
    }

    step("ALL OK");
    return 0;
}
