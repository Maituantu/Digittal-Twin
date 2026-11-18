#include "common/command_queue.h"

#include <stdint.h>
#include <pthread.h>
#include <memory>
#include <new>

/*** MQTT command queue ***/
static uint16_t s_command_queue_capacity; // Maximum number of queued commands.
static std::unique_ptr<kit_command_struct_t[]> s_command_queue; // Command storage allocated at initialization.
static uint16_t s_command_queue_head; // Index of the next command to process.
static uint16_t s_command_queue_count; // Number of commands waiting to be processed.
static pthread_mutex_t s_command_queue_mutex = PTHREAD_MUTEX_INITIALIZER; // Protect queue access across threads.

bool command_queue_init(uint16_t capacity)
{
    if (capacity == 0)
    {
        return false;
    }
    pthread_mutex_lock(&s_command_queue_mutex);
    if (s_command_queue)
    {
        pthread_mutex_unlock(&s_command_queue_mutex);
        return false;
    }
    s_command_queue.reset(new (std::nothrow) kit_command_struct_t[capacity]);
    bool initialized = s_command_queue != nullptr;
    if (initialized)
    {
        s_command_queue_capacity = capacity;
        s_command_queue_head = 0;
        s_command_queue_count = 0;
    }
    pthread_mutex_unlock(&s_command_queue_mutex);
    return initialized;
}

bool command_queue_push(const kit_command_struct_t &command)
{
    pthread_mutex_lock(&s_command_queue_mutex);
    if (s_command_queue_count == s_command_queue_capacity)
    {
        pthread_mutex_unlock(&s_command_queue_mutex);
        return false;
    }
    uint16_t tail = (s_command_queue_head + s_command_queue_count) % s_command_queue_capacity;
    s_command_queue[tail] = command;
    ++s_command_queue_count;
    pthread_mutex_unlock(&s_command_queue_mutex);
    return true;
}

bool command_queue_pop(kit_command_struct_t *command)
{
    if (command == nullptr)
    {
        return false;
    }
    pthread_mutex_lock(&s_command_queue_mutex);
    if (s_command_queue_count == 0)
    {
        pthread_mutex_unlock(&s_command_queue_mutex);
        return false;
    }
    *command = s_command_queue[s_command_queue_head];
    s_command_queue_head = (s_command_queue_head + 1) % s_command_queue_capacity;
    --s_command_queue_count;
    pthread_mutex_unlock(&s_command_queue_mutex);
    return true;
}

