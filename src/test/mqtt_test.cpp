#include "test/mqtt_test.h"
#include "test/mqtt_test_client.h"
#include "test/mqtt_test_messages.h"

#include <stdio.h>
#include <unistd.h>

/** @brief Configure only the test modules selected by MQTT_TEST_MODE. */
gw_status_enum_t mqtt_test_config(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

#if MQTT_TEST_MODE == MQTT_TEST_CLIENT
    printf("MQTT test mode: CLIENT\n");
    return mqtt_test_client_config();
#elif MQTT_TEST_MODE == MQTT_TEST_MESSAGES
    printf("MQTT test mode: MESSAGES (offline)\n");
    return mqtt_test_messages_config();
#else
    printf("MQTT test mode: BOTH\n");
    gw_status_enum_t status = mqtt_test_messages_config();
    if (status != GW_OK)
    {
        return status;
    }
    return mqtt_test_client_config();
#endif
}

/** @brief Dispatch selected tests and pace the main loop once per iteration. */
void mqtt_test_run(void)
{
#if MQTT_TEST_MODE == MQTT_TEST_CLIENT
    mqtt_test_client_run();
#elif MQTT_TEST_MODE == MQTT_TEST_MESSAGES
    mqtt_test_messages_run();
#else
    mqtt_test_messages_run();
    mqtt_test_client_run();
#endif

    usleep(250000);
}
