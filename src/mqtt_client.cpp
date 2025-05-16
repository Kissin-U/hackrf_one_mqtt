#include "mqtt_client.h"
#include "logger.h" // Include our logger
#include <cstring>  // For strlen, memcpy

// The MosquittoInitializer in main.cpp handles lib_init/lib_cleanup globally.
// No need for the static counter logic here anymore.

MqttClient::MqttClient(const char* id, bool clean_session)
    : mosqpp::mosquittopp(id, clean_session),
      host_("localhost"),
      port_(1883),
      keepalive_seconds_(60),
      client_id_str_(id ? id : ""),
      connected_flag_(false),
      control_topic_qos_(0) {
    // Constructor body
}

MqttClient::~MqttClient() {
    if (connected_flag_.load()) {
        disconnect_from_broker();
    }
    // loop_stop(true) should be called if loop_start() was used.
    // It's good practice to stop the loop before the object is fully destroyed.
    // If loop_start() is consistently called in connect_to_broker,
    // then loop_stop() should be called in disconnect_from_broker or here.
    // Let's ensure loop_stop is called.
    if (is_connected()) { // Check if loop might be running
      loop_stop(true); // Force stop if it was started
    }
}

void MqttClient::set_host(const std::string& host) {
    host_ = host;
}

void MqttClient::set_port(int port) {
    port_ = port;
}

void MqttClient::set_username_password(const std::string& username, const std::string& password) {
    if (!username.empty()) {
        username_pw_set(username.c_str(), password.empty() ? nullptr : password.c_str());
    }
}

void MqttClient::set_keepalive(int keepalive) {
    keepalive_seconds_ = keepalive;
}

bool MqttClient::connect_to_broker() {
    if (connected_flag_.load()) {
        LOG_INFO("MQTT: Already connected or attempting to connect.");
        return true;
    }

    int rc = loop_start();
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("MQTT: Error starting network loop: ", mosqpp::strerror(rc));
        return false;
    }

    rc = connect_async(host_.c_str(), port_, keepalive_seconds_);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("MQTT: Error initiating connection: ", mosqpp::strerror(rc));
        loop_stop(true); // Stop loop if connect_async call fails immediately
        return false;
    }

    LOG_INFO("MQTT: Attempting to connect to ", host_, ":", port_, "...");
    return true;
}

bool MqttClient::disconnect_from_broker() {
    if (!connected_flag_.load() && !is_connected()) { // is_connected might be more robust if loop is running
        LOG_INFO("MQTT: Already disconnected.");
        // return true; // No action needed
    }
    int rc = mosquittopp::disconnect();
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("MQTT: Error disconnecting: ", mosqpp::strerror(rc));
        return false;
    }
    LOG_INFO("MQTT: Disconnect initiated.");
    // on_disconnect callback will set connected_flag_ to false and stop the loop.
    // loop_stop(true); // Call this in on_disconnect or ensure it's called if disconnect is synchronous
    return true;
}

bool MqttClient::is_connected() const {
    return connected_flag_.load();
}

void MqttClient::set_control_topic(const std::string& topic, int qos) {
    control_topic_str_ = topic;
    control_topic_qos_ = qos;
    if (connected_flag_.load() && !control_topic_str_.empty()) {
        int rc = subscribe(nullptr, control_topic_str_.c_str(), control_topic_qos_);
        if (rc != MOSQ_ERR_SUCCESS) {
            LOG_ERROR("MQTT: Error subscribing to control topic '", control_topic_str_, 
                      "' (rc: ", mosqpp::strerror(rc), ")");
        }
    }
}

void MqttClient::set_control_command_callback(std::function<void(const std::string&)> callback) {
    on_control_command_received_callback_ = callback;
}

int MqttClient::publish_message(const std::string& topic, const void* payload, int payloadlen, int qos, bool retain) {
    if (!connected_flag_.load()) {
        LOG_WARN("MQTT: Not connected. Cannot publish message to topic '", topic, "'.");
        return MOSQ_ERR_NO_CONN;
    }
    int mid_ptr;
    int rc = mosquittopp::publish(&mid_ptr, topic.c_str(), payloadlen, payload, qos, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("MQTT: Error publishing message to topic '", topic, "': ", mosqpp::strerror(rc));
    } else {
        LOG_DEBUG("MQTT: Published message to topic '", topic, "' (MID: ", mid_ptr, ")");
    }
    return rc;
}

int MqttClient::publish_message(const std::string& topic, const std::string& message, int qos, bool retain) {
    return publish_message(topic, message.c_str(), static_cast<int>(message.length()), qos, retain);
}

