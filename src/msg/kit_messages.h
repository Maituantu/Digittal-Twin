/**
 * @file kit_messages.h
 * @brief Hardware-independent message types from the UART v1.0 specification.
 * @note Types only: UART stream parsing, framing and CRC are implemented separately.
 * Share this header with the future UART codec; do not duplicate these structs.
 */
#ifndef KIT_MESSAGES_H
#define KIT_MESSAGES_H

#include <stdint.h>

#define KIT_ATTEMPT_ID_CAPACITY 128

typedef enum
{
    KIT_MSG_RESET_ACTION = 0x01,
    KIT_MSG_SET_POWER = 0x02,
    KIT_MSG_SET_VOLTAGE = 0x03,
    KIT_MSG_SET_SCALE = 0x04,
    KIT_MSG_SET_RULER = 0x05,
    KIT_MSG_SHUTDOWN_KIT = 0x06,
    KIT_MSG_ACK_NACK = 0x80,
    KIT_MSG_TELEMETRY = 0x81
} kit_message_id_enum_t;

typedef enum
{
    KIT_RESET_START = 0x01,
    KIT_RESET_STOP = 0x02,
    KIT_RESET_CALIB = 0x03
} kit_reset_action_enum_t;

typedef enum
{
    KIT_SCALE_1_999 = 0x00,
    KIT_SCALE_19_99 = 0x01,
    KIT_SCALE_199_9 = 0x02
} kit_scale_enum_t;

typedef union
{
    uint8_t action;
    uint8_t power_on;
    uint8_t voltage_v;
    uint8_t scale_code;
    uint16_t ruler_position_mm;
} kit_command_data_union_t;

/** @brief Typed command; read only the union member corresponding to id. */
typedef struct
{
    uint8_t id;
    kit_command_data_union_t data;
    char attempt_id[KIT_ATTEMPT_ID_CAPACITY];
    uint64_t command_seq;
    bool has_attempt_id;
    bool has_command_seq;
} kit_command_struct_t;

/** @brief Internal UART acknowledgement; not a MQTT telemetry payload. */
typedef struct
{
    uint8_t ack_code;
    uint8_t command_id;
    uint8_t error_code;
} kit_ack_struct_t;

/** @brief Decoded actual measurements; never transmit sizeof(this struct). */
typedef struct
{
    float voltage_v;
    float current_ma;
    uint8_t power_on;
    double magnetic_field_mt;
    uint8_t scale_code;
    uint16_t ruler_position_mm;
} kit_telemetry_struct_t;

#endif
