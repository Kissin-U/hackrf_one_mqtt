# HackRF MQTT Data Transmitter for Unmanned Surface Vehicle (USV) Signal Acquisition

This project is designed to capture radio frequency (RF) data, such as 2.4 GHz remote control signals from other USVs, using a HackRF One device. The captured data is then transmitted over MQTT using a Mosquitto client (or any compatible MQTT broker). This enables real-time or near real-time analysis and monitoring of the RF environment around the USV.

## Prerequisites

-   A C++ compiler (supporting C++17 or later)
-   CMake (version 3.10 or later)
-   libhackrf (HackRF library)
-   libmosquittopp (Mosquitto C++ client library, which depends on libmosquitto)

## Build Instructions

1.  **Clone the repository (if applicable) or ensure you have the source code.**
2.  **Create a build directory:**
    ```bash
    mkdir build
    cd build
    ```
3.  **Run CMake:**
    ```bash
    cmake ..
    ```
    If `libhackrf` or `libmosquittopp` are installed in non-standard locations and `pkg-config` cannot find them, you might need to set environment variables like `PKG_CONFIG_PATH` or directly provide hints to CMake (though `pkg_check_modules` is preferred).
    Ensure `libmosquittopp-dev` (or equivalent for your distribution) is installed. This package usually provides the necessary headers, libraries, and `.pc` files for `pkg-config`.

4.  **Build the project:**
    ```bash
    make
    ```
    (Or `ninja` if you used the Ninja generator with CMake)

## Usage

After building, the executable `hackrf_mqtt_transmitter` will be located in the `build` directory.

```bash
./hackrf_mqtt_transmitter [options]
```
Currently, options are hardcoded in `src/main.cpp`. Future work could involve adding command-line arguments for frequency, sample rate, MQTT settings, etc.

### Understanding Key Parameters for 2.4 GHz Signal Acquisition

The effectiveness of capturing 2.4 GHz signals (like RC remotes, Wi-Fi, Bluetooth) heavily depends on the correct configuration of HackRF parameters in `src/main.cpp`:

-   **`FREQUENCY_HZ` (Center Frequency)**:
    -   Currently set to `2400000000ULL` (2.4 GHz) in `src/main.cpp`.
    -   The 2.4 GHz ISM band (approx. 2.400-2.4835 GHz) is where many MAVLink-based RC systems operate.
    -   If you know the specific channel or frequency range your target MAVLink system uses, adjust this value accordingly. MAVLink itself is a protocol; the underlying radio link determines the exact frequency.

-   **`SAMPLE_RATE_HZ` (Sample Rate)**:
    -   Currently set to `2000000` (2 MS/s) in `src/main.cpp`. This is HackRF One's lowest officially supported sample rate.
    -   MAVLink telemetry radios (like SiK-based ones) often use relatively narrow bandwidths (e.g., 50-250 kHz per channel). A 2 MS/s sample rate is generally sufficient to capture these signals, providing an observable bandwidth of up to ~1.75-2MHz (limited by the filter).
    -   Using a lower sample rate reduces the amount of data generated significantly.

-   **`BASEBAND_FILTER_BANDWIDTH_HZ` (Baseband Filter Bandwidth)**:
    -   Currently set to `1750000` (1.75 MHz) in `src/main.cpp`. `libhackrf` will select the closest available hardware filter bandwidth to this value (e.g., 1.75 MHz, 2.5 MHz).
    -   This should be wide enough to capture the MAVLink signal's bandwidth, plus some margin, but not excessively wide to include unnecessary noise. For a MAVLink signal with a ~250 kHz bandwidth, a 1.75 MHz filter is generous but acceptable. You could experiment with lower values if your target signal is known to be narrower and you want to improve the Signal-to-Noise Ratio (SNR), ensuring it's still wider than the signal itself.

