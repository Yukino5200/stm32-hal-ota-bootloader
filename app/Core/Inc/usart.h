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
#define U1_RX_SIZE 2048 // 接收缓冲区大小
#define U1_TX_SIZE 2048 // 发送缓冲区大小
#define U1_RX_MAX  256  // 接收缓冲区数组单块区域最大容量（用于DMA预设长度）
#define NUM        10   // 接收缓冲区数组长度
/* USER CODE END Private defines */

void MX_USART1_UART_Init(void);

/* USER CODE BEGIN Prototypes */
typedef struct{
  uint8_t *start;         // 接收缓冲区起始地址（初始化后固定，标记内存块首边界）
  uint8_t *in;            // 写入位置指针（DMA/中断往这里写数据，每写1字节向后移动）
  uint8_t *out;           // 读取位置指针（应用层从这里读数据，每读1字节向后移动）
  uint8_t *end;           // 接收缓冲区结束地址（初始化后固定，标记内存块尾边界）
}UCB_URxBuffptr;          // 单块接收缓冲区的控制块

typedef struct{
  uint16_t URxCounter;            // 待处理的完整数据包计数
  UCB_URxBuffptr URxDataPtr[NUM]; // 接收缓冲区结构体数组，一共 NUM 块独立的接收缓存
  UCB_URxBuffptr *URxDataIN;      // 入队指针：指向当前正在写入的缓冲区（DMA正在写这块）
  UCB_URxBuffptr *URxDataOUT;     // 出队指针：指向当前待读取的缓冲区（应用层读这块）
  UCB_URxBuffptr *URxDataEND;     // 队列尾标记：指向缓冲区数组的末尾，用来判断回绕
}UCB_CB;                          // 多缓冲区环形队列的总控制块

extern UCB_CB U1CB;
extern uint8_t U1_RxBuff[U1_RX_SIZE];
void u1_printf(char *format, ...);
void U1Rx_PtrInit(void);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */

