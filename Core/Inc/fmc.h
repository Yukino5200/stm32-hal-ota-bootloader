//
// Created by lenovo on 2026/10/11.
//

#ifndef OTA_FMC_H
#define OTA_FMC_H

#include "main.h"

uint32_t STM32_Flash_EraseSinglePage(uint32_t page_addr);
void STM32_EraseFlash(uint16_t start, uint16_t num);
void STM32_WriteFlash(uint32_t saddr, uint32_t *wdata, uint32_t wnum);

#endif //OTA_FMC_H
