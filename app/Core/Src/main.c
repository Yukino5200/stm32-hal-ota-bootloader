/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "delay_us.h"
#include "AT24C02.h"
#include "MyI2C.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
uint8_t rbuff[256];
uint8_t wbuff[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start(&htim2);
  uint16_t i;
  u1_printf("Hello World\r\n");

  // for (i = 0; i < 256; i ++)
  // {
  //   AT24C02_WriteByte(i, i);
  //   delay_us(5);
  // }

  // ===== 原本的程序：整片EEPROM写测试 + 全片打印（做页大小测试期间先注释掉，测完取消注释即可恢复）=====
  // 地址0~15是Bootloader的OTA标志区，A区App不能写它，
  // 测试数据从地址16开始写
  // for (i = 1; i < 16; i ++)
  // {
  //   AT24C02_WritePage(i*16, wbuff);
  //   delay_us(5);
  // }
  // AT24C02_ReadData(0, rbuff, 256);
  //
  // for (i = 0; i < 256; i ++)
  // {
  //   u1_printf("地址%d = %x\r\n", i, rbuff[i]);
  // }
  // ===== 原本的程序结束 =====

  // ===== 新加：EEPROM 页大小测试（AT24C02=8字节/页，M24C02=16字节/页）=====
  // ① 地址16~47 逐字节写 0xFF（做干净底；逐字节写不受页大小影响）
  // ② 从地址16（8和16都页对齐）用一次页写连发16个互不相同的字节 0x10~0x1F
  // ③ 读回，只打印地址16~47
  // 判读：
  //   地址16~23 = 18..1F 且 地址24~31 = FF  → 页=8，超出部分【回绕覆盖】本页开头（AT24C02）
  //   地址16~31 = 10..1F                    → 页=16（M24C02）
  //   地址16~23 = 10..17 且 地址24~31 = FF  → 页=8，超出部分被【丢弃】
  // ★ 全程只碰地址16以后，不动地址0~15（OTA标志区）
  for (i = 0; i < 32; i ++)              // ① 干净底：地址16~47 = 0xFF
  {
    AT24C02_WriteByte(16 + i, 0xFF);     // WriteByte 内部自带 5ms 等待
  }

  uint8_t test_w[16];
  for (i = 0; i < 16; i ++)              // ② 待测数据：10 11 ... 1F
  {
    test_w[i] = 0x10 + i;
  }
  AT24C02_WritePage(16, test_w);         // 一次连续写16字节

  AT24C02_ReadData(0, rbuff, 256);       // ③ 整片读回

  for (i = 16; i < 48; i ++)             // 只打印 16~47，32行看完
  {
    u1_printf("地址%d = %x\r\n", i, rbuff[i]);
  }
  // ===== 新加代码结束 =====
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    if(U1CB.URxDataOUT != U1CB.URxDataIN)
    {
      u1_printf("本次接收了%d字节数据\r\n",U1CB.URxDataOUT->end - U1CB.URxDataOUT->start + 1);
      for(i=0;i<U1CB.URxDataOUT->end - U1CB.URxDataOUT->start + 1;i++) u1_printf("%c",U1CB.URxDataOUT->start[i]);
      u1_printf("\r\n\r\n");

      U1CB.URxDataOUT++;
      if(U1CB.URxDataOUT == U1CB.URxDataEND)
      {
        U1CB.URxDataOUT = &U1CB.URxDataPtr[0];
      }
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
