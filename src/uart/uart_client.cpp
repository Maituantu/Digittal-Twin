#include "uart/uart_client.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <poll.h>

/*** Linux UART transport ***/

static int s_fd = -1;
static uart_parser_struct_t s_parser = {};
static uint8_t s_tx[UART_MAX_FRAME];
static uint16_t s_tx_length, s_tx_offset;
static uint64_t s_rx_time, s_tx_time;
static uart_config_struct_t s_config;
static uart_frame_callback_t s_callback;
static void *s_context;

static uint64_t now_ms()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec)*1000+t.tv_nsec/1000000;
}

void uart_client_stop()
{
    if (s_fd >= 0)
    {
        close(s_fd);
    }
    s_fd = -1;
    s_parser = {};
    s_tx_length = s_tx_offset = 0;
}

gw_status_enum_t uart_client_init(const uart_config_struct_t *c, uart_frame_callback_t callback, void *context)
{
    if (s_fd >= 0)
    {
        return GW_BUSY;
    }
    if (!c || !c->device || !c->device[0] || !callback || !c->rx_timeout_ms || !c->tx_timeout_ms)
    {
        return GW_INVALID_ARG;
    }
    speed_t speed;
    switch (c->baud)
    {
        case 9600:
            speed = B9600;
            break;
        case 19200:
            speed = B19200;
            break;
        case 38400:
            speed = B38400;
            break;
        case 57600:
            speed = B57600;
            break;
        case 115200:
            speed = B115200;
            break;
        default:
            return GW_UNSUPPORTED;
    }
    s_fd = open(c->device, O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
    if (s_fd < 0)
    {
        return GW_IO_ERROR;
    }
    termios settings;
    if (tcgetattr(s_fd, &settings) != 0)
    {
        uart_client_stop();
        return GW_IO_ERROR;
    }
    cfmakeraw(&settings);
    settings.c_cflag &= ~(CSIZE|PARENB|CSTOPB|CRTSCTS);
    settings.c_cflag |= CS8|CLOCAL|CREAD;
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;
    if (cfsetispeed(&settings, speed) != 0 || cfsetospeed(&settings, speed) != 0 || tcsetattr(s_fd, TCSANOW, &settings) != 0 || tcflush(s_fd, TCIOFLUSH) != 0)
    {
        uart_client_stop();
        return GW_IO_ERROR;
    }
    s_config = *c;
    s_callback = callback;
    s_context = context;
    s_parser = {};
    return GW_OK;
}

gw_status_enum_t uart_client_send(const kit_command_struct_t *command)
{
    if (s_fd < 0)
    {
        return GW_NOT_READY;
    }
    if (s_tx_length)
    {
        return GW_BUSY;
    }
    gw_status_enum_t status = uart_encode_command(command, s_tx, sizeof(s_tx), &s_tx_length);
    if (status == GW_OK)
    {
        s_tx_offset = 0;
        s_tx_time = now_ms();
    }
    return status;
}

gw_status_enum_t uart_client_send_ack(uint8_t message_id)
{
    uint8_t payload[3] = {0, message_id, 0};
    int16_t frame_length;

    if (s_fd < 0)
    {
        return GW_NOT_READY;
    }
    if (s_tx_length)
    {
        return GW_BUSY;
    }

    frame_length = CreateMessage(KIT_MSG_ACK_NACK, sizeof(payload), payload, s_tx);
    if (frame_length < 0)
    {
        return GW_INVALID_ARG;
    }

    s_tx_length = (uint16_t)frame_length;
    s_tx_offset = 0;
    s_tx_time = now_ms();
    return GW_OK;
}

gw_status_enum_t uart_client_poll()
{
    if (s_fd < 0)
    {
        return GW_NOT_READY;
    }
    pollfd device  =
    {
        s_fd, POLLIN, 0
    };
    int ready = poll(&device, 1, 0);
    if ((ready < 0 && errno != EINTR) || (ready > 0 && (device.revents & (POLLERR | POLLHUP | POLLNVAL))))
    {
        uart_client_stop();
        return GW_IO_ERROR;
    }
    if (s_tx_length)
    {
        if (now_ms()-s_tx_time >= s_config.tx_timeout_ms)
        {
            uart_client_stop();
            return GW_TIMEOUT;
        }
        ssize_t n = write(s_fd, s_tx+s_tx_offset, s_tx_length-s_tx_offset);
        if (n > 0)
        {
            s_tx_offset += uint16_t(n);
            if (s_tx_offset == s_tx_length)
            {
                s_tx_length = s_tx_offset = 0;
            }
        }
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
            uart_client_stop();
            return GW_IO_ERROR;
        }
    }
    if (s_parser.count && now_ms()-s_rx_time >= s_config.rx_timeout_ms)
    {
        s_parser = {};
    }
    uint8_t bytes[256];
    // Bounded work per iteration so MQTT commands and shutdown remain responsive.
    for (uint8_t batch = 0;batch < 4;batch++)
    {
        ssize_t n = read(s_fd, bytes, sizeof(bytes));
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            uart_client_stop();
            return GW_IO_ERROR;
        }
        if (n == 0)
        {
            break;
        }
        s_rx_time = now_ms();
        for (ssize_t i = 0;i < n;i++)
        {
            uart_frame_struct_t frame = {};
            if (uart_parser_feed(&s_parser, bytes[i], &frame))
            {
                s_callback(&frame, s_context);
            }
        }
    }
    return GW_OK;
}

