#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <mosquittopp.h>
#include <string>
#include <vector>
#include <atomic>
#include <functional> // For std::function (command callback)
#include <mutex>      // For potential future use with shared state

class MqttClient : public mosqpp::mosquittopp {
public:
    MqttClient(const char* id, bool clean_session = true);
    ~MqttClient();

    // Setup connection parameters
    void set_host(const std::string& host);
    void set_port(int port);
    void set_username_password(const std::string& username, const std::string& password);
    void set_keepalive(int keepalive_seconds);

    // Connection management
    bool connect_to_broker();
    bool disconnect_from_broker();
    bool is_connected() const; // Our own connected flag

    // Publishing
    int publish_message(const std::string& topic, const void* payload, int payloadlen, int qos = 0, bool retain = false);
    int publish_message(const std::string& topic, const std::string& message, int qos = 0, bool retain = false);

    // For control commands (pause/resume)
    void set_control_topic(const std::string& topic, int qos = 0);
    void set_control_command_callback(std::function<void(const std::string& command_payload)> callback);

private:
    // Callbacks from mosqpp::mosquittopp
    void on_connect(int rc) override;
    void on_disconnect(int rc) override;
    void on_publish(int mid) override;
    void on_message(const struct mosquitto_message* message) override;
    void on_subscribe(int mid, int qos_count, const int* granted_qos) override;
    void on_unsubscribe(int mid) override;
    void on_log(int level, const char* str) override;
    void on_error() override; // Not a standard mosquittopp callback, but good to consider error handling

    std::string host_;
    int port_;
    int keepalive_seconds_;
    std::string client_id_str_; 

    std::atomic<bool> connected_flag_;

    // Control topic handling
    std::string control_topic_str_;
    int control_topic_qos_;
    std::function<void(const std::string& command_payload)> on_control_command_received_callback_;
    
    // For reconnect logic (can be added later)
    // int reconnect_delay_s_ = 5;
    // bool auto_reconnect_ = true;
};

#endif // MQTT_CLIENT_H
