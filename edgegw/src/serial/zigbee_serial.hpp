// ======================================================================
// zigbee_serial.hpp - Zigbee (DL-30 透传) 串口收发
//
// DL-30 透传模块: 与单片机通过串口收发纯文本 JSON (与 MQTT 同格式),
// 每行以 \r\n 结尾, 无 MQTT 协议壳。
// 网关从串口按行读取上报数据, 控制命令也按行写入串口。
// ======================================================================
#ifndef EDGEGW_SERIAL_ZIGBEE_SERIAL_HPP
#define EDGEGW_SERIAL_ZIGBEE_SERIAL_HPP

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace edgegw {
namespace serial {

class ZigbeeSerial {
public:
    // 收到一行 JSON 的回调 (已去掉 \r\n)
    using LineCallback = std::function<void(const std::string&)>;

    // 打开串口 (8N1); 成功返回 true
    bool Open(const std::string& device, int baudrate = 115200);

    // 关闭串口 + 停止读线程
    void Close();

    // 发送一行 (自动补 \r\n)
    bool Send(const std::string& line);

    // 启动读线程 (回调在独立线程执行)
    void StartReadLoop(LineCallback cb);

    bool IsOpen() const { return fd_ >= 0; }

private:
    int fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace serial
}  // namespace edgegw

#endif  // EDGEGW_SERIAL_ZIGBEE_SERIAL_HPP
