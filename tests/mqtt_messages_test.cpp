#include "mqtt/mqtt_messages.h"

#include <jansson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++s_failures; } } while (0)

static uint16_t command_value(const kit_command_struct_t *command)
{
    switch (command->id)
    {
        case KIT_MSG_SET_POWER: return command->data.power_on;
        case KIT_MSG_SET_VOLTAGE: return command->data.voltage_v;
        case KIT_MSG_SET_SCALE: return command->data.scale_code;
        case KIT_MSG_SET_RULER: return command->data.ruler_position_mm;
        default: return command->data.action;
    }
}

static void check_command(const char *suffix, const char *payload, uint8_t id, uint16_t value)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "lab/kit/KIT-001/cmd/%s", suffix);
    kit_command_struct_t command = {};
    CHECK(mqtt_messages_decode_command("KIT-001", topic, payload, strlen(payload), &command) == GW_OK);
    CHECK(command.id == id);
    CHECK(command_value(&command) == value);
}

static void check_rejected(const char *suffix, const char *payload)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "lab/kit/KIT-001/cmd/%s", suffix);
    kit_command_struct_t output;
    memset(&output, 0xa5, sizeof(output));
    unsigned char before[sizeof(output)];
    memcpy(before, &output, sizeof(output));
    CHECK(mqtt_messages_decode_command("KIT-001", topic, payload, strlen(payload), &output) != GW_OK);
    CHECK(memcmp(before, &output, sizeof(output)) == 0);
}

