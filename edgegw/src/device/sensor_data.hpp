// ======================================================================
// sensor_data.hpp - 设备上报数据模型
//
// 对应 PROTOCOL.md 的消息格式:
// 1. 传感器上报 (type=sensor):
//    {"type":"sensor","dev":"mcu01","msg_id":"...","ts":0,
//     "body":{"data":{"temp":25.6,"humi":60.1,"light":320,"ir":0}}}
// 2. 执行器状态上报 (type=status):
//    {"type":"status","dev":"mcu01",...,
//     "body":{"data":{"led_on":1,"led_brightness":80,"fan_on":1,...}}}
// 3. 命令回执 (type=ack):
//    {"type":"ack","dev":"mcu01","req_id":"cmd-2001",...,
//     "body":{"ok":true,"code":0,"message":"ok"}}
// ======================================================================
#ifndef EDGEGW_DEVICE_SENSOR_DATA_HPP
#define EDGEGW_DEVICE_SENSOR_DATA_HPP

#include <cstdint>
#include <string>

namespace edgegw {
namespace device {

// 一次设备消息的解析结果 (兼容 sensor/status/ack 三种)
struct SensorData {
    // ---- 公共字段 ----
    std::string type;       // "sensor" / "status" / "ack"
    std::string dev_id;     // "mcu01"
    std::string msg_id;     // 消息唯一 ID
    std::string req_id;     // ACK 对应的原命令 ID
    int64_t ts = 0;         // Unix 时间戳(秒)

    // ---- 传感器值 (type=sensor) ----
    double temp = 0.0;      // 温度
    double humi = 0.0;      // 湿度
    double light = 0.0;     // 光照
    double ir = 0.0;        // 红外

    bool has_temp = false;  // 标记该字段是否存在
    bool has_humi = false;
    bool has_light = false;
    bool has_ir = false;

    // ---- 执行器状态 (type=status) ----
    bool led_on = false;        bool has_led = false;
    int  led_brightness = 0;    bool has_led_br = false;
    bool fan_on = false;        bool has_fan = false;
    int  fan_speed = 0;         bool has_fan_speed = false;
    int  fan_dir = 0;           bool has_fan_dir = false;
    int  servo_angle = 90;      bool has_servo = false;
    bool beep_on = false;       bool has_beep = false;

    // ---- ACK 回执 (type=ack) ----
    bool ack_ok = false;        // 命令是否执行成功
    bool has_ack = false;       // 是否 ACK 消息
    int  ack_code = 0;          // 错误码
    std::string ack_message;    // 回执消息
};

}  // namespace device
}  // namespace edgegw

#endif  // EDGEGW_DEVICE_SENSOR_DATA_HPP
