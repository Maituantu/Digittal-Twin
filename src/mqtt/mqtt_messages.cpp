#include "mqtt/mqtt_messages.h"

#include <jansson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MQTT_MESSAGES_MAX_JSON_BYTES 4096
#define MQTT_MESSAGES_MAX_KIT_BYTES 127

static const char *const s_scale_names[] = {"1.999", "19.99", "199.9"};
static const char *const s_command_suffixes[] = {"reset", "power", "voltage", "scale", "ruler", "shutdown"};

static bool valid_attempt_id(const char *attempt_id)
{
    if (!attempt_id || !attempt_id[0] || strlen(attempt_id) >= KIT_ATTEMPT_ID_CAPACITY)
    {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)attempt_id; *cursor; ++cursor)
    {
        if (*cursor < 0x20 || *cursor == 0x7f)
        {
            return false;
        }
    }
    return true;
}

/** @brief Accept a bounded, nonempty single topic level as the configured kit. */
static bool valid_kit_code(const char *kit_code)
{
    if (!kit_code || !kit_code[0] || strlen(kit_code) > MQTT_MESSAGES_MAX_KIT_BYTES)
    {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)kit_code; *cursor; ++cursor)
    {
        if (*cursor < 0x20 || *cursor == 0x7f || *cursor == '/' || *cursor == '+' || *cursor == '#')
        {
            return false;
        }
    }
    return true;
}

/** @brief Match an exact command topic for the configured kit, not a suffix alone. */
static gw_status_enum_t command_id_from_topic(const char *kit_code, const char *topic, uint8_t *id)
{
    char prefix[160];
    int prefix_length = snprintf(prefix, sizeof(prefix), "lab/kit/%s/cmd/", kit_code);
    if (prefix_length < 0 || (size_t)prefix_length >= sizeof(prefix) || strncmp(topic, prefix, (size_t)prefix_length) != 0)
    {
        return GW_INVALID_ARG;
    }
    for (size_t index = 0; index < sizeof(s_command_suffixes) / sizeof(s_command_suffixes[0]); ++index)
    {
        if (strcmp(topic + prefix_length, s_command_suffixes[index]) == 0)
        {
            *id = (uint8_t)(KIT_MSG_RESET_ACTION + index);
            return GW_OK;
        }
    }
    return GW_UNSUPPORTED;
}

/** @brief Compare strings by decoded length, not by a possibly embedded NUL. */
static bool string_equals(const json_t *value, const char *expected)
{
    return json_is_string(value) && json_string_length(value) == strlen(expected) && memcmp(json_string_value(value), expected, strlen(expected)) == 0;
}

/** @brief Require a finite integral number; JSON booleans/strings are not numbers. */
static bool read_unsigned(const json_t *object, const char *key, uint16_t maximum, uint16_t *output)
{
    const json_t *value = json_object_get(object, key);
    if (!json_is_number(value))
    {
        return false;
    }
    double number = json_number_value(value);
    if (!isfinite(number) || number < 0 || number > maximum || floor(number) != number)
    {
        return false;
    }
    *output = (uint16_t)number;
    return true;
}

static bool read_command_metadata(const json_t *object, kit_command_struct_t *command)
{
    const json_t *attempt = json_object_get(object, "attempt_id");
    const json_t *sequence = json_object_get(object, "command_seq");

    if (attempt)
    {
        if (!json_is_string(attempt) ||
            !valid_attempt_id(json_string_value(attempt)))
        {
            return false;
        }
        snprintf(command->attempt_id, sizeof(command->attempt_id), "%s",
                 json_string_value(attempt));
        command->has_attempt_id = true;
    }

    if (sequence)
    {
        if (!json_is_integer(sequence) || json_integer_value(sequence) < 0 ||
            (uint64_t)json_integer_value(sequence) > UINT64_C(9007199254740991))
        {
            return false;
        }
        command->command_seq = (uint64_t)json_integer_value(sequence);
        command->has_command_seq = true;
    }
    return true;
}

