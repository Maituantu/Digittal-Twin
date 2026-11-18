/**
 * @file mqtt_messages.h
 * @brief MQTT JSON codec for the supplied Pi/STM32 UART specification v1.0.
 * @note Several JSON field/topic names in that document remain draft. These
 * APIs implement the document's examples, not a verified server deployment.
 */
#ifndef MQTT_MESSAGES_H
#define MQTT_MESSAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/gw_types.h"
#include "msg/kit_messages.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Return true when the JSON codec is compiled in. */
bool mqtt_messages_is_supported(void);

/** @brief Return current Unix time in milliseconds, or -1 if the clock fails. */
int64_t mqtt_messages_now_ms(void);

/**
 * @brief Validate topic/JSON and decode one of the six documented Web commands.
 * @param[in] kit_code Expected kit, from application configuration, not payload.
 * @param[in] topic Actual NUL-terminated lab/kit/{kit_code}/cmd/... topic.
 * @param[in] payload Length-delimited JSON object; no terminator is required.
 * @param[in] length Payload bytes, 1..4096; embedded NUL is rejected.
 * @param[out] output Decoded command; unchanged on failure.
 * @retval GW_OK Command decoded, not authorized or executed.
 * @retval GW_INVALID_ARG Invalid topic/kit/JSON, missing field, wrong type/value.
 * @retval GW_UNSUPPORTED Topic is not one of the six documented command types.
 * @retval GW_IO_ERROR JSON allocation failed.
 * @note Duplicate keys, invalid UTF-8, trailing garbage, NaN/Inf and booleans
 * used as numbers are rejected. Optional payload kit_code must match.
 * Unknown extra fields are ignored; field names and scale strings are exact.
 * Session freshness, retained policy, permissions and deduplication belong to
 * the caller. Shutdown requires system-level orchestration, not just UART.
 */
gw_status_enum_t mqtt_messages_decode_command(const char *kit_code, const char *topic, const void *payload, size_t length, kit_command_struct_t *output);

/**
 * @brief Encode an actual STM32 telemetry snapshot as the documented JSON.
 * @param[in] kit_code Nonempty kit identifier, a single MQTT topic level, <=127 bytes.
 * @param[in] received_at_unix_ms Retained for API compatibility; not serialized.
 * @param[in] attempt_id Current server-issued attempt UUID, or NULL/empty when
 * no attempt has been started yet.
 * @param[in] sequence Sequence within the current attempt.
 * @param[in] sample Measurements already decoded from a CRC-valid UART frame.
 * @param[out] output Caller buffer; empty on failure if capacity is nonzero.
 * @param[in] capacity Buffer bytes, including the terminating NUL.
 * @param[out] written JSON bytes excluding NUL; set to zero on failure.
 * @retval GW_OK JSON produced; send exactly written bytes to mqtt_client_publish.
 * @retval GW_INVALID_ARG Invalid argument, timestamp, nonfinite or out-of-range sample.
 * @retval GW_BUFFER_TOO_SMALL Output does not fit; partial JSON is never returned.
 * @retval GW_IO_ERROR JSON allocation/serialization failed.
 * @note Uses current_ma in mA, magnetic_field in mT and ruler_position in mm.
 * Telemetry without an active attempt omits attempt_id so live measurements
 * remain available before the server sends reset/start.
 * No clamping or LED rounding. Does not enforce session/publish permissions,
 * freshness or 2 Hz scheduling. No UART parsing, topic construction or network I/O.
 */
gw_status_enum_t mqtt_messages_encode_telemetry(const char *kit_code, int64_t received_at_unix_ms, const char *attempt_id, uint64_t sequence, const kit_telemetry_struct_t *sample, char *output, size_t capacity, size_t *written);

/** @brief Encode the current attempt heartbeat JSON. */
gw_status_enum_t mqtt_messages_encode_heartbeat(const char *kit_code, const char *attempt_id, uint64_t sequence, char *output, size_t capacity, size_t *written);

#ifdef __cplusplus
}
#endif
#endif
