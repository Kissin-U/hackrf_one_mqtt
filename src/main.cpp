// #include <iostream> // Replaced by logger
#include <vector>
#include <string>
#include "logger.h" // Include our new logger
#include <csignal>
#include <thread>
#include <chrono>
#include <fstream> // For reading config file
#include <nlohmann/json.hpp> // For JSON parsing
#include "hackrf_handler.h" 
#include "mqtt_client.h"  
#include "config_model.h" // Our config structure
#include <mosquittopp.h>

volatile sig_atomic_t keep_running = 1;

#include "thread_safe_queue.h"

// Define a type for our data queue
using DataQueue = hackrf_mqtt::ThreadSafeQueue<std::vector<unsigned char>>;


void signal_handler(int signal_num) {
    if (signal_num == SIGINT || signal_num == SIGTERM) {
        keep_running = 0;
        LOG_INFO("\nSignal ", signal_num, " received, shutting down...");
    }
}

// HackRF callback function: Pushes data to the queue
int hackrf_rx_callback(hackrf_transfer* transfer) {
    if (!keep_running) { // Check global flag
        return -1; // Signal libhackrf to stop streaming
    }

    DataQueue* data_queue_ptr = static_cast<DataQueue*>(transfer->rx_ctx);

    if (data_queue_ptr && transfer->valid_length > 0) {
        std::vector<unsigned char> data_chunk(transfer->buffer, transfer->buffer + transfer->valid_length);
        if (!data_queue_ptr->try_push(std::move(data_chunk))) {
            // Queue is full, data chunk was discarded.
            // TODO: Implement rate-limited logging for this warning to avoid console spam.
            LOG_WARN("IQ data queue full, discarding data chunk of size ", transfer->valid_length, " bytes.");
        }
    }
    return 0; // Continue streaming
}

// MQTT Publisher Thread function
void mqtt_publisher_thread_func(
    DataQueue& data_queue, 
    MqttClient& mqtt_client, 
    const hackrf_mqtt::MqttConfig& mqtt_config,
    const std::atomic<bool>& should_run
) {
    LOG_INFO("MQTT Publisher thread started.");
    while (should_run.load()) {
        std::optional<std::vector<unsigned char>> data_chunk_opt = 
            data_queue.wait_for_and_pop(std::chrono::milliseconds(100));

        if (data_chunk_opt) {
            std::vector<unsigned char>& data_chunk = *data_chunk_opt;
            if (mqtt_client.is_connected()) {
                int rc = mqtt_client.publish_message(
                    mqtt_config.topic,
                    data_chunk.data(),
                    static_cast<int>(data_chunk.size()),
                    mqtt_config.qos
                );
                if (rc != MOSQ_ERR_SUCCESS) {
                    LOG_ERROR("MQTT Publish error in publisher thread: ", mosqpp::strerror(rc));
                    if (rc == MOSQ_ERR_NO_CONN || rc == MOSQ_ERR_CONN_LOST) {
                        LOG_WARN("MQTT disconnected, publisher thread may pause.");
                    }
                }
            } else {
                LOG_DEBUG("MQTT not connected in publisher thread, discarding data chunk if any was popped.");
            }
        }
    }
    LOG_INFO("MQTT Publisher thread stopping.");
}

class MosquittoInitializer {
public:
    MosquittoInitializer() {
        mosqpp::lib_init();
        // This initial log might happen before logger is configured by main's AppConfig
        // So it will use the default LogLevel::INFO.
        hackrf_mqtt::logger::log(hackrf_mqtt::logger::LogLevel::INFO, "INFO", "Mosquitto library initialized by MosquittoInitializer.");
    }
    ~MosquittoInitializer() {
        mosqpp::lib_cleanup();
        hackrf_mqtt::logger::log(hackrf_mqtt::logger::LogLevel::INFO, "INFO", "Mosquitto library cleaned up by MosquittoInitializer.");
    }
};


