#include "test/mqtt_test.h"
#include "test/mqtt_test_client.h"

#if MQTT_TEST_MODE == MQTT_TEST_BOTH
#include "test/mqtt_test_messages.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "mqtt/mqtt_client.h"

/* -------------------- Broker configuration -------------------- */
#define MQTT_TEST_HOST_1 "mqtt.mysignage.vn"
#define MQTT_TEST_HOST_2 "thachthanh.vov.link"
#define MQTT_TEST_HOST_3 "remote.mysignage.vn"
#define MQTT_TEST_HOST_4 "ybimqtt.mysignage.vn"

#ifndef MQTT_TEST_HOST
#define MQTT_TEST_HOST MQTT_TEST_HOST_1
#endif

#ifndef MQTT_TEST_PORT
#define MQTT_TEST_PORT 1883
#endif

static const char s_test_host[] = MQTT_TEST_HOST;
static const int s_test_port = MQTT_TEST_PORT;
static const char s_test_kit_code[] = "KIT01";
static const char s_test_client_id[] = "pi-kit01";

/* -------------------- Authentication -------------------- */

static const char *const s_test_username = NULL;
static const char *const s_test_password = NULL;

/* -------------------- Test state -------------------- */

static volatile sig_atomic_t s_stop_requested;
static bool s_previous_ready;

/* -------------------- Callbacks -------------------- */

/* Upper-layer topic policy: edit these values, not mqtt_client.cpp. */
static const char *const s_sub_topics[] =
{
    "lab/kit/KIT01/cmd/reset",
    "lab/kit/KIT01/cmd/power",
    "lab/kit/KIT01/cmd/voltage",
    "lab/kit/KIT01/cmd/scale",
    "lab/kit/KIT01/cmd/ruler",
    "lab/kit/KIT01/cmd/shutdown"
};
static const char s_pub_topic[] = "test/pi-kit01/status";
static const char s_pub_payload[] = "{\"status\":\"mqtt_test_ready\"}";

#if MQTT_TEST_MODE == MQTT_TEST_BOTH
/* Synthetic codec data goes to a test-only topic, never production telemetry. */
static const char s_pub_telemetry_topic[] = "test/pi-kit01/telemetry";
#endif

/** @brief Set the signal-safe stop flag. */
static void handle_signal(int)
{
    s_stop_requested = 1;
}

/** @brief Print raw client data; combined mode also passes it to the codec test. */
static void on_command(const char *topic, const void *payload, size_t length, bool retained, void *)
{
    if (retained)
    {
        return;
    }

    printf("Command received: topic=%s bytes=%zu\n", topic, length);
    if (length > 0 && length <= 4096)
    {
        printf("Payload: %.*s\n", (int)length, (const char *)payload);
    }

#if MQTT_TEST_MODE == MQTT_TEST_BOTH
    mqtt_test_messages_receive(s_test_kit_code, topic, payload, length);
#endif
}


/* -------------------- Configure once -------------------- */

/** @brief Apply fixed settings and start the MQTT test. */
gw_status_enum_t mqtt_test_client_config(void)
{
    mqtt_config_struct_t config = {};

    config.host = s_test_host;
    config.port = s_test_port;
    config.kit_code = s_test_kit_code;
    config.client_id = s_test_client_id;
    config.username = s_test_username;
    config.password = s_test_password;


    s_stop_requested = 0;
    s_previous_ready = false;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    gw_status_enum_t status = mqtt_client_init(&config, on_command, NULL);
    if (status != GW_OK)
    {
        fprintf(stderr, "MQTT init failed: %d\n", (int)status);
        return status;
    }

    for (size_t i = 0; i < sizeof(s_sub_topics) / sizeof(s_sub_topics[0]); ++i)
    {
        status = mqtt_client_subscribe(s_sub_topics[i], 1);
        if (status != GW_OK)
        {
            fprintf(stderr, "MQTT subscribe registration failed: %d\n", (int)status);
            return status;
        }
    }

    status = mqtt_client_start();
    if (status != GW_OK)
    {
        fprintf(stderr, "MQTT start failed: %d\n", (int)status);
        return status;
    }

    printf("MQTT worker started; host=%s:%d; waiting for caller-selected subscriptions\n", s_test_host, s_test_port);
    return GW_OK;
}

/* -------------------- Run repeatedly -------------------- */

/**
 * @brief Process one test iteration.
 * @note Ctrl+C/SIGTERM stops
 * the client and exits this standalone test process. The common test owns pacing.
 */
void mqtt_test_client_run(void)
{
    if (s_stop_requested)
    {
        exit(mqtt_client_stop() == GW_OK ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    bool ready = mqtt_client_is_ready();

    if (ready && !s_previous_ready)
    {
        gw_status_enum_t status = mqtt_client_publish(s_pub_topic, s_pub_payload, sizeof(s_pub_payload) - 1, 1, false);
        printf("MQTT test publish status=%d topic=%s\n", (int)status, s_pub_topic);

#if MQTT_TEST_MODE == MQTT_TEST_BOTH
        char telemetry[512];
        size_t length = 0;
        status = mqtt_test_messages_build_telemetry(s_test_kit_code, telemetry, sizeof(telemetry), &length);
        if (status == GW_OK)
        {
            status = mqtt_client_publish(s_pub_telemetry_topic, telemetry, length, 1, false);
        }
        printf("MQTT synthetic telemetry publish status=%d topic=%s\n", (int)status, s_pub_telemetry_topic);
#endif
    }


    s_previous_ready = ready;

}
