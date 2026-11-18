
#include "bts_frame_message.h"
#include <string.h>
uint16_t CheckSum(uint8_t *buf, uint8_t len);

int16_t CreateMessage(uint8_t type_msg, uint16_t length, uint8_t *datain, uint8_t *dataout)
{
  uint16_t frame_length;
  uint16_t crc;
	
	if (length > DEFAULT_MAX_SIZE_DATA)
  {
#if DEBUG_ERROR
    BTS_Debug_Error("Length > DEFAULT_MAX_SIZE_DATA");
#endif
    return RETRUN_ERROR;
  }
	
  dataout[0] = START_BYTE;
  dataout[1] = type_msg;
  dataout[2] = (uint8_t)length;
  if (length > 0)
  {
    memcpy(&dataout[3], datain, length);
  }

  frame_length = (uint16_t)(length + DEFAULT_BYTE);
  crc = CheckSum(&dataout[1], (uint8_t)(length + 2));
  dataout[frame_length] = (uint8_t)(crc & 0xff);
  dataout[frame_length + 1] = (uint8_t)(crc >> 8);

  return (int16_t)(frame_length + DEFAULT_BYTE_CHECKSUM);
}

int16_t DetectMessage(uint8_t *datain, messageFrameMsg_t *dataout)
{
  uint16_t frame_size;
  uint16_t crc_offset;
  uint16_t received_crc;

  if (datain[0] != START_BYTE || datain[2] > DEFAULT_MAX_SIZE_DATA)
  {
    return RETRUN_ERROR;
  }

  frame_size = (uint16_t)(datain[2] + DEFAULT_BYTE + DEFAULT_BYTE_CHECKSUM);
  crc_offset = (uint16_t)(datain[2] + DEFAULT_BYTE);
  received_crc = Bts_Convert_From_Bytes_To_Uint16(datain[crc_offset], datain[crc_offset + 1]);

  if (received_crc != CheckSum(&datain[1], (uint8_t)(datain[2] + 2)))
  {
    return RETRUN_ERROR;
  }

  dataout->Start = datain[0];
  dataout->TypeMessage = datain[1];
  dataout->Length = datain[2];
  if (datain[2] > 0)
  {
    memcpy(dataout->Data, &datain[3], datain[2]);
  }
  dataout->Crc = received_crc;

  return (int16_t)frame_size;
}

int16_t DebugMessage(uint8_t *datain)
{
	uint8_t i = 0;
  messageFrameMsg_t dataout;
  
  printf("\n-(Size    : %d)-\n", DetectMessage(datain, &dataout));
  printf("Start     : %x\n", dataout.Start);
  printf("TypeMSG   : %x\n", dataout.TypeMessage);
	printf("Length    : %d\n", dataout.Length);
	printf("Value     : ");
  for (i = 0; i < dataout.Length; i++)
	{
		printf("%d ", dataout.Data[i]);
	}
  printf("\nCheck sum : %x\n", dataout.Crc);
  return 0;
}

uint16_t CheckSum(uint8_t *buf, uint8_t len)
{
  uint16_t crc = 0xFFFF, pos = 0, i = 0;
  for (pos = 0; pos < len; pos++)
  {
    crc ^= (uint16_t)buf[pos]; 	// XOR byte into least sig. byte of crc
    for (i = 8; i != 0; i--)   	// Loop over each bit
    {
      if ((crc & 0x0001) != 0)	// If the LSB is set
      {
        crc >>= 1; 						 	// Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else 											// Else LSB is not set
      {
        crc >>= 1; 							// Just shift right
      }
    }
  }
  return crc;
}
