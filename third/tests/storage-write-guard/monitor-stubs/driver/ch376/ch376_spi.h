#pragma once
#include <stdbool.h>
#include <stdint.h>
#define USB_INT_SUCCESS 0x14
#define USB_INT_CONNECT 0x15
#define USB_INT_DISCONNECT 0x16
void CH376_PORT_INIT(void);
uint8_t CH376T_Get_IC_Ver(void);
bool CH376T_SetUsbMode(void);
bool CH376T_CMD_DISK_CONNECT(void);
bool CH376T_DISK_MOUNT(void);
uint8_t CH376T_DiskCheckRaw(void);
uint8_t CH376T_FILE_OPEN_STATUS(const char *);
bool CH376T_FILE_OPEN_NAME(const char *);
bool CH376T_FILE_CREATE_NAME(const char *);
bool CH376T_BYTE_WRITE_DATA(const uint8_t *,uint16_t);
bool CH376T_FILE_CLOSE(void);
uint32_t CH376T_GetFileSize(void);
bool CH376T_BYTE_LOCATE(uint32_t);
