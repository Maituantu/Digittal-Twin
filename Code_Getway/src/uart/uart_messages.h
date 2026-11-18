#ifndef UART_MESSAGES_H
#define UART_MESSAGES_H

#include <stdint.h>
#include "common/gw_types.h"
#include "msg/kit_messages.h"
#include "uart/bts/bts_frame_message.h"
#define UART_MAX_PAYLOAD DEFAULT_MAX_SIZE_DATA
#define UART_MAX_FRAME (UART_MAX_PAYLOAD + 5)
struct uart_frame_struct_t
{
    uint8_t id;
    uint8_t length;
    uint8_t data[UART_MAX_PAYLOAD];
};
struct uart_parser_struct_t
{
    uint8_t bytes[UART_MAX_FRAME];
    uint16_t count;
};

gw_status_enum_t uart_encode_command(const kit_command_struct_t *command, uint8_t *output, uint16_t capacity, uint16_t *written);
// Feed arbitrary fragments one byte at a time; returns true only for a CRC-valid frame.

bool uart_parser_feed(uart_parser_struct_t *parser, uint8_t byte, uart_frame_struct_t *frame);

gw_status_enum_t uart_decode_ack(const uart_frame_struct_t *frame, kit_ack_struct_t *ack);
// Float32 magnetic field: packed 16 or ESP32 aligned 20 bytes. Legacy double: 24/32 bytes.

gw_status_enum_t uart_decode_telemetry(const uart_frame_struct_t *frame, kit_telemetry_struct_t *sample);

#endif
