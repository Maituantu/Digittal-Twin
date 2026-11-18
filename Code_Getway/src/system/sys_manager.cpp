#include "system/sys_manager.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_messages.h"
#include "system/sys_config.h"
#include "common/command_queue.h"
#include "uart/uart_client.h"

#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/*** System shutdown ***/
static volatile sig_atomic_t s_stop_requested; // Stop requested by SIGINT/SIGTERM.
static uint16_t s_pending_telemetry_acks;
static char s_attempt_id[KIT_ATTEMPT_ID_CAPACITY];
static uint64_t s_telemetry_seq;
static uint64_t s_heartbeat_seq;
static uint64_t s_next_heartbeat_ms;

/*** Internal function prototypes ***/
static void handle_signal(int signal_number);
static gw_status_enum_t subscribe_mqtt_commands(void);
static gw_status_enum_t configure_mqtt(void);
static gw_status_enum_t configure_uart(void);
static void on_mqtt_message(const char *topic, const void *payload, size_t length, bool retained, void *user_data);
static gw_status_enum_t process_command(const kit_command_struct_t &command);
static void on_uart_frame(const uart_frame_struct_t *frame, void *context);

gw_status_enum_t sys_manager_config(void)
{
    gw_status_enum_t uart_status;
    gw_status_enum_t status;

    s_stop_requested = 0;
    s_pending_telemetry_acks = 0;
    s_attempt_id[0] = '\0';
    s_telemetry_seq = 0;
    s_heartbeat_seq = 0;
    s_next_heartbeat_ms = 0;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("Gateway starting\n");

    if (!command_queue_init(COMMAND_QUEUE_CAPACITY))
    {
        fprintf(stderr, "Command queue initialization failed\n");
        return GW_IO_ERROR;
    }

    printf("UART initialization: enabled=%d device=%s baud=%u\n",
           UART_ENABLED, UART_DEVICE, (unsigned int)UART_BAUD);
    uart_status = configure_uart();
    if (uart_status != GW_OK)
    {
        fprintf(stderr, "UART initialization failed: device=%s status=%d\n", UART_DEVICE, (int)uart_status);
        return uart_status;
    }

    printf("UART initialization complete\n");
    printf("MQTT initialization: host=%s port=%d\n", MQTT_HOST, MQTT_PORT);
    status = configure_mqtt();
    if (status != GW_OK)
    {
        uart_client_stop();
        fprintf(stderr, "System MQTT initialization failed: %d\n", (int)status);
    }
    else
    {
        printf("Gateway running; waiting for UART frames and MQTT commands\n");
    }

    return status;
}

void sys_manager_run(void)
{
    // Preserve a dequeued command while the previous frame is still being written.
    static kit_command_struct_t command = {};
    static bool flag_command_pending = false;
    
    gw_status_enum_t status;
#if UART_ENABLED
    gw_status_enum_t uart_status;
#endif
    if (s_attempt_id[0])
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t now_ms = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
        if (s_next_heartbeat_ms == 0)
        {
            s_next_heartbeat_ms = now_ms;
        }
        if (now_ms >= s_next_heartbeat_ms)
        {
            char heartbeat[256];
            size_t heartbeat_length = 0;
            status = mqtt_messages_encode_heartbeat(MQTT_KIT_CODE, s_attempt_id, s_heartbeat_seq++, heartbeat, sizeof(heartbeat), &heartbeat_length);
            if (status == GW_OK)
            {
                status = mqtt_client_publish(MQTT_TOPIC_HEARTBEAT, heartbeat, heartbeat_length, 1, false);
            }
            if (status != GW_OK)
            {
                fprintf(stderr, "MQTT heartbeat publish failed: %d\n", (int)status);
            }
            s_next_heartbeat_ms = now_ms + 1000;
        }
    }

    if (s_stop_requested)
    {
        uart_client_stop();
        exit(mqtt_client_stop() == GW_OK ? EXIT_SUCCESS : EXIT_FAILURE);
    }
#if UART_ENABLED
    uart_status = uart_client_poll();
    if (uart_status != GW_OK)
    {
        fprintf(stderr, "UART poll failed: %d; stopping gateway\n", (int)uart_status);
        uart_client_stop();
        mqtt_client_stop();
        exit(EXIT_FAILURE);
    }
    if (s_pending_telemetry_acks > 0)
    {
        status = uart_client_send_ack(KIT_MSG_TELEMETRY);
        if (status == GW_OK)
        {
            --s_pending_telemetry_acks;
            printf("UART telemetry ACK queued for TX\n");
        }
        else if (status != GW_BUSY)
        {
            fprintf(stderr, "UART telemetry ACK send failed: %d\n", (int)status);
        }
        usleep(10000);
        return;
    }
#endif
    if (!flag_command_pending)
    {
        flag_command_pending = command_queue_pop(&command);
    }

    if (flag_command_pending)
    {
        status = process_command(command);
        if (status == GW_BUSY)
        {
            usleep(10000);
            return;
        }

        if (status != GW_OK)
        {
            fprintf(stderr, "UART send rejected: %d\n", (int)status);
        }

        flag_command_pending = false;
    }

    usleep(10000);
}

static void handle_signal(int)
{
    s_stop_requested = 1;
}

static gw_status_enum_t configure_uart(void)
{
#if UART_ENABLED
    uart_config_struct_t config = {UART_DEVICE, UART_BAUD, UART_RX_TIMEOUT_MS, UART_TX_TIMEOUT_MS};
    return uart_client_init(&config, on_uart_frame, NULL);
#else
    return GW_OK;
#endif
}

