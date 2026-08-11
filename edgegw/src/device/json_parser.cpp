// ======================================================================
// json_parser.cpp - 用 rapidjson 解析设备上报的 JSON
//
// 输入:  MQTT 收到的 payload 字符串
// 输出:  SensorData 结构体
// 解析失败返回 false, 不抛异常(防御式编程)
// ======================================================================
#include "device/json_parser.hpp"

#include <rapidjson/document.h>

namespace edgegw {
namespace device {

namespace {

// 从 rapidjson 节点取 double 值; 不存在/类型不对返回 false
bool GetDouble(const rapidjson::Value& obj, const char* key, double& out) {
    if (!obj.IsObject()) {
        return false;
    }
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsNumber()) {
        return false;
    }
    out = it->value.GetDouble();
    return true;
}

// 从 rapidjson 节点取字符串; 不存在返回 false
bool GetString(const rapidjson::Value& obj, const char* key, std::string& out) {
    if (!obj.IsObject()) {
        return false;
    }
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsString()) {
        return false;
    }
    out = it->value.GetString();
    return true;
}

// 从 rapidjson 节点取 int64; 不存在返回 false
bool GetInt64(const rapidjson::Value& obj, const char* key, int64_t& out) {
    if (!obj.IsObject()) {
        return false;
    }
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsInt64()) {
        return false;
    }
    out = it->value.GetInt64();
    return true;
}

}  // namespace

bool ParseSensorJson(const std::string& payload, SensorData& out) {
    // 1. 解析 JSON 文档
    rapidjson::Document doc;
    doc.Parse(payload.data(), payload.size());
    if (doc.HasParseError()) {
        return false;  // JSON 语法错误
    }
    if (!doc.IsObject()) {
        return false;  // 根节点必须是对象
    }

    // 2. 顶层字段: type / dev / msg_id / ts
    if (!GetString(doc, "type", out.type) ||
        !GetString(doc, "dev", out.dev_id)) {
        return false;  // type 和 dev 是必填, 缺失直接判失败
    }
    GetString(doc, "msg_id", out.msg_id);  // msg_id 可选
    GetInt64(doc, "ts", out.ts);           // ts 可选

    // 3. body.data 子对象
    auto body_it = doc.FindMember("body");
    if (body_it == doc.MemberEnd() || !body_it->value.IsObject()) {
        return false;
    }
    const rapidjson::Value& body = body_it->value;

    auto data_it = body.FindMember("data");
    if (data_it == body.MemberEnd() || !data_it->value.IsObject()) {
        return false;
    }
    const rapidjson::Value& data = data_it->value;

    // 4. 传感器字段(可选, 缺哪个哪个 has_xxx 为 false)
    out.has_temp = GetDouble(data, "temp", out.temp);
    out.has_humi = GetDouble(data, "humi", out.humi);
    out.has_light = GetDouble(data, "light", out.light);
    out.has_ir = GetDouble(data, "ir", out.ir);

    return true;
}

}  // namespace device
}  // namespace edgegw
