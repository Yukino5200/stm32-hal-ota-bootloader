//
// Created by lenovo on 2026/10/12.
//

#ifndef OTA_BOOT_H
#define OTA_BOOT_H

#include "main.h"

typedef void (*load_a) (void);

void BootLoader_Brance(void);
void LOAD_A(uint32_t addr);
void BootLoader_Clear(void);
uint8_t BootLoader_Enter(uint8_t timeout);
void BootLoader_Info(void);
void BootLoader_Event(uint8_t *data, uint16_t datalen);
uint16_t Xmodem_CRC16(uint8_t *data, uint16_t datalen);

#endif //OTA_BOOT_H