int main(int argc, char* argv[]) {
    MosquittoInitializer mosq_initializer;
    hackrf_mqtt::AppConfig app_config;

    // --- Load Configuration from JSON ---
    std::string config_file_path = "config.json";
    std::ifstream config_file_stream(config_file_path);
    if (config_file_stream.is_open()) {
        try {
            nlohmann::json json_config = nlohmann::json::parse(config_file_stream);
            app_config = json_config.get<hackrf_mqtt::AppConfig>();
            // Initialize logger here, after config is loaded
            hackrf_mqtt::logger::init(app_config.log_level);
            LOG_INFO("Configuration loaded from ", config_file_path);
        } catch (const nlohmann::json::parse_error& e) {
            // Logger might not be initialized with desired level yet, use default or direct cerr
            std::cerr << "Error parsing config file " << config_file_path << ": " << e.what() << std::endl;
            std::cerr << "Using default configuration and INFO log level." << std::endl;
            hackrf_mqtt::logger::init(hackrf_mqtt::logger::LogLevel::INFO); // Ensure logger is init'd
            LOG_ERROR("Error parsing config file ", config_file_path, ": ", e.what());
            LOG_INFO("Using default configuration.");
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "Error processing config file " << config_file_path << ": " << e.what() << std::endl;
            std::cerr << "Using default configuration and INFO log level." << std::endl;
            hackrf_mqtt::logger::init(hackrf_mqtt::logger::LogLevel::INFO);
            LOG_ERROR("Error processing config file ", config_file_path, ": ", e.what());
            LOG_INFO("Using default configuration.");
        }
        config_file_stream.close();
    } else {
        hackrf_mqtt::logger::init(app_config.log_level); // Use default log_level from AppConfig
        LOG_INFO("Config file ", config_file_path, " not found. Using default configuration.");
    }
    // --- End Load Configuration ---

    LOG_INFO("HackRF MQTT Transmitter starting...");
    LOG_INFO("Log level set to: ", app_config.log_level);
    LOG_INFO("MQTT Broker: ", app_config.mqtt.broker_host, ":", app_config.mqtt.broker_port);
    LOG_INFO("MQTT Topic: ", app_config.mqtt.topic);
    LOG_INFO("HackRF Frequency: ", app_config.hackrf.center_frequency_hz / 1e6, " MHz");
    LOG_INFO("HackRF Sample Rate: ", app_config.hackrf.sample_rate_hz / 1e6, " MS/s");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    HackRFHandler hackrf_handler;
    MqttClient mqtt_client(app_config.mqtt.client_id.c_str(), true); 

    mqtt_client.set_host(app_config.mqtt.broker_host);
    mqtt_client.set_port(app_config.mqtt.broker_port);
    mqtt_client.set_keepalive(app_config.mqtt.keepalive_s);
    if (!app_config.mqtt.username.empty()) {
        mqtt_client.set_username_password(app_config.mqtt.username, app_config.mqtt.password);
    }

    DataQueue iq_data_queue(app_config.data_queue_max_size);
    LOG_INFO("IQ Data Queue initialized with max size: ", (app_config.data_queue_max_size == 0 ? "UNBOUNDED" : std::to_string(app_config.data_queue_max_size)));

    std::atomic<bool> publisher_should_run(true);
    std::atomic<bool> hackrf_should_be_streaming(true);

    auto control_command_handler =
        [&](const std::string& payload, HackRFHandler& handler, DataQueue& queue) {
        LOG_INFO("Control command received: '", payload, "'");
        if (payload == "PAUSE") {
            if (hackrf_should_be_streaming.load() && handler.is_streaming()) {
                LOG_INFO("Pausing HackRF stream via MQTT command.");
                handler.stop_rx();
                hackrf_should_be_streaming = false;
            } else {
                LOG_INFO("HackRF already paused or not streaming. 'PAUSE' command ignored.");
            }
        } else if (payload == "RESUME") {
            if (!hackrf_should_be_streaming.load() && !handler.is_streaming()) {
                LOG_INFO("Resuming HackRF stream via MQTT command.");
                if (handler.start_rx(hackrf_rx_callback, &queue)) {
                    hackrf_should_be_streaming = true;
                } else {
                    LOG_ERROR("Failed to resume HackRF stream via MQTT command.");
                }
            } else {
                LOG_INFO("HackRF already streaming or not in a state to resume. 'RESUME' command ignored.");
            }
        } else {
            LOG_WARN("Unknown control command received: '", payload, "'");
        }
    };

    if (!app_config.mqtt.control_topic.empty()) {
        mqtt_client.set_control_command_callback(
            [&](const std::string& payload) {
                control_command_handler(payload, hackrf_handler, iq_data_queue);
            }
        );
        mqtt_client.set_control_topic(app_config.mqtt.control_topic, app_config.mqtt.qos);
        LOG_INFO("MQTT control enabled. Subscribed to topic: ", app_config.mqtt.control_topic);
    }


    // Start MQTT Publisher Thread
    std::thread publisher_thread(
        mqtt_publisher_thread_func, 
        std::ref(iq_data_queue), 
        std::ref(mqtt_client), 
        std::cref(app_config.mqtt), // Pass MqttConfig by const reference
        std::ref(publisher_should_run)
    );


    try {
        LOG_INFO("Initializing HackRF...");
        if (!hackrf_handler.init()) {
            LOG_ERROR("Failed to initialize HackRF.");
            publisher_should_run = false;
            if(publisher_thread.joinable()) publisher_thread.join();
            return 1;
        }

        hackrf_handler.set_frequency(app_config.hackrf.center_frequency_hz);
        hackrf_handler.set_sample_rate(app_config.hackrf.sample_rate_hz);
        hackrf_handler.set_baseband_filter_bandwidth(app_config.hackrf.baseband_filter_bandwidth_hz);
        
        LOG_INFO("Setting LNA gain to: ", app_config.hackrf.lna_gain, " dB");
        hackrf_handler.set_lna_gain(app_config.hackrf.lna_gain);
        LOG_INFO("Setting VGA gain to: ", app_config.hackrf.vga_gain, " dB");
        hackrf_handler.set_vga_gain(app_config.hackrf.vga_gain);
        
        LOG_INFO("Connecting to MQTT broker...");
        if (!mqtt_client.connect_to_broker()) {
            LOG_ERROR("Failed to initiate MQTT connection.");
            hackrf_handler.deinit();
            return 1;
        }
        
        int connect_timeout_ms = 5000;
        int time_waited_ms = 0;
        while(!mqtt_client.is_connected() && time_waited_ms < connect_timeout_ms && keep_running == 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            time_waited_ms += 100;
        }

        if (!mqtt_client.is_connected()) {
            LOG_ERROR("MQTT connection timed out or failed.");
            hackrf_handler.deinit();
            publisher_should_run = false;
            if(publisher_thread.joinable()) publisher_thread.join();
            return 1;
        }
        
        LOG_INFO("Attempting to start HackRF stream initially...");
        if (hackrf_should_be_streaming.load()) {
            if (!hackrf_handler.start_rx(hackrf_rx_callback, &iq_data_queue)) {
                LOG_ERROR("Failed to start HackRF stream initially.");
                if (mqtt_client.is_connected()) mqtt_client.disconnect_from_broker();
                hackrf_handler.deinit();
                publisher_should_run = false; 
                if (publisher_thread.joinable()) publisher_thread.join();
                return 1;
            }
            LOG_INFO("HackRF stream started. Send 'PAUSE'/'RESUME' to '", app_config.mqtt.control_topic, "' to control.");
        } else {
            LOG_INFO("HackRF initially set to not stream.");
        }
        
        while (keep_running == 1 ) {
            if (!mqtt_client.is_connected()) {
                LOG_ERROR("MQTT client disconnected. Shutting down application.");
                keep_running = 0;
                break;
            }
            // The main loop can perform other periodic tasks if needed.
            // For now, it primarily keeps the application alive while other threads work.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

    } catch (const std::exception& e) { 
        LOG_ERROR("An unhandled std::exception occurred in main: ", e.what());
        keep_running = 0; 
    } catch (...) { 
        LOG_ERROR("An unknown exception occurred in main.");
        keep_running = 0; 
    }

    LOG_INFO("Shutting down...");
    publisher_should_run = false;
    if (publisher_thread.joinable()) {
        LOG_INFO("Waiting for MQTT publisher thread to finish...");
        publisher_thread.join();
        LOG_INFO("MQTT publisher thread finished.");
    }

    if (hackrf_handler.is_streaming()) {
        LOG_INFO("Stopping HackRF stream...");
        hackrf_handler.stop_rx();
    }
    LOG_INFO("Deinitializing HackRF...");
    hackrf_handler.deinit(); 

    if (mqtt_client.is_connected()) {
        LOG_INFO("Disconnecting from MQTT broker...");
        mqtt_client.disconnect_from_broker();
    }
    
    LOG_INFO("HackRF MQTT Transmitter finished.");
    return 0;
}
