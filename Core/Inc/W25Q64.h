//
// Created by lenovo on 2026/10/11.
//

#ifndef OTA_W25Q64_H
#define OTA_W25Q64_H

#define CS_ENABLE HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET)
#define CS_DISABLE HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET)

void W25Q64_WaitBusy(void);
void W25Q64_WriteEnable(void);
void W25Q64_Erase64K(uint8_t blockNum);
void W25Q64_PageWrite(uint8_t *wbuff, uint16_t PageNum);
void W25Q64_Read(uint8_t *rbuff, uint32_t addr, uint32_t datalen);

#endif //OTA_W25Q64_H
