// ======================================================================
// device_registry.cpp - 设备状态表实现
// ======================================================================
#include "device/device_registry.hpp"
#include "device/sensor_data.hpp"

#include <cstddef>
#include <cstdio>
#include <mutex>
#include <sstream>


namespace edgegw {
namespace device{

//用一条传感器数据更新设备状态
void DeviceRegistry::UpdateSensor(const SensorData& data) {
    //1.加锁：防止并发写
    std::lock_guard<std::mutex> lock(mutex_);

    //2.找到或创建改设备的条目（operator[]不存在会自动创建）
    DeviceState& state = devices_[data.dev_id];
    state.dev_id = data.dev_id;
    state.last_seen_ms = /*用真实时钟*/0;
    state.online = true;

    //3.只更新"这条消息里有的字段"（has_xxx的用法！）
    if(data.has_temp) state.temp = data.temp;
    if(data.has_humi) state.humi = data.humi;
    if(data.has_light)state.light = data.light;
    if(data.has_ir)  state.ir = data.ir;
}

//获取设备状态
bool DeviceRegistry::Get(const std::string& dev_id, DeviceState& out)const{
        std::lock_guard<std::mutex> lock(mutex_); 
        auto it = devices_.find(dev_id);
        if (it == devices_.end()) {
            return false; //设备不存在
        }    
        out = it->second; //锁内拷贝
        return true;
}

//判断设备是否在线
bool DeviceRegistry::IsOnline(const std::string& dev_id,
                              int64_t timeout_ms)const {
    DeviceState state;
    if(!Get(dev_id, state)){
        return false; //设备不存在 = 离线
    }
    //TODO : 需要真实时钟，暂时用 last_seen 非 0 表示在线
    return state.last_seen_ms != 0;
}

//生成全部设备的最新状态 JSON
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
           << "}";
    }
    os << "]}";
    return os.str();
}

//返回设备数量
size_t DeviceRegistry::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_.size();
}

} // namespace device
} // namespace edgegw