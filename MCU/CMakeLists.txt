cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sdv_mcu)

target_sources(app PRIVATE
    src/main.cpp
    src/can_gateway.cpp
    src/someip_service.cpp
    src/someip_sd.cpp
    src/safety.cpp
)

zephyr_include_directories(src)
