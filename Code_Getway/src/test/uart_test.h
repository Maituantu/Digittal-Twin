#ifndef UART_TEST_H
#define UART_TEST_H

#include "common/gw_types.h"

// Offline UART message examples. No serial device or MQTT connection is opened.
gw_status_enum_t uart_test_config(void);
gw_status_enum_t uart_test_run(void);

#endif

