#include "mqtt/mqtt_client.hpp"

#include <algorithm>
#include <climits>
#include <iostream>
#include <utility>

namespace edgegw {
namespace mqtt {

MqttClient::MqttClient() = default;

MqttClient::~MqttClient() { Stop(); }

bool MqttClient::Start(const Options& options) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (started_) {
    return false;
  }

  if (options.host.empty() || options.port <= 0 || options.port > 65535 ||
      options.keep_alive_seconds <= 0 || options.client_id.empty()) {
    return false;
  }

  // libmosquitto 是 C 库，使用前必须初始化一次全局库状态。
  const int init_rc = mosquitto_lib_init();
  if (init_rc != MOSQ_ERR_SUCCESS) {
    return false;
  }
  library_initialized_ = true;

  options_ = options;
  mosq_ = mosquitto_new(options_.client_id.c_str(), options_.clean_session,
                        this);
  if (mosq_ == nullptr) {
    mosquitto_lib_cleanup();
    library_initialized_ = false;
    return false;
  }

  // 用户名和密码为空时不调用此函数，适配实验环境的匿名 MQTT。
  if (!options_.username.empty()) {
    const int auth_rc = mosquitto_username_pw_set(
        mosq_, options_.username.c_str(), options_.password.c_str());
    if (auth_rc != MOSQ_ERR_SUCCESS) {
      mosquitto_destroy(mosq_);
      mosq_ = nullptr;
      mosquitto_lib_cleanup();
      library_initialized_ = false;
      return false;
    }
  }

  mosquitto_connect_callback_set(mosq_, &MqttClient::OnConnect);
  mosquitto_disconnect_callback_set(mosq_, &MqttClient::OnDisconnect);
  mosquitto_message_callback_set(mosq_, &MqttClient::OnMessage);

  // 使用异步连接，真正的网络收发由 loop_start 创建的线程负责。
  const int connect_rc = mosquitto_connect_async(
      mosq_, options_.host.c_str(), options_.port,
      options_.keep_alive_seconds);
  if (connect_rc != MOSQ_ERR_SUCCESS) {
    mosquitto_destroy(mosq_);
    mosq_ = nullptr;
    mosquitto_lib_cleanup();
    library_initialized_ = false;
    return false;
  }

  const int loop_rc = mosquitto_loop_start(mosq_);
  if (loop_rc != MOSQ_ERR_SUCCESS) {
    mosquitto_destroy(mosq_);
    mosq_ = nullptr;
    mosquitto_lib_cleanup();
    library_initialized_ = false;
    return false;
  }

  started_ = true;
  return true;
}

void MqttClient::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (mosq_ == nullptr) {
    return;
  }

  connected_.store(false);

  if (started_) {
    // false 表示让网络线程自然退出，函数会等待线程结束。
    mosquitto_disconnect(mosq_);
    mosquitto_loop_stop(mosq_, false);
    started_ = false;
  }

  mosquitto_destroy(mosq_);
  mosq_ = nullptr;

  if (library_initialized_) {
    mosquitto_lib_cleanup();
    library_initialized_ = false;
  }
}

void MqttClient::SetMessageHandler(MessageHandler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  message_handler_ = std::move(handler);
}

bool MqttClient::Subscribe(const std::string& topic, int qos) {
  if (topic.empty() || qos < 0 || qos > 2) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  const auto already_exists = std::find_if(
      subscriptions_.begin(), subscriptions_.end(),
      [&](const std::pair<std::string, int>& item) {
        return item.first == topic;
      });
  if (already_exists == subscriptions_.end()) {
    subscriptions_.emplace_back(topic, qos);
  }

  // 未连接时只保存订阅，OnConnect 会自动补订阅。
  if (!started_ || !connected_.load() || mosq_ == nullptr) {
    return true;
  }

  const int rc = mosquitto_subscribe(mosq_, nullptr, topic.c_str(), qos);
  return rc == MOSQ_ERR_SUCCESS;
}

bool MqttClient::Publish(const std::string& topic, const std::string& payload,
                         int qos, bool retain) {
  if (topic.empty() || qos < 0 || qos > 2 || payload.size() > INT_MAX) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!started_ || !connected_.load() || mosq_ == nullptr) {
    return false;
  }

  const int rc = mosquitto_publish(
      mosq_, nullptr, topic.c_str(), static_cast<int>(payload.size()),
      payload.data(), qos, retain);
  return rc == MOSQ_ERR_SUCCESS;
}

bool MqttClient::IsConnected() const { return connected_.load(); }

void MqttClient::OnConnect(struct mosquitto* /*mosq*/, void* userdata, int rc) {
  auto* client = static_cast<MqttClient*>(userdata);
  if (client != nullptr) {
    client->HandleConnect(rc);
  }
}

void MqttClient::OnDisconnect(struct mosquitto* /*mosq*/, void* userdata,
                              int rc) {
  auto* client = static_cast<MqttClient*>(userdata);
  if (client != nullptr) {
    client->HandleDisconnect(rc);
  }
}

void MqttClient::OnMessage(struct mosquitto* /*mosq*/, void* userdata,
                           const struct mosquitto_message* message) {
  auto* client = static_cast<MqttClient*>(userdata);
  if (client != nullptr) {
    client->HandleMessage(message);
  }
}

void MqttClient::HandleConnect(int rc) {
  if (rc != MOSQ_ERR_SUCCESS) {
    connected_.store(false);
    std::cerr << "MQTT connect failed, rc=" << rc << '\n';
    return;
  }

  connected_.store(true);
  std::cout << "MQTT connected to " << options_.host << ':' << options_.port
            << '\n';
  SubscribeAll();
}

void MqttClient::HandleDisconnect(int rc) {
  connected_.store(false);
  if (rc != MOSQ_ERR_SUCCESS) {
    std::cerr << "MQTT disconnected unexpectedly, rc=" << rc << '\n';
  } else {
    std::cout << "MQTT disconnected\n";
  }
}

bool MqttClient::SubscribeAll() {
  std::vector<std::pair<std::string, int>> subscriptions;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions = subscriptions_;
  }

  bool success = true;
  for (const auto& subscription : subscriptions) {
    const int rc = mosquitto_subscribe(mosq_, nullptr, subscription.first.c_str(),
                                       subscription.second);
    if (rc != MOSQ_ERR_SUCCESS) {
      success = false;
      std::cerr << "MQTT subscribe failed, topic=" << subscription.first
                << ", rc=" << rc << '\n';
    }
  }
  return success;
}

void MqttClient::HandleMessage(
    const struct mosquitto_message* message) {
  if (message == nullptr || message->topic == nullptr ||
      message->payload == nullptr || message->payloadlen < 0) {
    return;
  }

  MessageHandler handler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    handler = message_handler_;
  }

  if (!handler) {
    return;
  }

  Message received;
  received.topic = message->topic;
  received.payload.assign(static_cast<const char*>(message->payload),
                          static_cast<std::size_t>(message->payloadlen));
  received.qos = message->qos;
  received.retained = message->retain != 0;

  // 注意：这里仍然运行在 MQTT 网络线程。
  // 后续接入设备表和 SQLite 时，应在这里把 received 放进业务队列，
  // 不要直接执行耗时的 JSON 解析或数据库写入。
  handler(received);
}

}  // namespace mqtt
}  // namespace edgegw
