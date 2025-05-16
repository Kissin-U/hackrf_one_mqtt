#include "hackrf_handler.h"
#include "logger.h" // Include our logger

HackRFHandler::HackRFHandler() : device_(nullptr), streaming_(false), rx_callback_context_(nullptr) {
}

HackRFHandler::~HackRFHandler() {
    deinit(); // Ensure resources are released
}

bool HackRFHandler::init() {
    if (device_) {
        LOG_WARN("HackRF already initialized.");
        return true; // Or false, depending on desired behavior
    }
    int result = hackrf_init();
    if (result != HACKRF_SUCCESS) {
        LOG_ERROR("Failed to initialize HackRF library: ", hackrf_error_name((hackrf_error)result));
        return false;
    }

    result = hackrf_open(&device_);
    if (result != HACKRF_SUCCESS || device_ == nullptr) {
        LOG_ERROR("Failed to open HackRF device: ", hackrf_error_name((hackrf_error)result));
        hackrf_exit(); // Clean up library initialization if open fails
        device_ = nullptr;
        return false;
    }
    LOG_INFO("HackRF device opened successfully.");
    return true;
}

void HackRFHandler::deinit() {
    if (streaming_) {
        stop_rx();
    }
    if (device_) {
        hackrf_close(device_);
        device_ = nullptr;
        LOG_INFO("HackRF device closed.");
    }
    // hackrf_exit should ideally be called when the application is completely done with libhackrf.
    // Calling it here might be too soon if other handlers exist, but for a single handler app, it's okay.
    // If multiple HackRFHandler instances could exist, manage hackrf_init/exit globally.
    hackrf_exit(); 
    LOG_INFO("HackRF library deinitialized.");
}

bool HackRFHandler::set_frequency(uint64_t freq_hz) {
    if (!device_) {
        LOG_ERROR("HackRF device not initialized. Cannot set frequency.");
        return false;
    }
    int result = hackrf_set_freq(device_, freq_hz);
    if (result != HACKRF_SUCCESS) {
        LOG_ERROR("Failed to set frequency: ", hackrf_error_name((hackrf_error)result));
        return false;
    }
    LOG_INFO("HackRF frequency set to ", freq_hz / 1e6, " MHz.");
    return true;
}

bool HackRFHandler::set_sample_rate(uint32_t rate_hz) {
    if (!device_) {
        LOG_ERROR("HackRF device not initialized. Cannot set sample rate.");
        return false;
    }
    // libhackrf expects sample rate as a double for hackrf_set_sample_rate_manual
    // but we'll use hackrf_set_sample_rate which takes uint32_t for common rates
    // For more fine-grained control, one might use hackrf_set_sample_rate_manual.
    int result = hackrf_set_sample_rate(device_, rate_hz);
    if (result != HACKRF_SUCCESS) {
        LOG_ERROR("Failed to set sample rate: ", hackrf_error_name((hackrf_error)result));
        return false;
    }
    LOG_INFO("HackRF sample rate set to ", rate_hz / 1e6, " MS/s.");
    return true;
}

bool HackRFHandler::set_baseband_filter_bandwidth(uint32_t bw_hz) {
    if (!device_) {
        LOG_ERROR("HackRF device not initialized. Cannot set baseband filter bandwidth.");
        return false;
    }
    int result = hackrf_set_baseband_filter_bandwidth(device_, bw_hz);
    if (result != HACKRF_SUCCESS) {
        LOG_ERROR("Failed to set baseband filter bandwidth: ", hackrf_error_name((hackrf_error)result));
        return false;
    }
    LOG_INFO("HackRF baseband filter bandwidth set to ", bw_hz / 1e6, " MHz.");
    return true;
}

bool HackRFHandler::set_lna_gain(uint32_t gain_db) {
    if (!device_) {
        LOG_ERROR("HackRF device not initialized. Cannot set LNA gain.");
        return false;
    }
    int result = hackrf_set_lna_gain(device_, gain_db);
    if (result != HACKRF_SUCCESS) {
        LOG_ERROR("Failed to set LNA gain: ", hackrf_error_name((hackrf_error)result));
        return false;
    }
    LOG_INFO("HackRF LNA gain set to ", gain_db, " dB.");
    return true;
}

bool HackRFHandler::set_vga_gain(uint32_t gain_db) {
    if (!device_) {
        LOG_ERROR("HackRF device not initialized. Cannot set VGA gain.");
        return false;
    }
    int result = hackrf_set_vga_gain(device_, gain_db);
    if (result != HACKRF_SUCCESS) {
        LOG_ERROR("Failed to set VGA gain: ", hackrf_error_name((hackrf_error)result));
        return false;
    }
    LOG_INFO("HackRF VGA gain set to ", gain_db, " dB.");
    return true;
}

bool HackRFHandler::set_amp_enable(bool enable) {
    if (!device_) {
        LOG_ERROR("HackRF device not initialized. Cannot set amp enable.");
        return false;
    }
    int result = hackrf_set_amp_enable(device_, enable ? 1 : 0);
    if (result != HACKRF_SUCCESS) {
        LOG_ERROR("Failed to set amp enable: ", hackrf_error_name((hackrf_error)result));
        return false;
    }
    LOG_INFO("HackRF amplifier ", (enable ? "enabled." : "disabled."));
    return true;
}

bool HackRFHandler::start_rx(hackrf_sample_block_cb_fn callback, void* callback_context) {
    if (!device_) {
        LOG_ERROR("HackRF device not initialized. Cannot start RX.");
        return false;
    }
    if (streaming_) {
        LOG_WARN("HackRF is already streaming. Start RX request ignored.");
        return false; // Or true if already streaming is not an error for the caller
    }
    rx_callback_context_ = callback_context; // Store context for the C callback
    int result = hackrf_start_rx(device_, callback, rx_callback_context_);
    if (result != HACKRF_SUCCESS) {
        LOG_ERROR("Failed to start RX streaming: ", hackrf_error_name((hackrf_error)result));
        return false;
    }
    streaming_ = true;
    LOG_INFO("HackRF RX streaming started.");
    return true;
}

bool HackRFHandler::stop_rx() {
    if (!device_ || !streaming_) {
        LOG_WARN("HackRF not streaming or not initialized. Stop RX request ignored.");
        return false; // Or true if not streaming is considered success for the caller
    }
    int result = hackrf_stop_rx(device_);
    if (result != HACKRF_SUCCESS) {
        LOG_ERROR("Failed to stop RX streaming: ", hackrf_error_name((hackrf_error)result));
        // Even if stopping fails, we should probably update our state
        streaming_ = false; 
        return false;
    }
    streaming_ = false;
    rx_callback_context_ = nullptr;
    LOG_INFO("HackRF RX streaming stopped.");
    return true;
}

bool HackRFHandler::is_streaming() const {
    if (!device_) return false;
    // hackrf_is_streaming() checks the actual device state.
    // Our streaming_ flag is an internal indicator.
    // It's good practice to rely on the library's check if available and appropriate.
    return hackrf_is_streaming(device_) == HACKRF_TRUE;
    // Or simply: return streaming_.load(); if we trust our flag management.
}