static gw_status_enum_t subscribe_mqtt_commands(void)
{
    const char *const topics[] =
        {
            MQTT_TOPIC_CMD_RESET,
            MQTT_TOPIC_CMD_POWER,
            MQTT_TOPIC_CMD_VOLTAGE,
            MQTT_TOPIC_CMD_SCALE,
            MQTT_TOPIC_CMD_RULER,
            MQTT_TOPIC_CMD_SHUTDOWN};

    for (uint16_t i = 0; i < sizeof(topics) / sizeof(topics[0]); i++)
    {
        gw_status_enum_t status = mqtt_client_subscribe(topics[i], 1);
        if (status != GW_OK)
        {
            return status;
        }
    }
    return GW_OK;
}

static gw_status_enum_t configure_mqtt(void)
{
    mqtt_config_struct_t config = {};
    config.host = MQTT_HOST;
    config.port = MQTT_PORT;
    config.client_id = MQTT_CLIENT_ID;
    config.kit_code = MQTT_KIT_CODE;
    config.username = MQTT_USERNAME;
    config.password = MQTT_PASSWORD;

    gw_status_enum_t status = mqtt_client_init(&config, on_mqtt_message, NULL);
    if (status != GW_OK)
    {
        return status;
    }

    status = subscribe_mqtt_commands();
    if (status != GW_OK)
    {
        return status;
    }
    return mqtt_client_start();
}

static void on_mqtt_message(const char *topic, const void *payload, size_t length, bool retained, void *)
{
    if (retained)
    {
        return;
    }

    kit_command_struct_t command = {};
    gw_status_enum_t status = mqtt_messages_decode_command(MQTT_KIT_CODE, topic, payload, length, &command);
    if (status != GW_OK)
    {
        fprintf(stderr, "MQTT command rejected: %d\n", (int)status);
        return;
    }

    if (!command_queue_push(command))
    {
        fprintf(stderr, "MQTT command queue full: dropping id=0x%02X\n", (unsigned int)command.id);
        return;
    }

    printf("MQTT RX: %s | ", topic);
    if (length > 0)
    {
        fwrite(payload, 1, length, stdout);
    }
    putchar('\n');
}

static gw_status_enum_t process_command(const kit_command_struct_t &command)
{
#if UART_ENABLED
    gw_status_enum_t status = uart_client_send(&command);
    if (status != GW_OK)
    {
        return status;
    }
#endif
    printf("UART command %s: id=0x%02X\n", UART_ENABLED ? "queued for TX" : "disabled; dropped", (unsigned int)command.id);
    if (command.id == KIT_MSG_RESET_ACTION && command.data.action == KIT_RESET_START && command.attempt_id[0])
    {
        snprintf(s_attempt_id, sizeof(s_attempt_id), "%s", command.attempt_id);
        s_telemetry_seq = 0;
        s_heartbeat_seq = 0;
        s_next_heartbeat_ms = 0;
    }
    else if (command.id == KIT_MSG_RESET_ACTION && command.data.action == KIT_RESET_STOP)
    {
        s_attempt_id[0] = '\0';
        s_next_heartbeat_ms = 0;
    }
    return GW_OK;
}

static void on_uart_frame(const uart_frame_struct_t *frame, void *)
{
    kit_ack_struct_t ack = {};
    kit_telemetry_struct_t sample = {};

    if (frame->id == KIT_MSG_ACK_NACK)
    {
        if (uart_decode_ack(frame, &ack) == GW_OK)
        {
            printf("UART ACK: command=0x%02X result=%u error=%u\n", ack.command_id, ack.ack_code, ack.error_code);
        }
        else
        {
            fprintf(stderr, "UART invalid ACK\n");
        }
    }
    else if (frame->id == KIT_MSG_TELEMETRY)
    {
        if (uart_decode_telemetry(frame, &sample) == GW_OK)
        {
            printf("UART telemetry: voltage=%.3f current_ma=%.3f field_mt=%.6f power=%u scale=%u ruler=%u\n",
                   sample.voltage_v, sample.current_ma, sample.magnetic_field_mt,
                   sample.power_on, sample.scale_code, sample.ruler_position_mm);
            char payload[512];
            size_t payload_length = 0;
            gw_status_enum_t status = mqtt_messages_encode_telemetry(
                MQTT_KIT_CODE, mqtt_messages_now_ms(), s_attempt_id, s_telemetry_seq++, &sample,
                payload, sizeof(payload), &payload_length);
            if (status != GW_OK)
            {
                fprintf(stderr, "MQTT telemetry JSON encoding failed: %d\n", (int)status);
            }
            else
            {
                status = mqtt_client_publish(MQTT_TOPIC_TELEMETRY, payload, payload_length, 1, false);
                if (status == GW_OK)
                {
                    printf("MQTT telemetry queued for PUB: %s | %s\n", MQTT_TOPIC_TELEMETRY, payload);
                }
                else
                {
                    fprintf(stderr, "MQTT telemetry publish failed; sample dropped: %d\n", (int)status);
                }
            }
            if (s_pending_telemetry_acks < COMMAND_QUEUE_CAPACITY)
            {
                ++s_pending_telemetry_acks;
            }
            else
            {
                fprintf(stderr, "UART telemetry ACK queue full\n");
            }
        }
        else
        {
            fprintf(stderr, "UART invalid telemetry length/value: %u\n", frame->length);
        }
    }
}
