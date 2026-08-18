
// ======================================================================
// logger.cpp - 日志模块实现 (基于 spdlog)
// ======================================================================
#define SPDLOG_FMT_EXTERNAL
#include "logger/logger.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace edgegw {
namespace logger {

namespace {

// 全局 logger 指针 (static: 所有模块共享)
std::shared_ptr<spdlog::logger> g_logger;

// "info" → spdlog::level::info 的转换
spdlog::level::level_enum ParseLevel(const std::string& level) {
    if (level == "debug") return spdlog::level::debug;
    if (level == "warn")  return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    return spdlog::level::info;   // 默认 info
}

}  // namespace

bool Logger::Init(const std::string& log_file, const std::string& level) {
    try {
        // ① 创建两个 sink (输出目标):
        //    终端(带颜色) + 文件(按大小滚动)
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file,             // 文件路径
            5 * 1024 * 1024,      // 单个文件最大 5MB
            3                     // 保留 3 个滚动文件 (edgegw.log, .1, .2)
        );

        // ② 把两个 sink 组装成一个 logger
        g_logger = std::make_shared<spdlog::logger>(
            "edgegw",                                  // logger 名字
            spdlog::sinks_init_list{console_sink, file_sink}
        );

        // ③ 设置格式模板:
        //    [时间] [级别] 消息
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");

        // ④ 设置日志级别 (低于此级别的不输出)
        g_logger->set_level(ParseLevel(level));

        // ⑤ 关键! info 及以上级别立即刷盘 (程序崩溃也不丢日志)
        g_logger->flush_on(spdlog::level::info);

        // ⑤ 设为全局默认 logger, 这样 spdlog::info() 也能用
        spdlog::set_default_logger(g_logger);

        return true;
    } catch (const std::exception& e) {
        // spdlog 创建失败会抛异常 (比如目录不存在)
        return false;
    }
}

void Logger::Shutdown() {
    if (g_logger != nullptr) {
        g_logger->flush();        // 把缓冲写入文件
        spdlog::shutdown();
        g_logger.reset();
    }
}

void Logger::Debug(const std::string& msg) {
    if (g_logger) g_logger->debug(msg);
}

void Logger::Info(const std::string& msg) {
    if (g_logger) g_logger->info(msg);
}

void Logger::Warn(const std::string& msg) {
    if (g_logger) g_logger->warn(msg);
}

void Logger::Error(const std::string& msg) {
    if (g_logger) g_logger->error(msg);
}

}  // namespace logger
} // namespace edgegw