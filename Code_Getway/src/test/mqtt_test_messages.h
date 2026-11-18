/** @file mqtt_test_messages.h
 * @brief Offline codec examples and live JSON helpers for combined testing.
 */
#ifndef MQTT_TEST_MESSAGES_H
#define MQTT_TEST_MESSAGES_H

#include <stddef.h>
#include "common/gw_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Initialize one-shot codec examples and offline shutdown handling. */
gw_status_enum_t mqtt_test_messages_config(void);

/** @brief Run six local decode examples and one synthetic telemetry encode once. */
void mqtt_test_messages_run(void);

/** @brief Decode a live command and log its typed value; no UART/hardware action. */
void mqtt_test_messages_receive(const char *kit_code, const char *topic, const void *payload, size_t length);

/**
 * @brief Encode a synthetic sample using current UTC; for test topics only.
 * @param[in] kit_code Kit metadata for the example JSON.
 * @param[out] output Caller-owned JSON buffer.
 * @param[in] capacity Buffer bytes including NUL.
 * @param[out] written JSON length excluding NUL, zero on failure.
 * @return Status from the codec, or GW_IO_ERROR if reading the clock fails.
 */
gw_status_enum_t mqtt_test_messages_build_telemetry(const char *kit_code, char *output, size_t capacity, size_t *written);

#ifdef __cplusplus
}
#endif
#endif