static void test_commands(void)
{
    check_command("reset", "{\"action\":\"start\"}", KIT_MSG_RESET_ACTION, 1);
    check_command("reset", "{\"action\":\"stop\"}", KIT_MSG_RESET_ACTION, 2);
    check_command("reset", "{\"action\":\"calib\"}", KIT_MSG_RESET_ACTION, 3);
    check_command("power", "{\"action\":\"off\"}", KIT_MSG_SET_POWER, 0);
    check_command("power", "{\"action\":\"on\"}", KIT_MSG_SET_POWER, 1);
    for (int value = 0; value <= 12; value += 3)
    {
        char payload[64];
        snprintf(payload, sizeof(payload), "{\"voltage\":%d}", value);
        check_command("voltage", payload, KIT_MSG_SET_VOLTAGE, (uint16_t)value);
    }
    check_command("scale", "{\"scale\":\"1.999\"}", KIT_MSG_SET_SCALE, 0);
    check_command("scale", "{\"scale\":\"19.99\"}", KIT_MSG_SET_SCALE, 1);
    check_command("scale", "{\"scale\":\"199.9\"}", KIT_MSG_SET_SCALE, 2);
    check_command("ruler", "{\"position\":0}", KIT_MSG_SET_RULER, 0);
    check_command("ruler", "{\"position\":120}", KIT_MSG_SET_RULER, 120);
    check_command("ruler", "{\"position\":300}", KIT_MSG_SET_RULER, 300);
    check_command("ruler", " {\"position\":120.0,\"kit_code\":\"KIT-001\",\"extra\":\"metadata\"}\n", KIT_MSG_SET_RULER, 120);
    check_command("shutdown", "{\"action\":\"shutdown\"}", KIT_MSG_SHUTDOWN_KIT, 1);

    check_rejected("reset", "{}");
    check_rejected("reset", "[]");
    check_rejected("reset", "{\"action\":\"restart\"}");
    check_rejected("reset", "{\"Action\":\"start\"}");
    check_rejected("reset", "{\"action\":\"start\",\"action\":\"stop\"}");
    check_rejected("reset", "{\"action\":\"start\",\"\\u0061ction\":\"stop\"}");
    check_rejected("reset", "{\"action\":\"start\\u0000bad\"}");
    check_rejected("reset", "{\"action\":\"start\",\"kit_code\":\"OTHER\"}");
    check_rejected("reset", "{\"action\":\"start\",\"kit_code\":null}");
    check_rejected("reset", "{\"action\":\"start\"} garbage");
    check_rejected("reset/extra", "{\"action\":\"start\"}");
    check_rejected("power", "{\"action\":true}");
    check_rejected("power", "{\"action\":\"ON\"}");
    check_rejected("voltage", "{\"voltage\":8}");
    check_rejected("voltage", "{\"voltage\":true}");
    check_rejected("voltage", "{\"voltage\":\"9\"}");
    check_rejected("voltage", "{\"voltage\":9.1}");
    check_rejected("voltage", "{\"voltage\":NaN}");
    check_rejected("voltage", "{\"voltage\":1e999}");
    check_rejected("scale", "{\"scale\":19.99}");
    check_rejected("scale", "{\"scale\":\"19.990\"}");
    check_rejected("ruler", "{\"position\":-1}");
    check_rejected("ruler", "{\"position\":301}");
    check_rejected("ruler", "{\"position\":1.5}");
    check_rejected("ruler", "{\"position\":false}");
    check_rejected("ruler", "{\"position\":01}");
    check_rejected("shutdown", "{\"action\":\"off\"}");
    const char payload[] = "{\"position\":120}";
    kit_command_struct_t output = {};
    char not_terminated[sizeof(payload) - 1];
    memcpy(not_terminated, payload, sizeof(not_terminated));
    CHECK(mqtt_messages_decode_command("KIT-001", "lab/kit/KIT-001/cmd/ruler", not_terminated, sizeof(not_terminated), &output) == GW_OK);
    CHECK(output.data.ruler_position_mm == 120);
    CHECK(mqtt_messages_decode_command("KIT-001", "lab/kit/OTHER/cmd/ruler", payload, strlen(payload), &output) == GW_INVALID_ARG);
    CHECK(mqtt_messages_decode_command("KIT-001", "lab/kit/KIT-001/cmd/unknown", payload, strlen(payload), &output) == GW_UNSUPPORTED);
    CHECK(mqtt_messages_decode_command("KIT-001", "lab/kit/KIT-001/cmd/ruler", payload, sizeof(payload), &output) == GW_INVALID_ARG);
    CHECK(mqtt_messages_decode_command("KIT-001", "lab/kit/KIT-001/cmd/ruler", NULL, 0, &output) == GW_INVALID_ARG);
    check_rejected("reset", "{\"action\":\"start\",\"extra\":\"\xff\"}");
    char oversized[4097];
    memset(oversized, ' ', sizeof(oversized));
    CHECK(mqtt_messages_decode_command("KIT-001", "lab/kit/KIT-001/cmd/reset", oversized, sizeof(oversized), &output) == GW_INVALID_ARG);
    CHECK(mqtt_messages_decode_command(NULL, "lab/kit/KIT-001/cmd/reset", payload, strlen(payload), &output) == GW_INVALID_ARG);
    CHECK(mqtt_messages_decode_command("KIT-001", NULL, payload, strlen(payload), &output) == GW_INVALID_ARG);
    CHECK(mqtt_messages_decode_command("KIT-001", "lab/kit/KIT-001/cmd/reset", payload, strlen(payload), NULL) == GW_INVALID_ARG);
}

