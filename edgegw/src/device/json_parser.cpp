// ======================================================================
// json_parser.cpp - 用 rapidjson 解析设备上报的 JSON
//
// 输入:  MQTT 收到的 payload 字符串
// 输出:  SensorData 结构体
// 支持:  type=sensor (传感器), type=status (执行器状态), type=ack (命令回执)
// 解析失败返回 false, 不抛异常(防御式编程)
// ======================================================================
#include "device/json_parser.hpp"

#include <rapidjson/document.h>

namespace edgegw {
namespace device {

namespace {

// 从 rapidjson 节点取 double 值; 不存在/类型不对返回 false
bool GetDouble(const rapidjson::Value& obj, const char* key, double& out) {
    if (!obj.IsObject()) return false;
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsNumber()) return false;
    out = it->value.GetDouble();
    return true;
}

// 从 rapidjson 节点取 int 值; 不存在/类型不对返回 false
bool GetInt(const rapidjson::Value& obj, const char* key, int& out) {
    if (!obj.IsObject()) return false;
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsNumber()) return false;
    out = it->value.GetInt();
    return true;
}

// 从 rapidjson 节点取字符串; 不存在返回 false
bool GetString(const rapidjson::Value& obj, const char* key, std::string& out) {
    if (!obj.IsObject()) return false;
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsString()) return false;
    out = it->value.GetString();
    return true;
}

// 从 rapidjson 节点取 int64; 不存在返回 false
bool GetInt64(const rapidjson::Value& obj, const char* key, int64_t& out) {
    if (!obj.IsObject()) return false;
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsInt64()) return false;
    out = it->value.GetInt64();
    return true;
}

// 解析 body.data 子对象中的传感器字段
void ParseSensorFields(const rapidjson::Value& data, SensorData& out) {
    out.has_temp = GetDouble(data, "temp", out.temp);
    out.has_humi = GetDouble(data, "humi", out.humi);
    out.has_light = GetDouble(data, "light", out.light);
    out.has_ir = GetDouble(data, "ir", out.ir);
}

// 解析 body.data 子对象中的执行器字段 (type=status)
void ParseActuatorFields(const rapidjson::Value& data, SensorData& out) {
    int v = 0;
    if (GetInt(data, "led_on", v)) { out.led_on = (v != 0); out.has_led = true; }
    if (GetInt(data, "led_brightness", v)) { out.led_brightness = v; out.has_led_br = true; }
    if (GetInt(data, "fan_on", v)) { out.fan_on = (v != 0); out.has_fan = true; }
    if (GetInt(data, "fan_speed", v)) { out.fan_speed = v; out.has_fan_speed = true; }
    if (GetInt(data, "fan_dir", v)) { out.fan_dir = v; out.has_fan_dir = true; }
    if (GetInt(data, "servo_angle", v)) { out.servo_angle = v; out.has_servo = true; }
    if (GetInt(data, "beep_on", v)) { out.beep_on = (v != 0); out.has_beep = true; }
}

// 解析 body 中的 ACK 字段 (type=ack)
void ParseAckFields(const rapidjson::Value& body, SensorData& out) {
    out.has_ack = true;
    int v = 0;
    if (GetInt(body, "ok", v)) out.ack_ok = (v != 0);
    if (GetInt(body, "code", v)) out.ack_code = v;
    GetString(body, "message", out.ack_message);
}

}  // namespace

bool ParseSensorJson(const std::string& payload, SensorData& out) {
    // 1. 解析 JSON 文档
    rapidjson::Document doc;
    doc.Parse(payload.data(), payload.size());
    if (doc.HasParseError()) return false;
    if (!doc.IsObject()) return false;

    // 2. 顶层字段: type / dev / msg_id / ts / req_id
    if (!GetString(doc, "type", out.type) ||
        !GetString(doc, "dev", out.dev_id)) {
        return false;   // type 和 dev 必填
    }
    GetString(doc, "msg_id", out.msg_id);
    GetString(doc, "req_id", out.req_id);
    GetInt64(doc, "ts", out.ts);

    // 3. body 子对象
    auto body_it = doc.FindMember("body");
    if (body_it == doc.MemberEnd() || !body_it->value.IsObject()) {
        // ACK 消息 body 可能缺失, 但 ack 字段在顶层? 按协议在 body
        return true;   // 有 type/dev 就算基本有效
    }
    const rapidjson::Value& body = body_it->value;

    // 4. 按消息类型解析
    if (out.type == "ack") {
        ParseAckFields(body, out);
        return true;
    }

    // sensor / status: 解析 body.data
    auto data_it = body.FindMember("data");
    if (data_it == body.MemberEnd() || !data_it->value.IsObject()) {
        return true;   // 没有 data 也不算致命
    }
    const rapidjson::Value& data = data_it->value;

    ParseSensorFields(data, out);
    ParseActuatorFields(data, out);

    return true;
}

// 解析控制命令, 填入执行器字段 (乐观更新用)
bool ParseCommandJson(const std::string& payload, SensorData& out) {
    rapidjson::Document doc;
    doc.Parse(payload.data(), payload.size());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    GetString(doc, "type", out.type);
    GetString(doc, "dev", out.dev_id);
    GetString(doc, "msg_id", out.msg_id);
    GetInt64(doc, "ts", out.ts);

    auto body_it = doc.FindMember("body");
    if (body_it == doc.MemberEnd() || !body_it->value.IsObject()) return false;
    const rapidjson::Value& body = body_it->value;

    std::string target;
    if (!GetString(body, "target", target)) return false;

    auto params_it = body.FindMember("params");
    if (params_it == body.MemberEnd() || !params_it->value.IsObject()) return false;
    const rapidjson::Value& params = params_it->value;

    int v = 0;
    if (target == "led") {
        if (GetInt(params, "on", v)) { out.led_on = (v != 0); out.has_led = true; }
        if (GetInt(params, "brightness", v)) {
            out.led_brightness = v; out.has_led_br = true;
        }
    } else if (target == "fan") {
        if (GetInt(params, "on", v)) { out.fan_on = (v != 0); out.has_fan = true; }
        if (GetInt(params, "speed", v)) { out.fan_speed = v; out.has_fan_speed = true; }
        if (GetInt(params, "dir", v)) { out.fan_dir = v; out.has_fan_dir = true; }
    } else if (target == "servo") {
        if (GetInt(params, "angle", v)) { out.servo_angle = v; out.has_servo = true; }
    } else if (target == "beep") {
        if (GetInt(params, "on", v)) { out.beep_on = (v != 0); out.has_beep = true; }
    } else {
        return false;   // 未知 target
    }
    return true;
}

}  // namespace device
}  // namespace edgegw
