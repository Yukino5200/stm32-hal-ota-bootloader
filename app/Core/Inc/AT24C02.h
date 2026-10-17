//
// Created by lenovo on 2026/10/10.
//

#ifndef OTA_AT24C02_H
#define OTA_AT24C02_H

#include "main.h"
#define AT24C02_WADDR    0xA0    /* I2C写设备地址 */
#define AT24C02_RADDR    0xA1    /* I2C读设备地址 */

uint8_t AT24C02_WriteByte(uint8_t addr, uint8_t wdata);
uint8_t AT24C02_WritePage(uint8_t addr, uint8_t *wdata);
uint8_t AT24C02_ReadData(uint8_t addr, uint8_t *rdata, uint16_t datalen);

#endif //OTA_AT24C02_H
