//
// Created by lenovo on 2026/10/10.
//

#ifndef OTA_M24C02_H
#define OTA_M24C02_H

#include "main.h"
#include <stdint.h>
#define M24C02_WADDR    0xA0    /* I2C写设备地址 */
#define M24C02_RADDR    0xA1    /* I2C读设备地址 */

uint8_t M24C02_WriteByte(uint8_t addr, uint8_t wdata);
uint8_t M24C02_WritePage(uint8_t addr, uint8_t *wdata);
uint8_t M24C02_ReadData(uint8_t addr, uint8_t *rdata, uint16_t datalen);

#endif //OTA_M24C02_H
