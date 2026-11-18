#include <stdlib.h>

// Select one test: set its flag to 1. Both 0 runs the system.
#ifndef MAIN_USE_MQTT_TEST
#define MAIN_USE_MQTT_TEST 0
#endif

#ifndef MAIN_USE_UART_TEST
#define MAIN_USE_UART_TEST 0
#endif

#if MAIN_USE_MQTT_TEST && MAIN_USE_UART_TEST
#error "Select only one test: MQTT or UART"
#endif

#if MAIN_USE_UART_TEST
#include "test/uart_test.h"
#elif MAIN_USE_MQTT_TEST
#include "test/mqtt_test.h"
#else
#include "system/sys_manager.h"
#endif

int main(void)
{
#if MAIN_USE_UART_TEST
    if (uart_test_config() != GW_OK)
    {
        return EXIT_FAILURE;
    }
    return uart_test_run() == GW_OK ? EXIT_SUCCESS : EXIT_FAILURE;
#else
#if MAIN_USE_MQTT_TEST
    if (mqtt_test_config() != GW_OK)
#else
    if (sys_manager_config() != GW_OK)
#endif
    {
        return EXIT_FAILURE;
    }

    while (1)
    {
#if MAIN_USE_MQTT_TEST
        mqtt_test_run();
#else
        sys_manager_run();
#endif
    }
#endif
}
