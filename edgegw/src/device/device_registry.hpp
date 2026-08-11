// ======================================================================
// device_registry.hpp - 设备状态表
//
// 作用: 保存每个设备的最新状态(传感器+执行器), 供 /api/status 查询
// 线程安全: 内部用 std::mutex 保护, 可被 MQTT 线程/HTTP 线程同时访问
// ======================================================================
#ifndef EDGEGW_DEVICE_DEVICE_REGISTRY_HPP
#define EDGEGW_DEVICE_DEVICE_REGISTRY_HPP

#include <cstdint>//
#include <map>
#include <mutex>
#include <string>

#include "device/sensor_data.hpp" //复用SensorData, 不重复定义

namespace edgegw {
namespace device {

//单个设备的最新状态
struct DeviceState {
    std::string dev_id;
    int64_t last_seen_ms = 0; //最后一次收到数据的时间（Unix ms）


}

}
}

#endif
