#include "uart/uart_messages.h"

#include <string.h>
#include <math.h>
// Telemetry uses 4-byte float and 8-byte double on Pi/ESP32/STM32.
static_assert(sizeof(float) == 4 && sizeof(double) == 8, "Unsupported telemetry types");

/*** Encode command payload and build a BTS frame ***/
gw_status_enum_t uart_encode_command(const kit_command_struct_t *command, uint8_t *output, uint16_t capacity, uint16_t *written)
{
    if (!written)
    {
        return GW_INVALID_ARG;
    }
    *written = 0;
    if (!command || !output)
    {
        return GW_INVALID_ARG;
    }
    uint8_t payload[2] = {};
    uint8_t payload_length = 1;
    bool valid = true;

    switch (command->id)
    {
        case KIT_MSG_RESET_ACTION:
            payload[0] = command->data.action;
            valid = payload[0] >= KIT_RESET_START && payload[0] <= KIT_RESET_CALIB;
            break;
        case KIT_MSG_SET_POWER:
            payload[0] = command->data.power_on;
            valid = payload[0] <= 1;
            break;
        case KIT_MSG_SET_VOLTAGE:
            payload[0] = command->data.voltage_v;
            valid = payload[0] <= 12 && payload[0] % 3 == 0;
            break;
        case KIT_MSG_SET_SCALE:
            payload[0] = command->data.scale_code;
            valid = payload[0] <= KIT_SCALE_199_9;
            break;
        case KIT_MSG_SET_RULER:
            valid = command->data.ruler_position_mm <= 300;
            payload_length = 2;
            payload[0] = command->data.ruler_position_mm & 0xff;
            payload[1] = command->data.ruler_position_mm >> 8;
            break;
        case KIT_MSG_SHUTDOWN_KIT:
            payload[0] = command->data.action;
            valid = payload[0] == 1;
            break;
        default:
            return GW_UNSUPPORTED;
    }

    if (!valid)
    {
        return GW_INVALID_ARG;
    }

    // BTS adds the header, command ID, length and two CRC bytes.
    if (capacity < payload_length + DEFAULT_BYTE + DEFAULT_BYTE_CHECKSUM)
    {
        return GW_BUFFER_TOO_SMALL;
    }
    int16_t frame_length = CreateMessage(command->id, payload_length, payload, output);
    if (frame_length < 0)
    {
        return GW_INVALID_ARG;
    }
    *written = (uint16_t)frame_length;
    return GW_OK;
}

/*** Assemble incoming bytes and validate complete BTS frames ***/
bool uart_parser_feed(uart_parser_struct_t *parser, uint8_t byte, uart_frame_struct_t *frame)
{
    if (!parser || !frame)
    {
        return false;
    }
    if (parser->count >= UART_MAX_FRAME)
    {
        parser->count = 0;
    }

    // Collect bytes; skip noise or a bad frame one byte at a time.
    parser->bytes[parser->count++] = byte;
    while (parser->count)
    {
        if (parser->bytes[0] == START_BYTE && parser->count < DEFAULT_BYTE)
        {
            return false;
        }
        if (parser->bytes[0] == START_BYTE && parser->bytes[2] <= UART_MAX_PAYLOAD)
        {
            uint16_t frame_length = parser->bytes[2] + DEFAULT_BYTE + DEFAULT_BYTE_CHECKSUM;
            if (parser->count < frame_length)
            {
                return false;
            }
            messageFrameMsg_t bts_frame = {};
            // DetectMessage has no input-length argument: call only after bounds
            // and complete-frame checks above.
            if (DetectMessage(parser->bytes, &bts_frame) == frame_length)
            {
                frame->id = bts_frame.TypeMessage;
                frame->length = (uint8_t)bts_frame.Length;
                memcpy(frame->data, bts_frame.Data, bts_frame.Length);
                parser->count -= frame_length;
                memmove(parser->bytes, parser->bytes + frame_length, parser->count);
                return true;
            }
        }
        --parser->count;
        memmove(parser->bytes, parser->bytes + 1, parser->count);
    }
    return false;
}

