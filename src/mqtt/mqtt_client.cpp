/**
 * @file mqtt_client.cpp
 * @brief Class-free MQTT client implemented with libmosquitto and pthreads.
 */
#include "mqtt/mqtt_client.h"

#include <mosquitto.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>

#define MQTT_KEEPALIVE_SECONDS          15      // Keepalive 15 giây; mất mạng âm thầm có thể cần khoảng 30 giây để phát hiện.
#define MQTT_RECONNECT_INITIAL_SECONDS   1U     // Thời gian chờ thử kết nối lại ban đầu: 1 giây.
#define MQTT_RECONNECT_MAX_SECONDS      30U     // Thời gian chờ thử lại tăng dần, tối đa 30 giây.
#define MQTT_HANDSHAKE_TIMEOUT_MS       15000U  // Timeout chờ CONNACK/SUBACK: 15.000 mili giây (15 giây).
#define MQTT_NETWORK_WAIT_MS            1000   // Chờ sự kiện mạng tối đa 1 giây; có dữ liệu thì xử lý ngay.

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_retry_condition;
static bool s_condition_initialized;
static pthread_t s_worker;
static struct mosquitto *s_client;
static mqtt_config_struct_t s_config;
static char s_host[256], s_id[128], s_kit[128];
static char s_username[256], s_password[256];
static mqtt_message_callback_t s_callback;
static void *s_context;
static bool s_initialized, s_running, s_stop, s_retry;
static bool s_connected, s_ready;
#define MQTT_MAX_SUBSCRIPTIONS 16
#define MQTT_TOPIC_CAPACITY 256
#define MQTT_MAX_PENDING_PUBLISHES 32

typedef struct
{
    char filter[MQTT_TOPIC_CAPACITY];
    int qos;
    int mid;
    bool acknowledged;
} mqtt_subscription_struct_t;

static mqtt_subscription_struct_t s_subscriptions[MQTT_MAX_SUBSCRIPTIONS];
static size_t s_subscription_count;
static bool s_subscriptions_pending;
static int s_publish_ids[MQTT_MAX_PENDING_PUBLISHES];
static size_t s_publish_count;
/* Only the network worker accesses this flag. */
static bool s_connection_failed;

/**
 * @brief Get monotonic time for retries/timeouts.
 * @return Milliseconds on CLOCK_MONOTONIC, not UTC wall-clock time.
 */
static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

/** @brief Initialize the monotonic retry wait once; init calls are serialized. */
static bool initialize_retry_wait(void)
{
    if (s_condition_initialized)
    {
        return true;
    }
    pthread_condattr_t attributes;
    if (pthread_condattr_init(&attributes) != 0)
    {
        return false;
    }
    int result = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (result == 0)
    {
        result = pthread_cond_init(&s_retry_condition, &attributes);
    }
    pthread_condattr_destroy(&attributes);
    s_condition_initialized = result == 0;
    return s_condition_initialized;
}

/**
 * @brief Update state under the mutex, forcing false during shutdown.
 * @param[in] connected New connection state.
 * @param[in] ready New subscription-readiness state.
 * @note Must not be called while holding s_mutex.
 */
static void set_state(bool connected, bool ready)
{
    pthread_mutex_lock(&s_mutex);
    s_connected = connected && !s_stop;
    s_ready = ready && !s_stop;
    pthread_mutex_unlock(&s_mutex);
}

/** @brief Mark the caller's desired subscriptions for restoration on CONNACK. */
static void on_connect(struct mosquitto *, void *, int result)
{
    s_connection_failed = result != 0;
    pthread_mutex_lock(&s_mutex);
    s_connected = result == 0 && !s_stop;
    s_ready = s_connected && s_subscription_count == 0;
    s_subscriptions_pending = s_connected && s_subscription_count != 0;
    for (size_t i = 0; i < s_subscription_count; ++i)
    {
        s_subscriptions[i].mid = 0;
        s_subscriptions[i].acknowledged = false;
    }
    pthread_mutex_unlock(&s_mutex);
    if (result != 0)
    {
        fprintf(stderr, "MQTT connection rejected: %s\n", mosquitto_connack_string(result));
    }
    else
    {
        fprintf(stderr, "[MQTT] CONNACK accepted; restoring caller subscriptions\n");
    }
}

