#include "test/mqtt_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "mqtt/mqtt_client.h"

static volatile sig_atomic_t s_stop_requested;
static bool s_previous_connected, s_previous_ready;

/* Edit manual test settings here. */
/* Select one of the four hosts below. */
#define MQTT_TEST_HOST_1 "mqtt.mysignage.vn"
#define MQTT_TEST_HOST_2 "thachthanh.vov.link"
#define MQTT_TEST_HOST_3 "remote.mysignage.vn"
#define MQTT_TEST_HOST_4 "ybimqtt.mysignage.vn"

#ifndef MQTT_TEST_HOST
#define MQTT_TEST_HOST MQTT_TEST_HOST_1
#endif

static const char s_test_host[] = MQTT_TEST_HOST;
static const int s_test_port = 1883;
static const char s_test_kit_code[] = "KIT01";
static const char s_test_client_id[] = "pi-kit01";

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

/** @brief Set the signal-safe stop flag. */
static void handle_signal(int)
{
    s_stop_requested = 1;
}

/** @brief Apply test-level retained policy and print actual topic/payload. */
static void on_command(const char *topic, const void *payload, size_t length, bool retained, void *)
{
    if (retained)
    {
        return;
    }
    printf("Command received: topic=%s bytes=%zu (JSON not decoded)\n", topic, length);
    if (length > 0 && length <= 4096)
    {
        printf("Payload: %.*s\n", (int)length, (const char *)payload);
    }
}

/** @brief Read configuration and start the MQTT test. */
gw_status_enum_t mqtt_test_config(void)
{
    int port = s_test_port;
    /* Optional isolated test port; no environment setup is needed manually. */
    const char *test_port = getenv("MQTT_TEST_PORT");
    
    if (test_port != NULL) {
        char *end;
        long parsed_port = strtol(test_port, &end, 10);
        if (*test_port == '\0' || *end != '\0' || parsed_port < 1 || parsed_port > 65535) {
            fprintf(stderr, "Invalid MQTT_TEST_PORT\n");
            return GW_INVALID_ARG;
        }
        port = (int)parsed_port;
    }

    mqtt_config_struct_t config = {};
    config.host = s_test_host;
    /* Keep automated broker tests isolated from the selected hosted service. */
    const char *test_host = getenv("MQTT_TEST_HOST");
    if (test_host != NULL) {
        config.host = test_host;
    }
    config.port = port;
    config.kit_code = s_test_kit_code;
    config.client_id = s_test_client_id;
    config.username = getenv("MQTT_USERNAME");
    config.password = getenv("MQTT_PASSWORD");
    s_stop_requested = 0;
    s_previous_connected = false;
    s_previous_ready = false;
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    if (mqtt_client_init(&config, on_command, NULL) != GW_OK)
    {
        return GW_IO_ERROR;
    }
    for (size_t i = 0; i < sizeof(s_sub_topics) / sizeof(s_sub_topics[0]); ++i)
    {
        if (mqtt_client_subscribe(s_sub_topics[i], 1) != GW_OK)
        {
            return GW_IO_ERROR;
        }
    }
    if (mqtt_client_start() != GW_OK)
    {
        return GW_IO_ERROR;
    }
    printf("MQTT worker started; waiting for caller-selected subscriptions\n");
    return GW_OK;
}

/**
 * @brief Process one test iteration.
 * @note Ctrl+C/SIGTERM stops
 * the client and exits this standalone test process. Includes a 100 ms pause.
 */
void mqtt_test_run(void)
{
    if (s_stop_requested) {
        exit(mqtt_client_stop() == GW_OK ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    bool connected = mqtt_client_is_connected();
    bool ready = mqtt_client_is_ready();

    if (ready && !s_previous_ready)
    {
        gw_status_enum_t status = mqtt_client_publish(s_pub_topic, s_pub_payload, sizeof(s_pub_payload) - 1, 1, false);
        printf("MQTT test publish status=%d topic=%s\n", (int)status, s_pub_topic);
    }

    if (connected != s_previous_connected || ready != s_previous_ready) {
        printf("MQTT connected=%d ready=%d\n", connected, ready);
        if (s_previous_connected && !connected) {
            gw_status_enum_t status = mqtt_client_reconnect();
            printf("MQTT reconnect request status=%d\n", (int)status);
        }
        s_previous_connected = connected;
        s_previous_ready = ready;
    }
    usleep(100000);
}
