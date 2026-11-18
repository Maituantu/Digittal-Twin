/** @file mqtt_test_client.h
 * @brief Broker connection and raw SUB/PUB manual test.
 */
#ifndef MQTT_TEST_CLIENT_H
#define MQTT_TEST_CLIENT_H

#include "common/gw_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Configure broker/topics and start the MQTT worker. */
gw_status_enum_t mqtt_test_client_config(void);

/** @brief Handle shutdown and publish on readiness; does not sleep. */
void mqtt_test_client_run(void);

#ifdef __cplusplus
}
#endif
#endif
