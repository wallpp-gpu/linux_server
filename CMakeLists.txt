cmake_minimum_required(VERSION 3.16)
project(a7a_control_center LANGUAGES C)

set(CMAKE_C_STANDARN 11)                      #使用C11
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(
    a7a_control_center
    src/main.c
    src/network.c
    src/uart.c
    src/command.c
    src/device.c
    src/logger.c
    src/system_monitor.c
)

target_include_directories(
    a7a_control_center
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)           #线程库

target_link_libraries(a7a_control_center PRIVATE Threads::Threads)

target_compile_options(a7a_control_center PRIVATE -Wall -Wextra)