/*** Decode ACK/NACK payload ***/
gw_status_enum_t uart_decode_ack(const uart_frame_struct_t *frame, kit_ack_struct_t *ack)
{
    if (!frame || !ack || frame->id != KIT_MSG_ACK_NACK || frame->length != 3 || frame->data[0] > 1)
    {
        return GW_INVALID_ARG;
    }
    ack->ack_code = frame->data[0];
    ack->command_id = frame->data[1];
    ack->error_code = frame->data[2];
    return GW_OK;
}

/*** Read little-endian bytes without depending on host byte order ***/
static uint64_t read_little_endian(const uint8_t *bytes, uint8_t byte_count)
{
    uint64_t value = 0;
    for (uint8_t i = 0; i < byte_count; i++)
    {
        value |= uint64_t(bytes[i]) << (8 * i);
    }
    return value;
}

/*** Decode telemetry payload ***/
gw_status_enum_t uart_decode_telemetry(const uart_frame_struct_t *frame, kit_telemetry_struct_t *sample)
{
    if (!frame || !sample || frame->id != KIT_MSG_TELEMETRY)
    {
        return GW_INVALID_ARG;
    }

    // Legacy MCU firmware sends sizeof(struct), including ABI padding.
    // Voltage/current/power always start at bytes 0/4/8; later offsets vary.
    uint8_t magnetic_offset;
    uint8_t scale_offset;
    uint8_t ruler_offset;

    if (frame->length == 16)
    {
        magnetic_offset = 9;
        scale_offset = 13;
        ruler_offset = 14;
    }
    else if (frame->length == 20)
    {
        // ESP32 float struct: padding at 9..11 and 17.
        magnetic_offset = 12;
        scale_offset = 16;
        ruler_offset = 18;
    }
    else if (frame->length == 24)
    {
        magnetic_offset = 12;
        scale_offset = 20;
        ruler_offset = 22;
    }
    else if (frame->length == 32)
    {
        magnetic_offset = 16;
        scale_offset = 24;
        ruler_offset = 26;
    }
    else
    {
        return GW_INVALID_ARG;
    }

    kit_telemetry_struct_t decoded_sample = {};

    // Copy IEEE754 bits safely, without relying on struct padding.
    uint32_t voltage_bits = uint32_t(read_little_endian(frame->data, 4));
    uint32_t current_bits = uint32_t(read_little_endian(frame->data + 4, 4));
    memcpy(&decoded_sample.voltage_v, &voltage_bits, 4);
    memcpy(&decoded_sample.current_ma, &current_bits, 4);
    if (frame->length == 16 || frame->length == 20)
    {
        // Packed/aligned ESP32 telemetry uses IEEE754 float32.
        uint32_t magnetic_bits = uint32_t(read_little_endian(frame->data + magnetic_offset, 4));
        float magnetic_field;
        memcpy(&magnetic_field, &magnetic_bits, 4);
        decoded_sample.magnetic_field_mt = magnetic_field;
    }
    else
    {
        uint64_t magnetic_bits = read_little_endian(frame->data + magnetic_offset, 8);
        memcpy(&decoded_sample.magnetic_field_mt, &magnetic_bits, 8);
    }
    decoded_sample.power_on = frame->data[8];
    decoded_sample.scale_code = frame->data[scale_offset];
    decoded_sample.ruler_position_mm = uint16_t(read_little_endian(frame->data + ruler_offset, 2));

    if (!isfinite(decoded_sample.voltage_v) ||
        !isfinite(decoded_sample.current_ma) ||
        !isfinite(decoded_sample.magnetic_field_mt) ||
        decoded_sample.power_on > 1 ||
        decoded_sample.scale_code > 2 ||
        decoded_sample.ruler_position_mm > 300)
    {
        return GW_INVALID_ARG;
    }
    *sample = decoded_sample;
    return GW_OK;
}

