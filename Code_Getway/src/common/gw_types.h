#ifndef GW_TYPES_H
#define GW_TYPES_H

typedef enum
{
    GW_OK = 0,
    GW_INVALID_ARG,
    GW_NOT_READY,
    GW_BUSY,
    GW_NO_DATA,
    GW_TIMEOUT,
    GW_IO_ERROR,
    GW_UNSUPPORTED,
    GW_BUFFER_TOO_SMALL
} gw_status_enum_t;

#endif
