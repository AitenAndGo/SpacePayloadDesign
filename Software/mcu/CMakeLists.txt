cmake_minimum_required(VERSION 3.13)

# Target board: Pico 2 WH (RP2350 + WiFi). LED is on CYW43 chip, not GPIO 25.
# Use pico2 for Pico 2 (no WiFi), pico2_w for Pico 2 W/WH.
set(PICO_BOARD pico2_w CACHE STRING "Pico board type" FORCE)

# Enable automatic SDK fetching from git if PICO_SDK_PATH is not set
if (NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_FETCH_FROM_GIT ON CACHE BOOL "Fetch Pico SDK from git" FORCE)
endif()

# Generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(pico_sdk_import.cmake)

project(optical_payload_app C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(optical_payload_app
    src/main.c
    # src/maxon.c
    # src/tcp_server.c
)

target_include_directories(optical_payload_app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/inc
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(optical_payload_app
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_spi
)

pico_enable_stdio_usb(optical_payload_app 1)
pico_enable_stdio_uart(optical_payload_app 0)

pico_add_extra_outputs(optical_payload_app)