static void test_telemetry(void)
{
    /* Values from the exact IEEE-754 words in HTML section 5.3. UART CRC is separate. */
    static_assert(sizeof(float) == 4 && sizeof(double) == 8, "IEEE-754 sizes required");
    uint32_t voltage_bits = UINT32_C(0x40c23d71);
    uint32_t current_bits = UINT32_C(0x42fb0000);
    uint64_t magnetic_bits = UINT64_C(0x405edd3c07ee0b0b);
    kit_telemetry_struct_t sample = {};
    memcpy(&sample.voltage_v, &voltage_bits, sizeof(float));
    memcpy(&sample.current_ma, &current_bits, sizeof(float));
    memcpy(&sample.magnetic_field_mt, &magnetic_bits, sizeof(double));
    sample.power_on = 1;
    sample.scale_code = 2;
    sample.ruler_position_mm = 150;
    char output[1024];
    size_t written;
    /* 2026-10-14T08:10:29.500Z */
    const int64_t received_at = INT64_C(1791965429500);
    CHECK(mqtt_messages_encode_telemetry("KIT-001", received_at, "c2ffb0d4-5901-4c2c-8c4b-141565182757", 0, &sample, output, sizeof(output), &written) == GW_OK);
    CHECK(written == strlen(output));
    json_error_t error;
    json_t *object = json_loadb(output, written, 0, &error);
    CHECK(object != NULL);
    if (object)
    {
        CHECK(json_object_size(object) == 9);
        CHECK(strcmp(json_string_value(json_object_get(object, "kit_code")), "KIT-001") == 0);
        CHECK(strcmp(json_string_value(json_object_get(object, "attempt_id")), "c2ffb0d4-5901-4c2c-8c4b-141565182757") == 0);
        CHECK(json_integer_value(json_object_get(object, "seq")) == 0);
        CHECK(json_number_value(json_object_get(object, "voltage")) == (double)sample.voltage_v);
        CHECK(json_number_value(json_object_get(object, "current_ma")) == 125.5);
        CHECK(json_is_true(json_object_get(object, "power_on")));
        CHECK(json_number_value(json_object_get(object, "magnetic_field")) == 123.456789);
        CHECK(strcmp(json_string_value(json_object_get(object, "scale")), "199.9") == 0);
        CHECK(json_integer_value(json_object_get(object, "ruler_position")) == 150);
        CHECK(json_object_get(object, "magnetic_field_mt") == NULL);
        json_decref(object);
    }
    printf("HTML telemetry example: %s\n", output);
    size_t needed = written;
    CHECK(mqtt_messages_encode_telemetry("KIT-001", received_at, "c2ffb0d4-5901-4c2c-8c4b-141565182757", 0, &sample, output, needed, &written) == GW_BUFFER_TOO_SMALL);
    CHECK(written == 0 && output[0] == '\0');
    CHECK(mqtt_messages_encode_telemetry("KIT-001", received_at, "c2ffb0d4-5901-4c2c-8c4b-141565182757", 0, &sample, output, needed + 1, &written) == GW_OK);
    const kit_telemetry_struct_t original = sample;
    for (int index = 0; index < 9; ++index)
    {
        sample = original;
        switch (index)
        {
            case 0: sample.voltage_v = NAN; break;
            case 1: sample.current_ma = INFINITY; break;
            case 2: sample.power_on = 2; break;
            case 3: sample.magnetic_field_mt = -0.01; break;
            case 4: sample.magnetic_field_mt = 200.01; break;
            case 5: sample.magnetic_field_mt = NAN; break;
            case 6: sample.scale_code = 3; break;
            case 7: sample.ruler_position_mm = 301; break;
            case 8: sample.magnetic_field_mt = INFINITY; break;
        }
        CHECK(mqtt_messages_encode_telemetry("KIT-001", received_at, "attempt", 0, &sample, output, sizeof(output), &written) == GW_INVALID_ARG);
        CHECK(written == 0 && output[0] == '\0');
    }
    sample = original;
    CHECK(mqtt_messages_encode_telemetry("KIT-001", -1, "attempt", 0, &sample, output, sizeof(output), &written) == GW_OK);
    CHECK(mqtt_messages_encode_telemetry("BAD/KIT", received_at, "attempt", 0, &sample, output, sizeof(output), &written) == GW_INVALID_ARG);
    CHECK(mqtt_messages_encode_telemetry("KIT-001", received_at, NULL, 0, &sample, output, sizeof(output), &written) == GW_OK);
    CHECK(mqtt_messages_encode_telemetry("KIT-001", received_at, "attempt", 0, NULL, output, sizeof(output), &written) == GW_INVALID_ARG);
    CHECK(mqtt_messages_encode_telemetry("KIT-001", received_at, "attempt", 0, &sample, NULL, sizeof(output), &written) == GW_INVALID_ARG);
    CHECK(mqtt_messages_encode_telemetry("KIT-001", received_at, "attempt", 0, &sample, output, sizeof(output), NULL) == GW_INVALID_ARG);
    sample.magnetic_field_mt = 200;
    sample.ruler_position_mm = 300;
    sample.power_on = 0;
    for (uint8_t scale = 0; scale < 3; ++scale)
    {
        sample.scale_code = scale;
        CHECK(mqtt_messages_encode_telemetry("KIT-001", 0, "attempt", scale, &sample, output, sizeof(output), &written) == GW_OK);
    }
}

int main(void)
{
    CHECK(mqtt_messages_is_supported());
    test_commands();
    test_telemetry();
    if (s_failures)
    {
        fprintf(stderr, "%d JSON codec checks failed\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("PASS: six commands, strict validation, exact HTML telemetry schema and buffer handling");
    return EXIT_SUCCESS;
}
