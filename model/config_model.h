#ifndef CONFIG_MODEL_H
#define CONFIG_MODEL_H

#include <string>
#include <cstdint> // For uint64_t, uint32_t
#include <nlohmann/json.hpp> // Include the nlohmann/json library

// Forward declaration for nlohmann/json if needed, or just include
// using json = nlohmann::json; // Alias for convenience

namespace hackrf_mqtt {

struct HackRFConfig {
    uint64_t center_frequency_hz = 2400000000ULL;
    uint32_t sample_rate_hz = 2000000;
    uint32_t baseband_filter_bandwidth_hz = 1750000;
    uint32_t lna_gain = 32; // IF gain
    uint32_t vga_gain = 24; // Baseband gain (RX VGA)
    // bool amp_enable = false; // Typically false for RX
};

struct MqttConfig {
    std::string broker_host = "localhost";
    int broker_port = 1883;
    std::string client_id = "usv_hackrf_transmitter";
    std::string topic = "usv/signals/hackrf_raw_iq";
    std::string control_topic = "usv/hackrf/control"; // New: Topic for control commands
    int qos = 0;
    int keepalive_s = 60;
    std::string username = ""; // Optional
    std::string password = ""; // Optional
};

struct AppConfig {
    HackRFConfig hackrf;
    MqttConfig mqtt;
    size_t data_queue_max_size = 100; 
    std::string log_level = "INFO"; // New: Logging level (e.g., "DEBUG", "INFO", "WARNING", "ERROR")
};

// --- nlohmann/json serialization/deserialization ---
// Helper macros or functions to map struct members to JSON keys.
// NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE is used for structs that don't have
// to_json/from_json member functions. It requires the members to be public.

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(HackRFConfig,
                                   center_frequency_hz,
                                   sample_rate_hz,
                                   baseband_filter_bandwidth_hz,
                                   lna_gain,
                                   vga_gain)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MqttConfig,
                                   broker_host,
                                   broker_port,
                                   client_id,
                                   topic,
                                   control_topic, // New
                                   qos,
                                   keepalive_s,
                                   username,
                                   password)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppConfig,
                                   hackrf,
                                   mqtt,
                                   data_queue_max_size,
                                   log_level) // New

} // namespace hackrf_mqtt

#endif // CONFIG_MODEL_H
