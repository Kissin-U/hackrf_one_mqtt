#ifndef HACKRF_HANDLER_H
#define HACKRF_HANDLER_H

#include <hackrf.h>
#include <cstdint>
#include <string>
#include <atomic>

// Forward declaration for the callback type
typedef int (*hackrf_sample_block_cb_fn)(hackrf_transfer* transfer);

class HackRFHandler {
public:
    HackRFHandler();
    ~HackRFHandler();

    bool init();
    void deinit();

    bool set_frequency(uint64_t freq_hz);
    bool set_sample_rate(uint32_t rate_hz);
    bool set_baseband_filter_bandwidth(uint32_t bw_hz);
    bool set_lna_gain(uint32_t gain_db); // 0-40dB, steps of 8dB
    bool set_vga_gain(uint32_t gain_db); // 0-62dB, steps of 2dB
    bool set_amp_enable(bool enable);

    // The callback_context will be passed to the callback via transfer->rx_ctx or transfer->tx_ctx
    bool start_rx(hackrf_sample_block_cb_fn callback, void* callback_context);
    bool stop_rx();
    bool is_streaming() const;

private:
    hackrf_device* device_;
    std::atomic<bool> streaming_;
    // Store the callback context to pass it to libhackrf
    void* rx_callback_context_; 
};

#endif // HACKRF_HANDLER_H
