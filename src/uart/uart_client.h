#ifndef UART_CLIENT_H
#define UART_CLIENT_H

#include "uart/uart_messages.h"

struct uart_config_struct_t
{
    const char *device;
    uint32_t baud;
    uint32_t rx_timeout_ms;
    uint32_t tx_timeout_ms;
};
typedef void (*uart_frame_callback_t)(const uart_frame_struct_t *frame, void *context);
// Single owner: call all functions from the main thread. Linux raw 8N1, no flow control.

gw_status_enum_t uart_client_init(const uart_config_struct_t *config, uart_frame_callback_t callback, void *context);
// Success means queued locally, not acknowledged by the MCU. Busy preserves the current TX.

gw_status_enum_t uart_client_send(const kit_command_struct_t *command);

// Queue a successful ACK for a received message. Busy leaves the current TX intact.
gw_status_enum_t uart_client_send_ack(uint8_t message_id);

gw_status_enum_t uart_client_poll(void);

void uart_client_stop(void);

#endif
