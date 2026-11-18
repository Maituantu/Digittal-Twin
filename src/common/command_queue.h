#ifndef COMMAND_QUEUE_H
#define COMMAND_QUEUE_H

#include "msg/kit_messages.h"

/** Initialize an empty FIFO with capacity command slots (1..65535).
 * Call once before starting MQTT. Returns false for zero capacity,
 * allocation failure, or an already initialized queue; existing data is preserved.
 */
bool command_queue_init(uint16_t capacity);

/** Thread-safe FIFO push after successful initialization.
 * Copies command data. Returns false when full, without overwriting old commands.
 */
bool command_queue_push(const kit_command_struct_t &command);

/** Copy and remove the oldest command.
 * Returns false when empty or command is nullptr; output is unchanged on failure.
 */
bool command_queue_pop(kit_command_struct_t *command);

#endif
