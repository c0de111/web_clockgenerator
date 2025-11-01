cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(web_clockgenerator C CXX ASM)

pico_sdk_init()

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(GIT_COMMIT STREQUAL "")
    set(GIT_COMMIT "unknown")
endif()

execute_process(
    COMMAND git show -s --format=%cd --date=iso-strict HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_DATE
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(GIT_DATE STREQUAL "")
    string(TIMESTAMP GIT_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
endif()

string(TIMESTAMP BUILD_TIME "%Y-%m-%dT%H:%M:%SZ" UTC)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/build_info.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.h
    @ONLY)

add_executable(web_clockgen
    src/main.c
    src/webserver.c
    src/webserver.h
    src/webserver_pages.c
    src/webserver_pages.h
    src/webserver_utils.c
    src/webserver_utils.h
    src/logging.c
    src/logging.h
    src/debug.c
    src/debug.h
    src/signal_controller.c
    src/signal_controller.h
    src/morse_player.c
    src/morse_player.h
    third_party/si5351/si5351.c
    third_party/si5351/si5351.h
)

target_include_directories(web_clockgen PRIVATE
    src
    third_party/si5351
    ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(web_clockgen
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_i2c
)

pico_enable_stdio_usb(web_clockgen 1)
pico_enable_stdio_uart(web_clockgen 0)

pico_add_extra_outputs(web_clockgen)
