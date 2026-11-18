/**
 * @file mqtt_client.h
 * @brief Function-based gateway MQTT API.
 * @details Serialize init/start/stop
 * on the application thread. This module owns process-wide libmosquitto setup.
 */
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include "common/gw_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Plaintext MQTT configuration copied during init; TLS is not supported. */
typedef struct
{
    const char *host; /**< Hostname/IP without scheme. */
    int port; /**< Port in 1..65535. */
    const char *client_id; /**< Nonempty client identifier. */
    const char *kit_code; /**< Optional application metadata; never used to construct topics. */
    const char *username; /**< NULL when unused. */
    const char *password; /**< Optional; requires username when non-NULL. */
} mqtt_config_struct_t;

/**
 * @brief Raw MQTT message callback; topic selection and validation belong to the caller.
 * @param[in] topic Borrowed actual topic string, not a subscription filter.
 * @param[in] payload Borrowed buffer, not necessarily NUL-terminated.
 * @param[in] length Payload length in bytes.
 * @param[in] retained True if delivered as a retained message.
 * @param[in,out] context Application context registered at init.
 * @note Runs on the worker; copy payload
 * before returning if needed later. Do not block or call init/start/stop.
 * @warning JSON is
 * not validated; do not forward directly to UART. The caller decides whether to accept retained messages.
 */
typedef void (*mqtt_message_callback_t)(const char *topic, const void *payload, size_t length, bool retained, void *context);

/**
 * @brief Validate/copy configuration and register callback.
 * @param[in] config Non-NULL configuration.
 * @param[in] callback Callback, or NULL to ignore messages.
 * @param[in,out] context Borrowed context valid until stop returns.
 * @retval GW_OK Configuration stored; not connected yet.
 * @retval GW_BUSY Worker is running.
 * @retval GW_INVALID_ARG Invalid field or string length.
 * @retval GW_IO_ERROR Could not initialize the monotonic retry wait.
 * @pre Call serially on the application thread with the worker stopped.
 * @note Input strings only need to live for this call; callback/context are not deep-copied.
 */
gw_status_enum_t mqtt_client_init(const mqtt_config_struct_t *config, mqtt_message_callback_t callback, void *context);
/**
 * @brief Create the MQTT client and worker.
 * @retval GW_OK Worker created; connection proceeds in the background.
 * @retval GW_BUSY Already running.
 * @retval GW_NOT_READY Initialization has not succeeded.
 * @retval GW_IO_ERROR Library, resource or thread creation error.
 * @pre Successful init; serialize on the application thread.
 * @note GW_OK does not establish broker/SUBACK success.
 */
gw_status_enum_t mqtt_client_start(void);

/**
 * @brief Register/update a caller-selected subscription filter and QoS (0..2).
 * @param[in] topic_filter NUL-terminated MQTT filter; +/# wildcards are allowed.
 * @param[in] qos Requested maximum QoS, 0..2.
 * @retval GW_OK Filter copied into the desired subscription list, not yet SUBACKed.
 * @retval GW_NOT_READY Not initialized, or shutdown is in progress.
 * @retval GW_INVALID_ARG Invalid filter/QoS or filter exceeds 255 bytes.
 * @retval GW_BUSY Subscription capacity (16 filters) reached.
 * @note Call after init, before or after start. Thread-safe after init.
 * Filters persist across stop/start and are restored after reconnect.
 * Calling init while stopped resets the list. No subscriptions are implicit.
 */
gw_status_enum_t mqtt_client_subscribe(const char *topic_filter, int qos);

/**
 * @brief Publish raw bytes to a caller-selected topic, with QoS 0..2 and retain.
 * @param[in] topic NUL-terminated topic without wildcards.
 * @param[in] payload Bytes to send, or NULL for an empty payload.
 * @param[in] length Payload length; maximum 4096 bytes in this gateway.
 * @param[in] qos Requested QoS, 0..2.
 * @param[in] retain Whether the broker should retain this message.
 * @retval GW_OK Accepted by libmosquitto, not proof of broker/subscriber delivery.
 * @retval GW_NOT_READY Not connected, not started, or stopping; no offline enqueue.
 * @retval GW_INVALID_ARG Invalid topic, QoS, payload or length.
 * @retval GW_IO_ERROR Backend publish failed.
 * @retval GW_BUSY Outbound queue is full (32 pending messages).
 * @note Thread-safe; copies payload before returning. Pending QoS messages may
 * be retried by libmosquitto after reconnect; command deduplication belongs to
 * the application. Stop discards uncompleted outgoing messages.
 */
gw_status_enum_t mqtt_client_publish(const char *topic, const void *payload, size_t length, int qos, bool retain);

/**
 * @brief Request an earlier attempt while waiting to reconnect.
 * @retval GW_OK Request accepted or connection already active.
 * @retval GW_NOT_READY Worker absent or stopping.
 * @note Thread-safe; creates no
 * worker, interrupts neither a healthy connection nor an active handshake,
 * and does not wait for connection completion.
 */
gw_status_enum_t mqtt_client_reconnect(void);

/**
 * @brief Read the latest successful-CONNACK state.
 * @return true after connection acceptance until a disconnect/stop is recorded; otherwise false.
 * @note Thread-safe; snapshot may change immediately and does not imply subscription readiness.
 */
bool mqtt_client_is_connected(void);

/**
 * @brief Read readiness of all caller-selected subscriptions.
 * @return true when connected and every registered filter is SUBACKed;
 * a connected client with no filters is ready immediately.
 * @note Thread-safe; does not establish JSON/UART/STM32 readiness.
 */
bool mqtt_client_is_ready(void);

/**
 * @brief Request shutdown, join the worker and release MQTT resources.
 * @retval GW_OK Stopped or not running; no callbacks remain after return.
 * @retval GW_BUSY Called from the worker itself; self-join prevented.
 * @retval GW_IO_ERROR Join failed; resources are not released.
 * @pre Serialize on the application thread.
 * @warning Waits for the worker; system DNS/callbacks
 * can extend shutdown latency, so there is no strict time bound.
 * @note Retains configuration for a later start.
 */
gw_status_enum_t mqtt_client_stop(void);

#ifdef __cplusplus
}
#endif
#endif
