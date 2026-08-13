// ======================================================================
// json_parser.hpp - JSON 解析接口声明
// ======================================================================
#ifndef EDGEGW_DEVICE_JSON_PARSER_HPP
#define EDGEGW_DEVICE_JSON_PARSER_HPP

#include <string>

#include "device/sensor_data.hpp"

namespace edgegw {
namespace device {

// 解析 ESP8266 上报的 JSON (兼容 sensor/status/ack 三种消息)
// 成功返回 true, 失败返回 false(不抛异常)
bool ParseSensorJson(const std::string& payload, SensorData& out);

}  // namespace device
}  // namespace edgegw

#endif  // EDGEGW_DEVICE_JSON_PARSER_HPP
