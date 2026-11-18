#include "test/mqtt_test_messages.h"
#include "mqtt/mqtt_messages.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t s_stop_requested;
static bool s_examples_pending;

typedef struct
{
    const char *suffix;
    const char *payload;
    uint8_t expected_id;
} mqtt_message_example_struct_t;

static const mqtt_message_example_struct_t s_examples[] =
{
    {"reset", R"({"action":"start"})", KIT_MSG_RESET_ACTION},
    {"power", R"({"action":"on"})", KIT_MSG_SET_POWER},
    {"voltage", R"({"voltage":9})", KIT_MSG_SET_VOLTAGE},
    {"scale", R"({"scale":"19.99"})", KIT_MSG_SET_SCALE},
    {"ruler", R"({"position":120})", KIT_MSG_SET_RULER},
    {"shutdown", R"({"action":"shutdown"})", KIT_MSG_SHUTDOWN_KIT}
};

/** @brief Set a signal-safe flag for the offline test process. */
static void handle_signal(int)
{
    s_stop_requested = 1;
}

/** @brief Print only the union member selected by the command ID. */
static void print_command(const kit_command_struct_t *command)
{
    printf("Command decoded: id=0x%02X ", (unsigned int)command->id);
    switch (command->id)
    {
        case KIT_MSG_RESET_ACTION:
        case KIT_MSG_SHUTDOWN_KIT:
            printf("action=%u", (unsigned int)command->data.action);
            break;
        case KIT_MSG_SET_POWER:
            printf("power_on=%u", (unsigned int)command->data.power_on);
            break;
        case KIT_MSG_SET_VOLTAGE:
            printf("voltage_v=%u", (unsigned int)command->data.voltage_v);
            break;
        case KIT_MSG_SET_SCALE:
            printf("scale_code=%u", (unsigned int)command->data.scale_code);
            break;
        case KIT_MSG_SET_RULER:
            printf("ruler_position_mm=%u", (unsigned int)command->data.ruler_position_mm);
            break;
        default:
            break;
    }
    printf(" (no UART/hardware action)\n");
}

/** @brief Stop the local examples if a codec operation fails. */
static void check_example(gw_status_enum_t status, const char *name)
{
    if (status == GW_OK)
    {
        return;
    }
    fprintf(stderr, "Local %s example failed: status=%d\n", name, (int)status);
    exit(EXIT_FAILURE);
}

/** @brief Arm local examples; the common selector decides whether MQTT is started. */
gw_status_enum_t mqtt_test_messages_config(void)
{
    s_stop_requested = 0;
    s_examples_pending = true;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    return mqtt_messages_is_supported() ? GW_OK : GW_UNSUPPORTED;
}

/** @brief Decode live JSON in combined mode without transmitting to STM32. */
void mqtt_test_messages_receive(const char *kit_code, const char *topic, const void *payload, size_t length)
{
    kit_command_struct_t command = {};
    gw_status_enum_t status = mqtt_messages_decode_command(kit_code, topic, payload, length, &command);
    if (status != GW_OK)
    {
        fprintf(stderr, "Command rejected by JSON codec: status=%d\n", (int)status);
        return;
    }
    print_command(&command);
}

/** @brief Produce clearly synthetic telemetry for local output or test-only PUB. */
gw_status_enum_t mqtt_test_messages_build_telemetry(const char *kit_code, char *output, size_t capacity, size_t *written)
{
    kit_telemetry_struct_t sample = {};
    sample.voltage_v = 6.07f;
    sample.current_ma = 125.5f;
    sample.power_on = 1;
    sample.magnetic_field_mt = 123.456789;
    sample.scale_code = KIT_SCALE_199_9;
    sample.ruler_position_mm = 150;

    int64_t received_at_ms = mqtt_messages_now_ms();
    gw_status_enum_t status = mqtt_messages_encode_telemetry(
        kit_code, received_at_ms, "demo-attempt", 0, &sample, output, capacity, written);
    return received_at_ms < 0 ? GW_IO_ERROR : status;
}

/** @brief Decode, verify and print one example passed as a single struct. */
static void run_command_example(const mqtt_message_example_struct_t *example)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "lab/kit/KIT01/cmd/%s", example->suffix);

    kit_command_struct_t command = {};
    gw_status_enum_t status = mqtt_messages_decode_command("KIT01", topic, example->payload, strlen(example->payload), &command);

    check_example(status, example->suffix);
    check_example(command.id == example->expected_id ? GW_OK : GW_INVALID_ARG, example->suffix);
    print_command(&command);
}

/** @brief Run examples once, then remain idle until Ctrl+C or a live callback. */
void mqtt_test_messages_run(void)
{
    if (s_stop_requested)
    {
        exit(EXIT_SUCCESS);
    }
    if (!s_examples_pending)
    {
        return;
    }
    s_examples_pending = false;

    for (const auto &example : s_examples)
    {
        run_command_example(&example);
    }

    char telemetry[512];
    size_t length = 0;
    gw_status_enum_t status = mqtt_test_messages_build_telemetry("KIT01", telemetry, sizeof(telemetry), &length);
    check_example(status, "telemetry");
    printf("LOCAL synthetic telemetry (%zu bytes): %s\n", length, telemetry);
    printf("Local message examples passed; Ctrl+C to stop.\n");
}
