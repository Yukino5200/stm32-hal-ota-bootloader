/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.h
  * @brief   This file contains all the function prototypes for
  *          the usart.c file
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
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
#include "stdint.h"
#include "stdarg.h"
#include "stdio.h"
#include "string.h"
/* USER CODE END Includes */

extern UART_HandleTypeDef huart1;

/* USER CODE BEGIN Private defines */
#define U1_RX_SIZE 2048   // 整块大仓库总大小：2048字节，U1_RxBuff[2048]
#define U1_RX_MAX  256    // 单个隔间最多放256字节（DMA单次最大接收上限）
#define NUM        10     // 一共划分 10 个隔间（10张卡片）
#define U1_TX_SIZE 2048   // 发送缓冲区大小
/* USER CODE END Private defines */

void MX_USART1_UART_Init(void);

/* USER CODE BEGIN Prototypes */
// 【卡片：单个隔间的管控信息】
typedef struct{
  uint8_t *start;  // 隔间起始地址，固定不变，这个隔间从内存哪里开始
  uint8_t *in;     // 写入游标（DMA传送带往这里放货，写一个字节往后挪一次） 注：该串口收包流程没用到该参数
  uint8_t *out;    // 读出游标（main分拣员从这里取货，读一个字节往后挪一次）注：该串口收包流程没用到该参数
  uint8_t *end;    // 隔间结束地址，固定不变，不能写到这个地址外面
}UCB_URxBuffptr;

// 【驿站总管台账，全局总控制块】
typedef struct{
  uint16_t URxCounter;                  // 全局计数，记录累计收到多少包
  UCB_URxBuffptr URxDataPtr[NUM];       // 10张卡片，10个隔间的信息全部存在这里
  UCB_URxBuffptr *URxDataIN;            // 入队指针：DMA【收货员正在往哪一个隔间里收货】
  UCB_URxBuffptr *URxDataOUT;           // 出队指针：main循环【分拣员正在处理哪一个隔间】
  UCB_URxBuffptr *URxDataEND;           // 队列边界标记，判断10个隔间是不是走到末尾，用来环形绕回
}UCB_CB;                                // 多缓冲区环形队列的总控制块

extern UCB_CB U1CB;
extern uint8_t U1_RxBuff[U1_RX_SIZE];
void u1_printf(char *format, ...);
void U1Rx_PtrInit(void);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */

