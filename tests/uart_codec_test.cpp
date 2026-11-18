#include "test/uart_test.h"
#include "uart/uart_messages.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

static bool test_float_telemetry()
{
    const uint8_t bytes[] = {0xAA,0x81,0x10,0x33,0x33,0x53,0x40,0x00,0x00,0xC8,0x42,0x01,0x49,0x9D,0x00,0x3E,0x00,0x2C,0x01,0xF7,0x8E};
    uart_parser_struct_t parser = {};
    uart_frame_struct_t frame = {};
    bool received = false;
    for (uint8_t byte : bytes)
        received = uart_parser_feed(&parser, byte, &frame) || received;
    kit_telemetry_struct_t sample = {};
    if (!received || uart_decode_telemetry(&frame, &sample) != GW_OK ||
        fabs(sample.voltage_v - 3.3) > 0.00001 || sample.current_ma != 100 ||
        sample.power_on != 1 || fabs(sample.magnetic_field_mt - 0.1256) > 0.000001 ||
        sample.scale_code != 0 || sample.ruler_position_mm != 300)
        return false;

    // Reject invalid fields and leave the caller's previous sample intact.
    const kit_telemetry_struct_t previous = sample;
    frame.data[13] = 3;
    if (uart_decode_telemetry(&frame, &sample) != GW_INVALID_ARG ||
        memcmp(&sample, &previous, sizeof(sample)) != 0) return false;
    frame.data[13] = 0;
    frame.data[14] = 0x2D; // 301 mm
    if (uart_decode_telemetry(&frame, &sample) != GW_INVALID_ARG) return false;
    frame.data[14] = 0x2C;
    frame.data[9] = 0; frame.data[10] = 0; frame.data[11] = 0x80; frame.data[12] = 0x7F;
    return uart_decode_telemetry(&frame, &sample) == GW_INVALID_ARG; // +infinity
}

static bool test_esp32_aligned_telemetry()
{
    const uint8_t bytes[] = {0xAA,0x81,0x14,0x33,0x33,0x53,0x40,0,0,0xC8,0x42,1,0x21,0xFB,0x3F,0x49,0x9D,0,0x3E,0,0x5F,0x2C,1,0xC1,0x5A};
    uart_parser_struct_t parser = {};
    uart_frame_struct_t frame = {};
    bool received = false;
    for (uint8_t byte : bytes)
        received = uart_parser_feed(&parser, byte, &frame) || received;
    kit_telemetry_struct_t sample = {};
    if (!received || uart_decode_telemetry(&frame, &sample) != GW_OK ||
        fabs(sample.voltage_v - 3.3) > 0.00001 || sample.current_ma != 100 ||
        sample.power_on != 1 || fabs(sample.magnetic_field_mt - 0.1256) > 0.000001 ||
        sample.scale_code != 0 || sample.ruler_position_mm != 300) return false;
    // Padding must not affect decoding.
    frame.data[9] = frame.data[10] = frame.data[11] = frame.data[17] = 0xFF;
    if (uart_decode_telemetry(&frame, &sample) != GW_OK) return false;
    frame.data[16] = 3;
    if (uart_decode_telemetry(&frame, &sample) != GW_INVALID_ARG) return false;
    frame.data[16] = 0;
    frame.data[18] = 0x2D;
    return uart_decode_telemetry(&frame, &sample) == GW_INVALID_ARG;
}

int main()
{
    if (!test_float_telemetry() || !test_esp32_aligned_telemetry())
    {
        fprintf(stderr, "Packed float telemetry test failed\n");
        return EXIT_FAILURE;
    }
    if (uart_test_config() != GW_OK)
        return EXIT_FAILURE;
    return uart_test_run() == GW_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
