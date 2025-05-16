cmake_minimum_required(VERSION 3.10)
project(HackRF_MQTT_Transmitter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- FetchContent for nlohmann/json ---
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3 # Or latest stable release
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)
# --------------------------------------

# Find HackRF library
find_package(PkgConfig REQUIRED)
pkg_check_modules(HACKRF REQUIRED libhackrf)

# Find Mosquitto C++ library (libmosquittopp)
# Ensure libmosquittopp-dev (or equivalent) is installed for pkg-config to find it.
pkg_check_modules(MOSQUITTO_CPP REQUIRED libmosquittopp)

if(NOT HACKRF_FOUND)
    message(FATAL_ERROR "libhackrf not found. Please install it or specify HACKRF_INCLUDE_DIR and HACKRF_LIBRARY.")
endif()

if(NOT MOSQUITTO_CPP_FOUND)
    message(FATAL_ERROR "libmosquittopp not found. Please install libmosquittopp-dev (or equivalent) or specify MOSQUITTO_CPP_INCLUDE_DIRS and MOSQUITTO_CPP_LIBRARIES.")
endif()

# Add source files
set(SOURCES
    src/main.cpp
    src/hackrf_handler.cpp
    src/mqtt_client.cpp
)

# Add include directories
include_directories(
    ${HACKRF_INCLUDE_DIRS}
    ${MOSQUITTO_CPP_INCLUDE_DIRS}
    ${nlohmann_json_SOURCE_DIR}/include # Add nlohmann/json include directory
    include # Our project's public headers
    model   # Our project's model headers
    src     # Our project's private headers (if any)
)

# Add executable
add_executable(hackrf_mqtt_transmitter ${SOURCES})

# Link libraries
target_link_libraries(hackrf_mqtt_transmitter
    PRIVATE
    ${HACKRF_LIBRARIES}
    ${MOSQUITTO_CPP_LIBRARIES} # This should link both libmosquittopp and libmosquitto
    # nlohmann_json is header-only, but if it had a library component, it would be nlohmann_json::nlohmann_json
    # For header-only, ensuring include_directories is enough.
    # However, modern CMake often uses target_include_directories with INTERFACE libraries.
    # nlohmann_json creates an INTERFACE target nlohmann_json::nlohmann_json if using FetchContent/add_subdirectory
    # So, we can link it like this for include paths:
    nlohmann_json::nlohmann_json # This makes its include directories available to our target
    pthread # Mosquitto might also require pthread
)

# Install rules (optional)
# install(TARGETS hackrf_mqtt_transmitter DESTINATION bin)

# Enable warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(hackrf_mqtt_transmitter PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "HackRF include dirs: ${HACKRF_INCLUDE_DIRS}")
message(STATUS "HackRF libraries: ${HACKRF_LIBRARIES}")
message(STATUS "Mosquitto C++ include dirs: ${MOSQUITTO_CPP_INCLUDE_DIRS}")
message(STATUS "Mosquitto C++ libraries: ${MOSQUITTO_CPP_LIBRARIES}")