/** @brief Decode fields after the JSON parser and topic checks have succeeded. */
static bool decode_fields(const json_t *object, kit_command_struct_t *command)
{
    const json_t *action = json_object_get(object, "action");
    uint16_t number = 0;
    switch (command->id)
    {
        case KIT_MSG_RESET_ACTION:
            if (string_equals(action, "start"))
            {
                command->data.action = KIT_RESET_START;
            }
            else if (string_equals(action, "stop"))
            {
                command->data.action = KIT_RESET_STOP;
            }
            else if (string_equals(action, "calib"))
            {
                command->data.action = KIT_RESET_CALIB;
            }
            else
            {
                return false;
            }
            return true;
        case KIT_MSG_SET_POWER:
            if (!string_equals(action, "on") && !string_equals(action, "off"))
            {
                return false;
            }
            command->data.power_on = string_equals(action, "on") ? 1 : 0;
            return true;
        case KIT_MSG_SET_VOLTAGE:
            if (!read_unsigned(object, "voltage", 12, &number) || number % 3 != 0)
            {
                return false;
            }
            command->data.voltage_v = (uint8_t)number;
            return true;
        case KIT_MSG_SET_SCALE:
            for (uint8_t index = 0; index < 3; ++index)
            {
                if (string_equals(json_object_get(object, "scale"), s_scale_names[index]))
                {
                    command->data.scale_code = index;
                    return true;
                }
            }
            return false;
        case KIT_MSG_SET_RULER:
            if (!read_unsigned(object, "ruler_position", 300, &number) &&
                !read_unsigned(object, "position", 300, &number))
            {
                return false;
            }
            command->data.ruler_position_mm = number;
            return true;
        case KIT_MSG_SHUTDOWN_KIT:
            if (!string_equals(action, "shutdown"))
            {
                return false;
            }
            command->data.action = 1;
            return true;
        default:
            return false;
    }
}

/** @brief Return whether the compiled JSON backend is available. */
bool mqtt_messages_is_supported(void)
{
    return true;
}

