#include "mqtt/mqtt_client.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_received;
static bool s_received_retained;
static bool s_bad_message;
static const unsigned char s_payload[] = {0, 1, 127, 255};

static void receive(const char *topic, const void *payload, size_t length, bool retained, void *)
{
    pthread_mutex_lock(&s_mutex);
    s_bad_message = s_bad_message || strcmp(topic, "custom/device/data") != 0 || length != sizeof(s_payload) || memcmp(payload, s_payload, sizeof(s_payload)) != 0;
    s_received_retained = s_received_retained || retained;
    ++s_received;
    pthread_mutex_unlock(&s_mutex);
}

static bool wait_ready(void)
{
    for (int i = 0; i < 100; ++i)
    {
        if (mqtt_client_is_ready())
        {
            return true;
        }
        usleep(100000);
    }
    return false;
}

static bool wait_messages(int expected, bool retained)
{
    for (int i = 0; i < 100; ++i)
    {
        pthread_mutex_lock(&s_mutex);
        bool ok = s_received >= expected && !s_bad_message && (!retained || s_received_retained);
        pthread_mutex_unlock(&s_mutex);
        if (ok)
        {
            return true;
        }
        usleep(100000);
    }
    return false;
}

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Failed: %s at line %d\n", #condition, __LINE__); mqtt_client_stop(); return EXIT_FAILURE; } } while (0)

int main(void)
{
    const char *port = getenv("MQTT_TEST_PORT");
    CHECK(port != NULL);
    mqtt_config_struct_t config = {};
    config.host = "127.0.0.1";
    config.port = atoi(port);
    config.client_id = "generic-api-test";
    /* No kit metadata is needed by the generic transport. */
    CHECK(mqtt_client_start() == GW_NOT_READY);
    CHECK(mqtt_client_init(&config, receive, NULL) == GW_OK);
    CHECK(mqtt_client_publish("custom/device/data", NULL, 0, 0, false) == GW_NOT_READY);
    CHECK(mqtt_client_subscribe("bad/#/filter", 0) == GW_INVALID_ARG);
    CHECK(mqtt_client_start() == GW_OK);
    CHECK(mqtt_client_start() == GW_BUSY);
    CHECK(wait_ready());
    CHECK(mqtt_client_publish("lab/kit/KIT01/cmd/power", "{}", 2, 0, false) == GW_OK);
    usleep(300000);
    pthread_mutex_lock(&s_mutex);
    bool no_implicit_subscriptions = s_received == 0;
    pthread_mutex_unlock(&s_mutex);
    CHECK(no_implicit_subscriptions);
    CHECK(mqtt_client_subscribe("custom/+/data", 2) == GW_OK);
    CHECK(wait_ready());
    CHECK(mqtt_client_publish("bad/+", "x", 1, 0, false) == GW_INVALID_ARG);
    CHECK(mqtt_client_publish("custom/device/data", s_payload, sizeof(s_payload), 2, true) == GW_OK);
    CHECK(wait_messages(1, false));
    /* Changing QoS re-SUBs and must expose retained data to the upper layer. */
    CHECK(mqtt_client_subscribe("custom/+/data", 1) == GW_OK);
    CHECK(wait_ready());
    CHECK(wait_messages(2, true));
    CHECK(mqtt_client_stop() == GW_OK);
    CHECK(mqtt_client_stop() == GW_OK);
    CHECK(mqtt_client_start() == GW_OK);
    CHECK(wait_ready());
    CHECK(wait_messages(3, true));
    CHECK(mqtt_client_stop() == GW_OK);
    CHECK(mqtt_client_publish("custom/device/data", NULL, 0, 0, false) == GW_NOT_READY);
    puts("PASS: generic topics, dynamic wildcard SUB, binary PUB/SUB, retained delivery, restart, invalid inputs");
    return EXIT_SUCCESS;
}
