#include "uart/uart_client.h"
#include <stdio.h>
#include <unistd.h>
static bool received;
static void on_frame(const uart_frame_struct_t *frame, void *)
{
    kit_ack_struct_t ack={};
    if(uart_decode_ack(frame,&ack)==GW_OK && ack.command_id==KIT_MSG_SET_VOLTAGE && ack.ack_code==0) received=true;
}
int main(int argc,char **argv)
{
    if(argc!=2) return 1;
    uart_config_struct_t config={argv[1],115200,200,1000};
    if(uart_client_init(&config,on_frame,nullptr)!=GW_OK) return 2;
    kit_command_struct_t command={}; command.id=KIT_MSG_SET_VOLTAGE; command.data.voltage_v=9;
    if(uart_client_send(&command)!=GW_OK || uart_client_send(&command)!=GW_BUSY) return 3;
    for(int i=0;i<400 && !received;i++) { if(uart_client_poll()!=GW_OK) return 4; usleep(5000); }
    uart_client_stop();
    if(!received) return 5;
    puts("PASS: UART PTY command TX and fragmented ACK RX");
}
