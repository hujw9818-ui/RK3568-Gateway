
// ======================================================================
// logger.hpp - 日志模块 (基于 spdlog)
//
// 用法:
//   Logger::Init("data/logs/edgegw.log", "info");
//   Logger::Info("MQTT connected");
//   Logger::Warn("设备超时");
//   Logger::Error("JSON 解析失败");
//   Logger::Debug("收到原始消息: ...");
//
// 特性: 终端+文件双输出, 按大小滚动, 线程安全
// ======================================================================

#ifndef EDGEGW_LOGGER_LOGGER_HPP
#define EDGEGW_LOGGER_LOGGER_HPP

#include <string>

namespace edgegw {
namespace logger {

class Logger {
public:
    // 初始化日志系统; 成功返回 true
    //   log_file: 日志文件路径 (例如 data/logs/edgegw.log)
    //   level:    "debug" / "info" / "warn" / "error"
    static bool Init(const std::string& log_file, const std::string& level);

    // 关闭日志 (程序退出时调用)
    static void Shutdown();

    // 四种日志级别
    static void Debug(const std::string& msg);
    static void Info(const std::string& msg);
    static void Warn(const std::string& msg);
    static void Error(const std::string& msg);
};

}  // namespace logger
}  // namespace edgegw

#endif // EDGEGW_LOGGER_LOGGER_HPP