/** @brief Send desired subscriptions on the worker, never from application callbacks. */
static bool apply_subscriptions(void)
{
    pthread_mutex_lock(&s_mutex);
    if (!s_connected || s_stop || !s_subscriptions_pending)
    {
        pthread_mutex_unlock(&s_mutex);
        return false;
    }
    s_subscriptions_pending = false;
    s_ready = false;
    for (size_t i = 0; i < s_subscription_count; ++i)
    {
        mqtt_subscription_struct_t *entry = &s_subscriptions[i];
        entry->acknowledged = false;
        int result = mosquitto_subscribe(s_client, &entry->mid, entry->filter, entry->qos);
        if (result != MOSQ_ERR_SUCCESS)
        {
            s_connection_failed = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return true;
}

/** @brief Clear connection state; the worker owns retry scheduling. */
static void on_disconnect(struct mosquitto *, void *, int reason)
{
    if (reason != MOSQ_ERR_SUCCESS)
    {
        fprintf(stderr, "[MQTT] Connection lost: %s (code=%d)\n", mosquitto_strerror(reason), reason);
    }
    s_connection_failed = true;
    set_state(false, false);
}

/** @brief Match SUBACK packet IDs against the caller's current filter list. */
static void on_subscribe(struct mosquitto *, void *, int mid, int count, const int *qos)
{
    pthread_mutex_lock(&s_mutex);
    for (size_t i = 0; i < s_subscription_count; ++i)
    {
        mqtt_subscription_struct_t *entry = &s_subscriptions[i];
        if (entry->mid == mid)
        {
            if (count != 1 || qos == NULL || qos[0] < 0 || qos[0] > entry->qos)
            {
                fprintf(stderr, "MQTT subscription rejected: %s\n", entry->filter);
                s_connection_failed = true;
                s_ready = false;
                pthread_mutex_unlock(&s_mutex);
                return;
            }
            entry->acknowledged = true;
        }
    }
    bool acknowledged = true;
    for (size_t i = 0; i < s_subscription_count; ++i)
    {
        acknowledged = acknowledged && s_subscriptions[i].acknowledged;
    }
    s_ready = acknowledged && s_connected && !s_stop && !s_subscriptions_pending;
    pthread_mutex_unlock(&s_mutex);
}

/** @brief Deliver actual topic and raw bytes; policy belongs to the upper layer. */
static void on_message(struct mosquitto *, void *, const struct mosquitto_message *message)
{
    if (!s_callback || !mqtt_client_is_connected() || !message || !message->topic || message->payloadlen < 0 || (message->payloadlen > 0 && !message->payload))
    {
        return;
    }
    s_callback(message->topic, message->payload, (size_t)message->payloadlen, message->retain, s_context);
}

/** @brief Release a bounded outbound slot on backend publish completion. */
static void on_publish(struct mosquitto *, void *, int mid)
{
    pthread_mutex_lock(&s_mutex);
    for (size_t i = 0; i < s_publish_count; ++i)
    {
        if (s_publish_ids[i] == mid)
        {
            s_publish_ids[i] = s_publish_ids[--s_publish_count];
            break;
        }
    }
    pthread_mutex_unlock(&s_mutex);
}

/**
 * @brief Own the network loop and the single retry schedule.
 * @return Always NULL on normal thread exit.
 * @details Connects, processes packets, allows up to
 * 15 seconds for subscription readiness as observed by the loop, and retries
 * with capped exponential backoff. Reads stop/retry flags under the mutex and
 * disconnects on shutdown.
 * @note Pthread argument is unused.
 * DNS/callbacks may block and extend deadlines; do not also use loop_start.
 */
static void *network_worker(void *)
{
    bool connecting = false;
    uint64_t next_attempt = 0, handshake_deadline = 0;
    unsigned int delay = MQTT_RECONNECT_INITIAL_SECONDS;
    for (;;)
    {
        pthread_mutex_lock(&s_mutex);
        /* Sleep until retry is due; stop/reconnect can wake this immediately. */
        while (!connecting && !s_stop && !s_retry && monotonic_ms() < next_attempt)
        {
            const struct timespec deadline = {(time_t)(next_attempt / 1000), (long)(next_attempt % 1000) * 1000000};
            int wait_result = pthread_cond_timedwait(&s_retry_condition, &s_mutex, &deadline);
            if (wait_result != 0 && wait_result != ETIMEDOUT)
            {
                fprintf(stderr, "[MQTT] Retry wait failed: %s\n", strerror(wait_result));
                pthread_mutex_unlock(&s_mutex);
                set_state(false, false);
                return NULL;
            }
        }
        bool stop = s_stop;
        bool retry = s_retry;
        s_retry = false;
        pthread_mutex_unlock(&s_mutex);
        if (stop)
        {
            break;
        }
        uint64_t now = monotonic_ms();
        if (retry && !connecting)
        {
            next_attempt = 0;
        }
        if (!connecting && now >= next_attempt)
        {
            s_connection_failed = false;
            fprintf(stderr, "[MQTT] Connecting to %s:%d; keepalive=%ds\n", s_host, s_config.port, MQTT_KEEPALIVE_SECONDS);
            int connect_result = mosquitto_connect_async(s_client, s_host, s_config.port, MQTT_KEEPALIVE_SECONDS);
            int connect_errno = errno;
            connecting = connect_result == MOSQ_ERR_SUCCESS;
            handshake_deadline = monotonic_ms() + MQTT_HANDSHAKE_TIMEOUT_MS;
            if (!connecting)
            {
                fprintf(stderr, "[MQTT] Connect failed: %s; retry in %us\n", connect_result == MOSQ_ERR_ERRNO ? strerror(connect_errno) : mosquitto_strerror(connect_result), delay);
                set_state(false, false);
                next_attempt = monotonic_ms() + (uint64_t)delay * 1000;
                delay = delay > MQTT_RECONNECT_MAX_SECONDS / 2
                    ? MQTT_RECONNECT_MAX_SECONDS : delay * 2;
            }
        }
        if (connecting)
        {
            /* select() sleeps while idle and returns early for network activity. */
            int result = mosquitto_loop(s_client, MQTT_NETWORK_WAIT_MS, 1);
            int loop_errno = errno;
            if (apply_subscriptions())
            {
                handshake_deadline = monotonic_ms() + MQTT_HANDSHAKE_TIMEOUT_MS;
            }
            now = monotonic_ms();
            if (result != MOSQ_ERR_SUCCESS || s_connection_failed || (!mqtt_client_is_ready() && now >= handshake_deadline))
            {
                const char *reason = result != MOSQ_ERR_SUCCESS ? (result == MOSQ_ERR_ERRNO ? strerror(loop_errno) : mosquitto_strerror(result)) : (s_connection_failed ? "connection/subscription rejected" : "CONNACK/SUBACK timeout");
                fprintf(stderr, "[MQTT] Session unavailable: %s; retry in %us\n", reason, delay);
                mosquitto_disconnect(s_client);
                set_state(false, false);
                connecting = false;
                next_attempt = monotonic_ms() + (uint64_t)delay * 1000;
                delay = delay > MQTT_RECONNECT_MAX_SECONDS / 2 ? MQTT_RECONNECT_MAX_SECONDS : delay * 2;
            }
            else if (mqtt_client_is_ready())
            {
                delay = MQTT_RECONNECT_INITIAL_SECONDS;
            }
        }
    }
    mosquitto_disconnect(s_client);
    mosquitto_loop(s_client, 100, 1);
    set_state(false, false);
    return NULL;
}

/**
 * @brief Check whether an optional string fits including NUL.
 * @param[in] text NUL-terminated string or NULL.
 * @param[in] capacity Destination capacity in bytes.
 * @return true for NULL or strlen(text) < capacity.
 */
static bool fits(const char *text, size_t capacity)
{
    return text == NULL || strlen(text) < capacity;
}

/**
 * @brief Validate/copy configuration and register callback.
 * @see mqtt_client.h mqtt_client_init
 */
gw_status_enum_t mqtt_client_init(const mqtt_config_struct_t *config, mqtt_message_callback_t callback, void *context)
{
    if (s_running)
    {
        return GW_BUSY;
    }
    if (!config || !config->host || !config->host[0] || !config->client_id || !config->client_id[0] || config->port < 1 || config->port > 65535 || (config->password && !config->username))
    {
        return GW_INVALID_ARG;
    }
    if (!fits(config->host, sizeof(s_host)) || !fits(config->client_id, sizeof(s_id)) || !fits(config->kit_code, sizeof(s_kit)) || !fits(config->username, sizeof(s_username)) || !fits(config->password, sizeof(s_password)))
    {
        return GW_INVALID_ARG;
    }
    if (!initialize_retry_wait())
    {
        return GW_IO_ERROR;
    }
    s_config = *config;
    /* All lengths were validated before modifying the stored configuration. */
    strcpy(s_host, config->host);
    strcpy(s_id, config->client_id);
    strcpy(s_kit, config->kit_code ? config->kit_code : "");
    strcpy(s_username, config->username ? config->username : "");
    strcpy(s_password, config->password ? config->password : "");
    s_config.host = s_host;
    s_config.client_id = s_id;
    s_config.kit_code = config->kit_code ? s_kit : NULL;
    s_config.username = config->username ? s_username : NULL;
    s_config.password = config->password ? s_password : NULL;
    s_callback = callback;
    s_context = context;
    pthread_mutex_lock(&s_mutex);
    s_subscription_count = 0;
    s_publish_count = 0;
    s_subscriptions_pending = false;
    s_initialized = true;
    pthread_mutex_unlock(&s_mutex);
    return GW_OK;
}

/**
 * @brief Create the client and start the network worker.
 * @see mqtt_client.h mqtt_client_start
 */
gw_status_enum_t mqtt_client_start(void)
{
    if (s_running)
    {
        return GW_BUSY;
    }
    if (!s_initialized)
    {
        return GW_NOT_READY;
    }
    if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS)
    {
        return GW_IO_ERROR;
    }
    s_client = mosquitto_new(s_id, true, NULL);
    if (!s_client)
    {
        mosquitto_lib_cleanup();
        return GW_IO_ERROR;
    }
    mosquitto_connect_callback_set(s_client, on_connect);
    mosquitto_disconnect_callback_set(s_client, on_disconnect);
    mosquitto_subscribe_callback_set(s_client, on_subscribe);
    mosquitto_message_callback_set(s_client, on_message);
    mosquitto_publish_callback_set(s_client, on_publish);
    int result = mosquitto_threaded_set(s_client, true);
    if (result == MOSQ_ERR_SUCCESS && s_config.username)
    {
        result = mosquitto_username_pw_set(s_client, s_config.username, s_config.password);
    }
    pthread_mutex_lock(&s_mutex);
    s_publish_count = 0;
    s_stop = false;
    s_retry = false;
    s_connected = false;
    s_ready = false;
    pthread_mutex_unlock(&s_mutex);
    if (result != MOSQ_ERR_SUCCESS || pthread_create(&s_worker, NULL, network_worker, NULL) != 0)
    {
        mosquitto_destroy(s_client);
        s_client = NULL;
        mosquitto_lib_cleanup();
        return GW_IO_ERROR;
    }
    pthread_mutex_lock(&s_mutex);
    s_running = true;
    pthread_mutex_unlock(&s_mutex);
    return GW_OK;
}

/** @brief Copy a caller-selected filter into the reconnect subscription list. */
gw_status_enum_t mqtt_client_subscribe(const char *topic_filter, int qos)
{
    if (!topic_filter || !topic_filter[0] || strlen(topic_filter) >= MQTT_TOPIC_CAPACITY || qos < 0 || qos > 2 || mosquitto_sub_topic_check(topic_filter) != MOSQ_ERR_SUCCESS)
    {
        return GW_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mutex);
    if (!s_initialized || (s_running && s_stop))
    {
        pthread_mutex_unlock(&s_mutex);
        return GW_NOT_READY;
    }
    size_t index = 0;
    while (index < s_subscription_count && strcmp(s_subscriptions[index].filter, topic_filter) != 0)
    {
        ++index;
    }
    if (index == MQTT_MAX_SUBSCRIPTIONS)
    {
        pthread_mutex_unlock(&s_mutex);
        return GW_BUSY;
    }
    if (index < s_subscription_count && s_subscriptions[index].qos == qos)
    {
        pthread_mutex_unlock(&s_mutex);
        return GW_OK;
    }
    if (index == s_subscription_count)
    {
        ++s_subscription_count;
    }
    mqtt_subscription_struct_t *entry = &s_subscriptions[index];
    strcpy(entry->filter, topic_filter);
    entry->qos = qos;
    entry->mid = 0;
    entry->acknowledged = false;
    s_subscriptions_pending = true;
    s_ready = false;
    pthread_mutex_unlock(&s_mutex);
    return GW_OK;
}

/** @brief Submit bytes to the caller's publish topic without interpreting JSON. */
gw_status_enum_t mqtt_client_publish(const char *topic, const void *payload, size_t length, int qos, bool retain)
{
    if (!topic || !topic[0] || length > 4096 || (length > 0 && !payload) || qos < 0 || qos > 2 || mosquitto_pub_topic_check(topic) != MOSQ_ERR_SUCCESS)
    {
        return GW_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mutex);
    if (!s_running || s_stop || !s_connected)
    {
        pthread_mutex_unlock(&s_mutex);
        return GW_NOT_READY;
    }
    if (s_publish_count == MQTT_MAX_PENDING_PUBLISHES)
    {
        pthread_mutex_unlock(&s_mutex);
        return GW_BUSY;
    }
    int mid;
    int result = mosquitto_publish(s_client, &mid, topic, (int)length, payload, qos, retain);
    if (result == MOSQ_ERR_SUCCESS)
    {
        s_publish_ids[s_publish_count++] = mid;
    }
    pthread_mutex_unlock(&s_mutex);
    if (result == MOSQ_ERR_NO_CONN)
    {
        return GW_NOT_READY;
    }
    return result == MOSQ_ERR_SUCCESS ? GW_OK : GW_IO_ERROR;
}

/**
 * @brief Request an earlier retry under the mutex.
 * @see mqtt_client.h mqtt_client_reconnect
 */
gw_status_enum_t mqtt_client_reconnect(void)
{
    pthread_mutex_lock(&s_mutex);
    gw_status_enum_t result = GW_NOT_READY;
    if (s_running && !s_stop)
    {
        s_retry = !s_connected;
        pthread_cond_signal(&s_retry_condition);
        result = GW_OK;
    }
    pthread_mutex_unlock(&s_mutex);
    return result;
}

/**
 * @brief Read the connection snapshot under the mutex.
 * @see mqtt_client.h mqtt_client_is_connected
 */
bool mqtt_client_is_connected(void)
{
    pthread_mutex_lock(&s_mutex);
    bool connected = s_connected;
    pthread_mutex_unlock(&s_mutex);
    return connected;
}

/**
 * @brief Read the subscription-readiness snapshot under the mutex.
 * @see mqtt_client.h mqtt_client_is_ready
 */
bool mqtt_client_is_ready(void)
{
    pthread_mutex_lock(&s_mutex);
    bool ready = s_ready;
    pthread_mutex_unlock(&s_mutex);
    return ready;
}

/**
 * @brief Stop/join the worker and release the client.
 * @see mqtt_client.h mqtt_client_stop
 */
gw_status_enum_t mqtt_client_stop(void)
{
    pthread_mutex_lock(&s_mutex);
    if (!s_running)
    {
        pthread_mutex_unlock(&s_mutex);
        return GW_OK;
    }
    if (pthread_equal(pthread_self(), s_worker))
    {
        pthread_mutex_unlock(&s_mutex);
        return GW_BUSY;
    }
    s_stop = true;
    s_connected = false;
    s_ready = false;
    pthread_cond_signal(&s_retry_condition);
    pthread_mutex_unlock(&s_mutex);
    if (pthread_join(s_worker, NULL) != 0)
    {
        return GW_IO_ERROR;
    }
    mosquitto_destroy(s_client);
    s_client = NULL;
    mosquitto_lib_cleanup();
    pthread_mutex_lock(&s_mutex);
    s_running = false;
    pthread_mutex_unlock(&s_mutex);
    return GW_OK;
}
