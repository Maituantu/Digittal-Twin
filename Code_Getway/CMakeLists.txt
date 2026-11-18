cmake_minimum_required(VERSION 3.16)
project(pi_gateway VERSION 0.1.0 LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson>=2.11)
find_package(Threads REQUIRED)

add_library(gateway_mqtt STATIC
    src/mqtt/mqtt_client.cpp
    src/mqtt/mqtt_messages.cpp
)
target_include_directories(gateway_mqtt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(gateway_mqtt PUBLIC PkgConfig::MOSQUITTO PkgConfig::JANSSON Threads::Threads)
set_target_properties(gateway_mqtt PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

add_library(gateway_command_queue STATIC src/common/command_queue.cpp)
target_include_directories(gateway_command_queue PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(gateway_command_queue PUBLIC Threads::Threads)
set_target_properties(gateway_command_queue PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

add_library(gateway_uart STATIC
    src/uart/uart_client.cpp
    src/uart/uart_messages.cpp
    src/uart/bts/bts_frame_message.c
    src/uart/bts/bts_convert.c
)
target_include_directories(gateway_uart PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(gateway_uart PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

set(MQTT_MANUAL_TEST_SOURCES
    src/test/mqtt_test.cpp
    src/test/mqtt_test_client.cpp
    src/test/mqtt_test_messages.cpp
)
add_executable(gateway src/main.cpp src/system/sys_manager.cpp src/test/uart_test.cpp ${MQTT_MANUAL_TEST_SOURCES})
target_link_libraries(gateway PRIVATE gateway_mqtt gateway_command_queue gateway_uart)
set_target_properties(gateway PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

foreach(target gateway_mqtt gateway_command_queue gateway_uart gateway)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

install(TARGETS gateway RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(mqtt_messages_test tests/mqtt_messages_test.cpp)
    target_link_libraries(mqtt_messages_test PRIVATE gateway_mqtt)
    set_target_properties(mqtt_messages_test PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    add_test(NAME mqtt_messages COMMAND mqtt_messages_test)
    find_package(Python3 COMPONENTS Interpreter)
    find_program(MOSQUITTO_BROKER mosquitto)
    if(Python3_Interpreter_FOUND AND MOSQUITTO_BROKER)
        add_executable(mqtt_generic_test tests/mqtt_generic.cpp)
        target_link_libraries(mqtt_generic_test PRIVATE gateway_mqtt)
        set_target_properties(mqtt_generic_test PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        add_executable(mqtt_lifecycle_driver src/main.cpp src/system/sys_manager.cpp tests/mqtt_test_driver.cpp)
        target_compile_definitions(mqtt_lifecycle_driver PRIVATE MAIN_USE_MQTT_TEST=1 MAIN_USE_UART_TEST=0)
        target_link_libraries(mqtt_lifecycle_driver PRIVATE gateway_mqtt gateway_command_queue gateway_uart)
        set_target_properties(mqtt_lifecycle_driver PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        add_test(NAME mqtt_lifecycle COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/mqtt_lifecycle.py
            $<TARGET_FILE:mqtt_lifecycle_driver> ${MOSQUITTO_BROKER} $<TARGET_FILE:mqtt_generic_test>)
        set_tests_properties(mqtt_lifecycle PROPERTIES TIMEOUT 60)
        add_test(NAME mqtt_network_loss COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/mqtt_network_loss.py
            $<TARGET_FILE:mqtt_lifecycle_driver> ${MOSQUITTO_BROKER})
        set_tests_properties(mqtt_network_loss PROPERTIES TIMEOUT 90)
        add_test(NAME mqtt_manual_modes COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/mqtt_manual_modes.py
            ${CMAKE_CXX_COMPILER} ${CMAKE_CURRENT_SOURCE_DIR}/src
            $<TARGET_FILE:gateway_mqtt> ${MOSQUITTO_BROKER})
        set_tests_properties(mqtt_manual_modes PROPERTIES TIMEOUT 120)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(uart_codec_test tests/uart_codec_test.cpp src/test/uart_test.cpp)
    target_link_libraries(uart_codec_test PRIVATE gateway_uart)
    add_test(NAME uart_codec COMMAND uart_codec_test)
endif()

if(BUILD_TESTING AND UNIX AND Python3_Interpreter_FOUND)
    add_executable(uart_pty_driver tests/uart_pty_driver.cpp)
    target_link_libraries(uart_pty_driver PRIVATE gateway_uart)
    add_test(NAME uart_pty COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/uart_pty.py $<TARGET_FILE:uart_pty_driver>)
    set_tests_properties(uart_pty PROPERTIES TIMEOUT 10)
endif()

if(BUILD_TESTING)
    add_executable(uart_manual_test src/main.cpp src/test/uart_test.cpp)
    target_compile_definitions(uart_manual_test PRIVATE MAIN_USE_UART_TEST=1 MAIN_USE_MQTT_TEST=0)
    target_link_libraries(uart_manual_test PRIVATE gateway_uart)
    add_test(NAME uart_main_mode COMMAND uart_manual_test)
endif()