// --- Callbacks ---

void MqttClient::on_connect(int rc) {
    if (rc == 0) {
        LOG_INFO("MQTT: Connected to broker successfully.");
        connected_flag_ = true;
        if (!control_topic_str_.empty()) {
            int sub_rc = subscribe(nullptr, control_topic_str_.c_str(), control_topic_qos_);
            if (sub_rc != MOSQ_ERR_SUCCESS) {
                LOG_ERROR("MQTT: Error subscribing to control topic '", control_topic_str_,
                          "' on connect (rc: ", mosqpp::strerror(sub_rc), ")");
            }
        }
    } else {
        LOG_ERROR("MQTT: Connection failed: ", mosqpp::connack_string(rc));
        connected_flag_ = false;
        // loop_stop(true); // Stop the loop if connection failed.
                         // This is important as connect_async was used.
                         // The main application loop might also handle this.
    }
}

void MqttClient::on_disconnect(int rc) {
    LOG_INFO("MQTT: Disconnected from broker (rc: ", rc, "). Reason: ", mosqpp::strerror(rc));
    connected_flag_ = false;
    // loop_stop(true); // Stop the network loop as we are disconnected.
                     // This is crucial if loop_start() was called.
                     // Consider if auto-reconnect logic is added, this might change.
}

void MqttClient::on_publish(int mid) {
    LOG_DEBUG("MQTT: Message (MID: ", mid, ") published successfully.");
}

void MqttClient::on_message(const struct mosquitto_message* message) {
    if (message && message->topic) {
        std::string topic_str(message->topic);
        std::string payload_str;
        if (message->payloadlen > 0) {
            payload_str.assign(static_cast<char*>(message->payload), message->payloadlen);
        }

        LOG_DEBUG("MQTT: Message received on topic '", topic_str, "'. Payload: '", payload_str, "'");

        if (!control_topic_str_.empty() && topic_str == control_topic_str_) {
            LOG_INFO("MQTT: Control command received on topic '", topic_str, "': '", payload_str, "'");
            if (on_control_command_received_callback_) {
                try {
                    on_control_command_received_callback_(payload_str);
                } catch (const std::exception& e) {
                    LOG_ERROR("MQTT: Exception in control command callback: ", e.what());
                }
            }
        } else {
            LOG_DEBUG("MQTT: Message on non-control topic '", topic_str, "'");
        }
    }
}

void MqttClient::on_subscribe(int mid, int qos_count, const int* granted_qos) {
    std::stringstream ss;
    ss << "MQTT: Subscribed (MID: " << mid << ") with QoS levels: ";
    for (int i = 0; i < qos_count; ++i) {
        ss << granted_qos[i] << (i == qos_count - 1 ? "" : ", ");
    }
    LOG_INFO(ss.str());
}

void MqttClient::on_unsubscribe(int mid) {
    LOG_INFO("MQTT: Unsubscribed (MID: ", mid, ")");
}

void MqttClient::on_log(int level, const char* str) {
    // Map mosquitto log levels to our logger if desired, or just log them directly.
    // Mosquitto log levels: MOSQ_LOG_INFO, MOSQ_LOG_NOTICE, MOSQ_LOG_WARNING, MOSQ_LOG_ERR, MOSQ_LOG_DEBUG.
    // Our levels: DEBUG, INFO, WARNING, ERROR.
    // This callback provides logs from the mosquitto library itself.
    if (str == nullptr) return;

    switch (level) {
        case MOSQ_LOG_DEBUG:
            LOG_DEBUG("MQTT_LIB: ", str);
            break;
        case MOSQ_LOG_INFO:
        case MOSQ_LOG_NOTICE: // Treat Notice as Info
            LOG_INFO("MQTT_LIB: ", str);
            break;
        case MOSQ_LOG_WARNING:
            LOG_WARN("MQTT_LIB: ", str);
            break;
        case MOSQ_LOG_ERR:
            LOG_ERROR("MQTT_LIB: ", str);
            break;
        default:
            LOG_DEBUG("MQTT_LIB (Lvl ", level, "): ", str); // Log unknown levels as debug
            break;
    }
}

void MqttClient::on_error() {
    // This callback is not standard in mosquittopp for general errors.
    // Errors are typically handled via return codes or specific callbacks like on_disconnect.
    // If this were a custom error signal, it would be logged here.
    LOG_ERROR("MQTT: A generic client error was signaled (on_error callback).");
}
