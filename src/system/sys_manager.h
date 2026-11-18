#ifndef SYS_MANAGER_H
#define SYS_MANAGER_H

#include "common/gw_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Initialize UART and MQTT, register command topics and start the MQTT worker.
     * GW_OK means the worker started; broker connection proceeds asynchronously.
     */
    gw_status_enum_t sys_manager_config(void);

    /** @brief Poll UART RX/TX, forward queued MQTT commands and handle shutdown. */
    void sys_manager_run(void);

#ifdef __cplusplus
}
#endif
#endif
