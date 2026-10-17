/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
#define STM32_FLASH_SADDR      0x08000000                                                       // FLASH起始地址
#define STM32_PAGE_SIZE        1024                                                             // FLASH页区大小
#define STM32_PAGE_NUM         64                                                               // FLASH总页区个数
#define STM32_B_PAGE_NUM       20                                                               // (B区)引导程序页数
#define STM32_A_PAGE_NUM       (STM32_PAGE_NUM - STM32_B_PAGE_NUM)                              // (A区)应用程序页数
#define STM32_A_START_PAGE     STM32_B_PAGE_NUM                                                 // A区起始扇区编号
#define STM32_A_SADDR          (STM32_FLASH_SADDR + STM32_PAGE_SIZE * STM32_A_START_PAGE)       // A区起始地址

#define STM32_RAM_SADDR        0x20000000                                                       // RAM起始地址
#define STM32_RAM_EADDR        (STM32_RAM_SADDR + 20 * 1024)                                    // RAM结束地址(20K)=0x20005000，也是App的栈顶

#define OTA_SET_FLAG           0xAABB1122                                                       // OTA标志
#define OTA_INFOCB_SIZE        sizeof(OTA_InfoCB)                                               // OTA信息结构体大小

/* Bootloader界面各个事件标志位
 * 下面数据任意与BootStaFlag进行按位与操作后，该标志位置1；
 * 例：BootStaFlag |= UPDATA_A_FLAG，BootStaFlag的二进制形式第1位为1，与SET_VERSION_FLAG就是其二进制形式第四位为1
 * 进行按位异或操作即是清零该标志位：BootStaFlag &= ~UPDATA_A_FLAG后，BootStaFlag的二进制形式第1位为0
 */
#define UPDATA_A_FLAG          0x00000001                                                       // 标志位 [1]擦除A区
#define IAP_XMODEM_C_FLAG      0x00000002                                                       // Xmodem传输C标志位
#define IAP_XMODEM_DATA_FALG   0x00000004                                                       // Xmodem传输数据标志位
#define SET_VERSION_FLAG       0x00000008                                                       // 设置版本号标志位
#define UPDATA_TO_W25Q64_FLAG  0x00000010                                                       // 程序下载至外部FLASH（即W25Q64）标志位
#define UPDATA_XMODEM_FLAG     0x00000020                                                       // Xmodem向W25Q64传输标志位
#define USE_W25Q64_FLAG        0x00000040                                                       // 使用W25Q64中的数据标志位

typedef struct{
  uint32_t OTA_flag;        // OTA标志位
  uint32_t Firelen[11];     // Firelen[n] = 第 n 块 W25Q64 里的固件长度（字节）
                            // 共 12×4 = 48 字节 = EEPROM 的 3 页（16B/页）
  uint8_t OTA_version[32];  // OTA固件版本号
}OTA_InfoCB;                // OTA相关的信息结构体，需要保存到24C02中

typedef struct{
  uint8_t UpDatabuff[STM32_PAGE_SIZE];  // 更新A区时，用于保存从W25Q64中读取的数据缓冲区
  uint32_t W25Q64_BlockNum;             // 记录从哪个W25Q64的块中读取数据,每一块64k，偏移量就是64k
  uint32_t XmodemTimer;                 // 记录Xmodem延时
  uint32_t XmodemNum;                   // Xmodem传输数据数量
  uint32_t XmodemCRC;                   // Xmodem的CRC校验位
}UpDataA_CB;                            // 更新A区用的结构体

extern OTA_InfoCB OTA_Info;
extern UpDataA_CB UpDataA;
extern uint32_t BootStaFlag;

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define CS_Pin GPIO_PIN_4
#define CS_GPIO_Port GPIOA
#define SDA_Pin GPIO_PIN_6
#define SDA_GPIO_Port GPIOB
#define SCL_Pin GPIO_PIN_7
#define SCL_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
