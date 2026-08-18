// ======================================================================
// device_registry.hpp - 设备状态表
//
// 作用: 保存每个设备的最新状态(传感器+执行器), 供 /api/status 查询
// 线程安全: 内部用 std::mutex 保护, 可被 MQTT 线程/HTTP 线程同时访问
// ======================================================================
#ifndef EDGEGW_DEVICE_DEVICE_REGISTRY_HPP
#define EDGEGW_DEVICE_DEVICE_REGISTRY_HPP

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "device/sensor_data.hpp"   // 复用 SensorData, 不重复定义

namespace edgegw {
namespace device {

// 单个设备的最新状态
struct DeviceState {
    std::string dev_id;
    int64_t last_seen_ms = 0;    // 最后一次收到数据的时间(Unix ms)
    bool online = false;

    // ---- 传感器(复用 SensorData 的字段) ----
    double temp = 0.0;   bool has_temp = false;
    double humi = 0.0;   bool has_humi = false;
    double light = 0.0;  bool has_light = false;
    double ir = 0.0;     bool has_ir = false;

    // ---- 执行器状态(ESP8266 上报) ----
    bool led_on = false;        bool has_led = false;
    int  led_brightness = 0;    bool has_led_br = false;
    bool fan_on = false;        bool has_fan = false;
    int  fan_speed = 0;         bool has_fan_speed = false;
    int  fan_dir = 0;           bool has_fan_dir = false;
    int  servo_angle = 90;      bool has_servo = false;
    bool beep_on = false;       bool has_beep = false;
};

class DeviceRegistry {
public:
    // 用一条解析好的传感器数据更新设备状态 (支持 sensor/status 消息)
    void UpdateSensor(const SensorData& data);

    // 获取设备状态; 设备不存在返回 false
    bool Get(const std::string& dev_id, DeviceState& out) const;

    // 判断设备是否在线(距 last_seen_ms 超过 timeout_ms 视为离线)
    bool IsOnline(const std::string& dev_id, int64_t timeout_ms) const;

    // 生成所有设备的最新状态 JSON (给 /api/status 用)
    std::string ToJson() const;

    // 返回设备数量
    size_t Count() const;

private:
    mutable std::mutex mutex_;     // mutable: const 方法也能加锁
    std::map<std::string, DeviceState> devices_;
};

}  // namespace device
}  // namespace edgegw

#endif  // EDGEGW_DEVICE_DEVICE_REGISTRY_HPP
