// ======================================================================
// device_registry.cpp - 设备状态表实现
// ======================================================================
#include "device/device_registry.hpp"

#include <cstdio>
#include <sstream>

namespace edgegw {
namespace device {

// 用一条传感器数据更新设备状态
void DeviceRegistry::UpdateSensor(const SensorData& data) {
    std::lock_guard<std::mutex> lock(mutex_);   // ① 加锁: 防止并发写

    // ② 找到或创建该设备的条目 (operator[] 不存在会自动创建)
    DeviceState& state = devices_[data.dev_id];
    state.dev_id = data.dev_id;
    state.last_seen_ms = /* TODO: 用真实时钟 */ 0;
    state.online = true;

    // ③ 只更新"这条消息里有的字段" (has_xxx 的用法!)
    //    传感器字段
    if (data.has_temp)  state.temp = data.temp;
    if (data.has_humi)  state.humi = data.humi;
    if (data.has_light) state.light = data.light;
    if (data.has_ir)    state.ir = data.ir;

    //    执行器字段 (status 消息)
    if (data.has_led)       { state.led_on = data.led_on; state.has_led = true; }
    if (data.has_led_br)    { state.led_brightness = data.led_brightness; state.has_led_br = true; }
    if (data.has_fan)       { state.fan_on = data.fan_on; state.has_fan = true; }
    if (data.has_fan_speed) { state.fan_speed = data.fan_speed; state.has_fan_speed = true; }
    if (data.has_fan_dir)   { state.fan_dir = data.fan_dir; state.has_fan_dir = true; }
    if (data.has_servo)     { state.servo_angle = data.servo_angle; state.has_servo = true; }
    if (data.has_beep)      { state.beep_on = data.beep_on; state.has_beep = true; }
}

// 获取设备状态
bool DeviceRegistry::Get(const std::string& dev_id, DeviceState& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(dev_id);
    if (it == devices_.end()) {
        return false;   // 设备不存在
    }
    out = it->second;   // 锁内拷贝
    return true;
}

// 判断设备是否在线
bool DeviceRegistry::IsOnline(const std::string& dev_id,
                              int64_t timeout_ms) const {
    DeviceState state;
    if (!Get(dev_id, state)) {
        return false;   // 设备不存在 = 离线
    }
    // TODO: 需要真实时钟, 暂时用 last_seen 非 0 表示在线
    return state.last_seen_ms != 0;
}

// 生成全部设备的最新状态 JSON
std::string DeviceRegistry::ToJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream os;
    os << "{\"devices\":[";
    bool first = true;
    for (const auto& kv : devices_) {
        const DeviceState& s = kv.second;
        if (!first) os << ",";
        first = false;

        os << "{\"dev_id\":\"" << s.dev_id << "\""
           << ",\"online\":" << (s.online ? "true" : "false")
           << ",\"temp\":" << s.temp
           << ",\"humi\":" << s.humi
           << ",\"light\":" << s.light
           << ",\"ir\":" << s.ir
           << ",\"led_on\":" << (s.led_on ? "true" : "false")
           << ",\"led_brightness\":" << s.led_brightness
           << ",\"fan_on\":" << (s.fan_on ? "true" : "false")
           << ",\"fan_speed\":" << s.fan_speed
           << ",\"fan_dir\":" << s.fan_dir
           << ",\"servo_angle\":" << s.servo_angle
           << ",\"beep_on\":" << (s.beep_on ? "true" : "false")
           << "}";
    }
    os << "]}";
    return os.str();
}

// 返回设备数量
size_t DeviceRegistry::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_.size();
}

}  // namespace device
}  // namespace edgegw
