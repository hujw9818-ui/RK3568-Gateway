// ======================================================================
// sensor_data.hpp - 设备上报数据模型
//
// 对应 PROTOCOL.md 的传感器上报格式:
// {
//   "type": "sensor",
//   "dev": "mcu01",
//   "msg_id": "mcu01-report-0001",
//   "ts": 1754557200,
//   "body": {
//     "data": {
//       "temp": 25.6,
//       "humi": 60.1,
//       "light": 320,
//       "ir": 0
//     }
//   }
// }
// ======================================================================
#ifndef EDGEGW_DEVICE_SENSOR_DATA_HPP
#define EDGEGW_DEVICE_SENSOR_DATA_HPP

#include <cstdint>
#include <string>

namespace edgegw {
namespace device {

// 一次传感器上报的解析结果
struct SensorData {
    std::string type;       // "sensor" / "status" / "ack" ...
    std::string dev_id;     // "mcu01"
    std::string msg_id;     // 消息唯一 ID
    int64_t ts = 0;         // Unix 时间戳(秒)

    // body.data 里的传感器值
    double temp = 0.0;      // 温度
    double humi = 0.0;      // 湿度
    double light = 0.0;     // 光照
    double ir = 0.0;        // 红外

    bool has_temp = false;  // 标记该字段是否存在(避免 0 和缺失混淆)
    bool has_humi = false;
    bool has_light = false;
    bool has_ir = false;
};

}  // namespace device
}  // namespace edgegw

#endif  // EDGEGW_DEVICE_SENSOR_DATA_HPP
