#include "test/uart_test.h"
#include "uart/uart_messages.h"

#include <stdio.h>

static void print_frame(const uint8_t *frame_bytes, uint16_t frame_length)
{
    for (uint16_t byte_index = 0; byte_index < frame_length; byte_index++)
    {
        printf(" %02X", frame_bytes[byte_index]);
    }
    putchar('\n');
}

static void show_command_frames(void)
{
    kit_command_struct_t commands[6] = {};
    commands[0].id = KIT_MSG_RESET_ACTION;
    commands[0].data.action = KIT_RESET_START;
    commands[1].id = KIT_MSG_SET_POWER;
    commands[1].data.power_on = 1;
    commands[2].id = KIT_MSG_SET_VOLTAGE;
    commands[2].data.voltage_v = 9;
    commands[3].id = KIT_MSG_SET_SCALE;
    commands[3].data.scale_code = KIT_SCALE_19_99;
    commands[4].id = KIT_MSG_SET_RULER;
    commands[4].data.ruler_position_mm = 300;
    commands[5].id = KIT_MSG_SHUTDOWN_KIT;
    commands[5].data.action = 1;

    for (const kit_command_struct_t &command : commands)
    {
        uint8_t frame_bytes[UART_MAX_FRAME] = {};
        uint16_t frame_length = 0;
        uart_encode_command(&command, frame_bytes, sizeof(frame_bytes), &frame_length);

        printf("Command 0x%02X:", command.id);
        print_frame(frame_bytes, frame_length);
    }
}

static void show_ack_frames(void)
{
    for (uint8_t ack_code = 0; ack_code <= 1; ack_code++)
    {
        uint8_t ack_payload[] = {ack_code, KIT_MSG_SET_VOLTAGE, uint8_t(ack_code ? 4 : 0)};
        uint8_t frame_bytes[UART_MAX_FRAME] = {};
        int16_t frame_length = CreateMessage(KIT_MSG_ACK_NACK, sizeof(ack_payload), ack_payload, frame_bytes);
        uart_parser_struct_t parser = {};
        uart_frame_struct_t received_frame = {};

        for (int16_t byte_index = 0; byte_index < frame_length; byte_index++)
        {
            if (uart_parser_feed(&parser, frame_bytes[byte_index], &received_frame))
            {
                kit_ack_struct_t acknowledgement = {};
                uart_decode_ack(&received_frame, &acknowledgement);
                printf("ACK code=%u command=0x%02X error=%u\n",
                       acknowledgement.ack_code, acknowledgement.command_id, acknowledgement.error_code);
            }
        }
    }
}

static void show_telemetry_frames(void)
{
    struct telemetry_layout_t
    {
        uint8_t payload_length;
        uint8_t magnetic_field_offset;
        uint8_t scale_offset;
        uint8_t ruler_offset;
    };
    const telemetry_layout_t telemetry_layouts[] = {
        {16, 9, 13, 14},
        {20, 12, 16, 18},
        {24, 12, 20, 22},
        {32, 16, 24, 26}
    };

    for (const telemetry_layout_t &layout : telemetry_layouts)
    {
        uint8_t telemetry_payload[32] = {};
        // Little-endian IEEE754: voltage = 9.0f, magnetic field = 1.5.
        telemetry_payload[2] = 0x10;
        telemetry_payload[3] = 0x41;
        telemetry_payload[8] = 1;
        if (layout.payload_length <= 20)
        {
            telemetry_payload[layout.magnetic_field_offset + 2] = 0xc0;
            telemetry_payload[layout.magnetic_field_offset + 3] = 0x3f;
        }
        else
        {
            telemetry_payload[layout.magnetic_field_offset + 6] = 0xf8;
            telemetry_payload[layout.magnetic_field_offset + 7] = 0x3f;
        }
        telemetry_payload[layout.scale_offset] = KIT_SCALE_199_9;
        telemetry_payload[layout.ruler_offset] = 120;

        uint8_t frame_bytes[UART_MAX_FRAME] = {};
        int16_t frame_length = CreateMessage(KIT_MSG_TELEMETRY, layout.payload_length, telemetry_payload, frame_bytes);
        uart_parser_struct_t parser = {};
        uart_frame_struct_t received_frame = {};

        for (int16_t byte_index = 0; byte_index < frame_length; byte_index++)
        {
            if (uart_parser_feed(&parser, frame_bytes[byte_index], &received_frame))
            {
                kit_telemetry_struct_t telemetry = {};
                uart_decode_telemetry(&received_frame, &telemetry);
                printf("Telemetry (%u bytes): voltage=%.1f V current=%.1f mA magnetic=%.1f mT power=%u scale=%u ruler=%u mm\n",
                       layout.payload_length, telemetry.voltage_v, telemetry.current_ma,
                       telemetry.magnetic_field_mt, telemetry.power_on, telemetry.scale_code,
                       telemetry.ruler_position_mm);
            }
        }
    }
}

gw_status_enum_t uart_test_config(void)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    puts("UART message examples (offline, BTS framing)");
    return GW_OK;
}

gw_status_enum_t uart_test_run(void)
{
    show_command_frames();
    show_ack_frames();
    show_telemetry_frames();
    return GW_OK;
}
