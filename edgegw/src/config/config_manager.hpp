#ifndef EDGEGW_CONFIG_CONGFIG_MANAGER_HPP
#define EDGEGW_CONFIG_CONGFIG_MANAGER_HPP


#include <map>
#include <string>
#include <yaml-cpp/yaml.h>

namespace edgegw {
namespace config {


class ConfigManager {
public:
    // 加载 yaml 文件，成功返回 true
    // 内部一次性把整个 yaml 展平为 key -> value 的 map
    // (key 形如 "mqtt.host", "web.port")
    bool Load(const std::string& path);

    // 取字符串值，键不存在返回默认值
    std::string GetString(const std::string& key, const std::string& def) const;

    // 取整数值，键不存在或解析失败返回默认值
    int GetInt(const std::string& key, int def) const;

    // 取布尔值
    bool GetBool(const std::string& key, bool def) const;

private:
    // 递归遍历 yaml 节点, 把嵌套结构展平成 "a.b.c" -> "value"
    void Flatten(const YAML::Node& node, const std::string& prefix);

    std::map<std::string, std::string> kv_;   // 展平后的键值对
};

}// namespace config
}// namespace edgegw
#endif
