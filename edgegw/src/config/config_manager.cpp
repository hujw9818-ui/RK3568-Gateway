#include "config/config_manager.hpp"

#include <cstdlib>

namespace edgegw {
namespace config {

bool ConfigManager::Load(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);   // 一次性加载
        kv_.clear();
        Flatten(root, "");                         // 展平为 map
        return true;
    } catch (const YAML::Exception& e) {
        return false;
    }
}

// 递归展平:
//   mqtt: { host: x }  ->  "mqtt.host" = "x"
//   name: edgegw       ->  "name" = "edgegw"
void ConfigManager::Flatten(const YAML::Node& node, const std::string& prefix) {
    if (node.IsMap()) {
        // 遍历 Map 的每个键值对, 递归下钻
        for (const auto& pair : node) {
            std::string key = pair.first.as<std::string>();
            std::string full = prefix.empty() ? key : prefix + "." + key;
            Flatten(pair.second, full);
        }
    } else if (node.IsScalar()) {
        // 叶子节点: 存进 map
        kv_[prefix] = node.as<std::string>();
    }
    // 其他类型(Sequence 等)本项目暂不需要
}

std::string ConfigManager::GetString(const std::string& key,
                                     const std::string& def) const {
    auto it = kv_.find(key);
    return (it != kv_.end()) ? it->second : def;
}

int ConfigManager::GetInt(const std::string& key, int def) const {
    auto it = kv_.find(key);
    if (it == kv_.end()) {
        return def;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return def;   // 解析失败(非数字)返回默认值
    }
}

bool ConfigManager::GetBool(const std::string& key, bool def) const {
    auto it = kv_.find(key);
    if (it == kv_.end()) {
        return def;
    }
    const std::string& v = it->second;
    if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    return def;   // 无法识别返回默认值
}

}  // namespace config
}  // namespace edgegw
