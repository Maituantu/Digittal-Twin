/**
 * @file mqtt_test.h
 * @brief Select client, local codec, or combined tests with MQTT_TEST_MODE.
 */
#ifndef MQTT_TEST_H
#define MQTT_TEST_H

#include "common/gw_types.h"

#define MQTT_TEST_CLIENT   1
#define MQTT_TEST_MESSAGES 2
#define MQTT_TEST_BOTH     3

/* Change only this selection, then rebuild. */
#ifndef MQTT_TEST_MODE
#define MQTT_TEST_MODE MQTT_TEST_MESSAGES
#endif

#if MQTT_TEST_MODE != MQTT_TEST_CLIENT && MQTT_TEST_MODE != MQTT_TEST_MESSAGES && MQTT_TEST_MODE != MQTT_TEST_BOTH
#error "MQTT_TEST_MODE must be MQTT_TEST_CLIENT, MQTT_TEST_MESSAGES or MQTT_TEST_BOTH"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Configure and start the test once.
 * @retval GW_OK Selected tests initialized.
 * @retval GW_INVALID_ARG Invalid test configuration.
 * @retval GW_IO_ERROR Selected test initialization failed.
 * @note Call before the while loop. Broker settings are in mqtt_test_client.cpp.
 * Messages-only mode does not start a network connection.
 */
gw_status_enum_t mqtt_test_config(void);

/**
 * @brief Run only the selected tests, then sleep once for 250 ms.
 * @pre Successful mqtt_test_config.
 * @note Codec examples run once; live messages are decoded in combined mode.
 * SIGINT/SIGTERM exits the standalone test, stopping MQTT if it was started.
 */
void mqtt_test_run(void);

#ifdef __cplusplus
}
#endif
#endif