int64_t mqtt_messages_now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
    {
        return -1;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/** @brief Decode a Web command according to the supplied v1.0 examples. */
gw_status_enum_t mqtt_messages_decode_command(const char *kit_code, const char *topic, const void *payload, size_t length, kit_command_struct_t *output)
{
    if (!valid_kit_code(kit_code) || !topic || !payload || !output || length == 0 || length > MQTT_MESSAGES_MAX_JSON_BYTES || memchr(payload, '\0', length))
    {
        return GW_INVALID_ARG;
    }
    kit_command_struct_t command = {};
    gw_status_enum_t status = command_id_from_topic(kit_code, topic, &command.id);
    if (status != GW_OK)
    {
        return status;
    }
    json_error_t error;
    json_t *object = json_loadb((const char *)payload, length, JSON_REJECT_DUPLICATES, &error);
    if (!object)
    {
        return json_error_code(&error) == json_error_out_of_memory ? GW_IO_ERROR : GW_INVALID_ARG;
    }
    const json_t *payload_kit = json_object_get(object, "kit_code");
    bool valid = json_is_object(object) && (!payload_kit || string_equals(payload_kit, kit_code)) &&
                 read_command_metadata(object, &command) &&
                 decode_fields(object, &command);
    json_decref(object);
    if (!valid)
    {
        return GW_INVALID_ARG;
    }
    *output = command;
    return GW_OK;
}

/** @brief Format caller-supplied Unix milliseconds as UTC, including milliseconds. */
/** @brief Reject invalid measurements rather than clamping or substituting zeros. */
static bool valid_telemetry(const kit_telemetry_struct_t *sample)
{
    return sample && isfinite(sample->voltage_v) && isfinite(sample->current_ma) && sample->power_on <= 1 && isfinite(sample->magnetic_field_mt) && sample->magnetic_field_mt >= 0 && sample->magnetic_field_mt <= 200 && sample->scale_code <= KIT_SCALE_199_9 && sample->ruler_position_mm <= 300;
}

/** @brief Serialize actual measurements without coupling JSON to UART or MQTT I/O. */
gw_status_enum_t mqtt_messages_encode_telemetry(const char *kit_code, int64_t, const char *attempt_id, uint64_t sequence, const kit_telemetry_struct_t *sample, char *output, size_t capacity, size_t *written)
{
    if (written)
    {
        *written = 0;
    }
    if (output && capacity > 0)
    {
        output[0] = '\0';
    }
    if (!output || !written || !valid_kit_code(kit_code) ||
        (attempt_id && attempt_id[0] && !valid_attempt_id(attempt_id)) ||
        sequence > UINT64_C(9007199254740991) || !valid_telemetry(sample))
    {
        return GW_INVALID_ARG;
    }
    json_t *object = json_object();
    if (!object)
    {
        return GW_IO_ERROR;
    }
    int result = 0;
    result |= json_object_set_new(object, "kit_code", json_string(kit_code));
    result |= json_object_set_new(object, "seq", json_integer((json_int_t)sequence));
    if (attempt_id && attempt_id[0])
    {
        result |= json_object_set_new(object, "attempt_id", json_string(attempt_id));
    }
    result |= json_object_set_new(object, "voltage", json_real((double)sample->voltage_v));
    result |= json_object_set_new(object, "current_ma", json_real((double)sample->current_ma));
    result |= json_object_set_new(object, "power_on", json_boolean(sample->power_on));
    result |= json_object_set_new(object, "magnetic_field", json_real(sample->magnetic_field_mt));
    result |= json_object_set_new(object, "scale", json_string(s_scale_names[sample->scale_code]));
    result |= json_object_set_new(object, "ruler_position", json_integer(sample->ruler_position_mm));
    if (result != 0)
    {
        json_decref(object);
        return GW_IO_ERROR;
    }
    const size_t flags = JSON_COMPACT | JSON_REAL_PRECISION(17);
    size_t needed = json_dumpb(object, NULL, 0, flags);
    if (needed == 0)
    {
        json_decref(object);
        return GW_IO_ERROR;
    }
    if (needed >= capacity)
    {
        json_decref(object);
        return GW_BUFFER_TOO_SMALL;
    }
    size_t actual = json_dumpb(object, output, capacity - 1, flags);
    json_decref(object);
    if (actual != needed)
    {
        output[0] = '\0';
        return GW_IO_ERROR;
    }
    output[actual] = '\0';
    *written = actual;
    return GW_OK;
}

gw_status_enum_t mqtt_messages_encode_heartbeat(const char *kit_code, const char *attempt_id, uint64_t sequence, char *output, size_t capacity, size_t *written)
{
    if (written)
    {
        *written = 0;
    }
    if (output && capacity > 0)
    {
        output[0] = '\0';
    }
    if (!output || !written || !valid_kit_code(kit_code) || !valid_attempt_id(attempt_id) ||
        sequence > UINT64_C(9007199254740991))
    {
        return GW_INVALID_ARG;
    }
    json_t *object = json_object();
    if (!object)
    {
        return GW_IO_ERROR;
    }
    int result = 0;
    result |= json_object_set_new(object, "kit_code", json_string(kit_code));
    result |= json_object_set_new(object, "attempt_id", json_string(attempt_id));
    result |= json_object_set_new(object, "seq", json_integer((json_int_t)sequence));
    size_t needed = result == 0 ? json_dumpb(object, NULL, 0, JSON_COMPACT) : 0;
    if (needed == 0 || needed >= capacity)
    {
        json_decref(object);
        return needed == 0 ? GW_IO_ERROR : GW_BUFFER_TOO_SMALL;
    }
    size_t actual = json_dumpb(object, output, capacity - 1, JSON_COMPACT);
    json_decref(object);
    if (actual != needed)
    {
        output[0] = '\0';
        return GW_IO_ERROR;
    }
    output[actual] = '\0';
    *written = actual;
    return GW_OK;
}
