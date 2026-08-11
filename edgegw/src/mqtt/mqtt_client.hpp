#ifndef EDGEGW_MQTT_MQTT_CLIENT_HPP
#define EDGEGW_MQTT_MQTT_CLIENT_HPP

// 这是网关对 libmosquitto 的一层小封装。
// 这个类只负责 MQTT 传输，不负责解析 JSON，也不负责写数据库。

#include <mosquitto.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace edgegw {
namespace mqtt {

struct Message {
  std::string topic;
  std::string payload;
  int qos = 0;
  bool retained = false;
};

class MqttClient {
 public:
  struct Options {
    std::string host = "127.0.0.1";
    int port = 1883;
    int keep_alive_seconds = 30;
    std::string client_id = "edgegw";
    std::string username;
    std::string password;
    bool clean_session = true;
  };

  using MessageHandler = std::function<void(const Message&)>;

  MqttClient();
  ~MqttClient();

  // 这个类管理底层 mosquitto 对象，因此禁止复制。
  MqttClient(const MqttClient&) = delete;
  MqttClient& operator=(const MqttClient&) = delete;

  // 启动 MQTT 客户端和后台网络线程。
  bool Start(const Options& options);

  // 停止网络线程并释放资源。
  void Stop();

  // 设置收到消息后的回调。
  // 回调运行在 libmosquitto 网络线程中，不要在里面执行耗时操作。
  void SetMessageHandler(MessageHandler handler);

  // 订阅 Topic。未连接时会先保存，连接成功后自动订阅。
  bool Subscribe(const std::string& topic, int qos = 0);

  // 发布消息。当前未连接时返回 false，不做离线缓存。
  bool Publish(const std::string& topic, const std::string& payload,
               int qos = 0, bool retain = false);

  bool IsConnected() const;

 private:
  static void OnConnect(struct mosquitto* mosq, void* userdata, int rc);
  static void OnDisconnect(struct mosquitto* mosq, void* userdata, int rc);
  static void OnMessage(struct mosquitto* mosq, void* userdata,
                        const struct mosquitto_message* message);

  void HandleConnect(int rc);
  void HandleDisconnect(int rc);
  void HandleMessage(const struct mosquitto_message* message);
  bool SubscribeAll();

  struct mosquitto* mosq_ = nullptr;
  Options options_;
  MessageHandler message_handler_;
  std::vector<std::pair<std::string, int>> subscriptions_;
  mutable std::mutex mutex_;
  std::atomic<bool> connected_{false};
  bool started_ = false;
  bool library_initialized_ = false;
};

}  // namespace mqtt
}  // namespace edgegw

#endif  // EDGEGW_MQTT_MQTT_CLIENT_HPP