-   **LNA Gain (Low Noise Amplifier)** and **VGA Gain (Variable Gain Amplifier)**:
    -   These are commented out in `main.cpp` (e.g., `// hackrf_handler.set_lna_gain(32);`).
    -   **Crucial for good reception.** These are now active in `src/main.cpp` with initial values (LNA: 32dB, VGA: 24dB) but **must be tuned** based on your specific RF environment and signal strength.
    -   LNA gain (IF gain, 0-40 dB, steps of 8) amplifies weak signals.
    -   VGA gain (Baseband gain, 0-62 dB, steps of 2) further amplifies.
    -   Experiment to find values that provide a clear signal without saturating the receiver.

-   **Challenges with MAVLink and 2.4 GHz Telemetry**:
    -   **Frequency Hopping (FHSS)**: Many MAVLink telemetry systems (especially SiK-based radios) use FHSS, where the radio hops between many channels across a portion of the 2.4 GHz band. Capturing a complete FHSS transmission with a fixed-tuned receiver like this setup is difficult. You will likely only capture bursts of data when the transmitter hops onto the frequency (and within the bandwidth) you are currently monitoring.
    -   **Decoding MAVLink**: This program transmits **raw IQ data**. It does **not** demodulate the signal or decode MAVLink packets. To get actual MAVLink messages, you would need to implement or integrate a separate signal processing chain (demodulator for FSK/GFSK, etc.) and a MAVLink parser on the receiving end of the MQTT data, or directly within this C++ application before sending via MQTT (which would be more efficient).

-   **Data Transmission**:
    -   The current implementation sends raw IQ samples. At 2 MS/s (4 MB/s), this is still a significant data rate for continuous streaming over some networks.
    -   For practical USV applications, especially if MAVLink messages are the goal, consider:
        -   **On-board processing**: Implement MAVLink demodulation and decoding on the device running this code. Then, send only the decoded MAVLink messages (much smaller data) over MQTT. This is the recommended approach for efficiency.
        -   **Signal detection**: Only transmit IQ data when a potential MAVLink signal is detected.
        -   **Data compression** if raw IQ is still needed.

The current settings (2.4 GHz center, 2 MS/s sample rate, 1.75 MHz bandwidth) provide a more targeted baseline for capturing MAVLink-like signals compared to wider band settings. However, successful capture and use will require careful tuning of gains and an understanding of the limitations, especially concerning FHSS and the need for separate MAVLink decoding.

## Project Structure

-   `include/`: Contains the public header files.
    -   `hackrf_handler.h`: Header for HackRF device interaction class.
    -   `mqtt_client.h`: Header for MQTT client communication class.
-   `src/`: Contains the source code implementation files.
    -   `main.cpp`: Main application entry point, orchestrates HackRF and MQTT operations.
    -   `hackrf_handler.cpp`: Implementation for HackRF device interaction.
    -   `mqtt_client.cpp`: Implementation for MQTT client communication.
-   `CMakeLists.txt`: CMake build script.
-   `README.md`: This file.

## Dependencies

### libhackrf

This library is required to interface with the HackRF One device.
Installation instructions can be found on the [official HackRF repository](https://github.com/greatscottgadgets/hackrf).

On Debian/Ubuntu-based systems:
```bash
sudo apt-get install libhackrf-dev
```

### Mosquitto Client Libraries (libmosquitto and libmosquittopp)

This library is used for MQTT communication. It provides both C (`libmosquitto`) and C++ (`libmosquittopp`) bindings.
The `CMakeLists.txt` in this project uses `pkg-config` to find `libmosquittopp`, which typically also links against `libmosquitto`.

**Installation on Debian/Ubuntu-based systems (like Jetson Nano's L4T):**
```bash
sudo apt-get update
sudo apt-get install libmosquitto-dev libmosquittopp-dev
```
This will install the necessary headers, libraries, and pkg-config files.

**Installation from source (if needed or for other systems):**
You can compile Mosquitto from source. Instructions are available on the [official Mosquitto website](https://mosquitto.org/download/). When compiling from source, ensure that both the C library and the C++ wrapper are built and installed correctly.

Ensure `ldconfig` is run if necessary after installing shared libraries from source to a custom location.
```bash
# sudo ldconfig (if installed to standard system paths, apt usually handles this)
