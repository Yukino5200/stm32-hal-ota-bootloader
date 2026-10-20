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
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "delay_us.h"
#include "M24C02.h"
#include "boot.h"
#include "W25Q64.h"
#include "fmc.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

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
OTA_InfoCB OTA_Info;  // OTA信息结构体
UpDataA_CB UpDataA;
uint32_t BootStaFlag; // 状态容器（真正保存数据，缓存其他标志位的值来置不同的标志位）
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
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start(&htim2);

  uint8_t i;
  M24C02_ReadOTAInfo();  // 读取OTA信息
  BootLoader_Brance();    // 根据OTA_FLAG判断是否更新OTA


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* --------------------------------------------------------------------------
     *                          环形缓冲区处理串口接收数据进行消费（分拣员处理货物）
     --------------------------------------------------------------------------*/
    if(U1CB.URxDataOUT != U1CB.URxDataIN)
    {
      BootLoader_Event(U1CB.URxDataOUT->start,U1CB.URxDataOUT->end - U1CB.URxDataOUT->start + 1); // 分拣员从分拣的起始位置，开始处理一个隔间的货物
      U1CB.URxDataOUT++;  // 这一整个包的数据发完了，分拣员进入到下一个隔间，等待收货员发货
      if(U1CB.URxDataOUT == U1CB.URxDataEND)    // 如果分拣员走到了最后一个隔间
      {
        U1CB.URxDataOUT = &U1CB.URxDataPtr[0];  // 分拣员回到第一个隔间
      }
    }

    /*--------------------------------------------------------------------------
     * [2]串口IAP下载A区程序
     * Xmodem传输握手：定时发送字符'C'，通知上位机准备发送固件
     * 注意：掩码宏里面，1 在哪一位，if 就检测哪一位。所以下面if仅判断IAP_XMODEM_C_FLAG为1位的那个bit，两个标志位是否都为1，也就是检测BootStaFlag的那位是否为1
     --------------------------------------------------------------------------*/
    if (BootStaFlag & IAP_XMODEM_C_FLAG)
    {
      if (UpDataA.XmodemTimer >= 100)
      {
        u1_printf("C");
        UpDataA.XmodemTimer = 0;
      }
      HAL_Delay(10);
      UpDataA.XmodemTimer ++; // 10ms * 100 = 1s
    }

    /*--------------------------------------------------------------------------
     * [1]擦除A区
     * A区固件升级任务：UPDATA_A_FLAG置位时，执行外部Flash(W25Q64)拷贝到片内Flash A区
     --------------------------------------------------------------------------*/
    if (BootStaFlag & UPDATA_A_FLAG)
    { // 更新A区
      u1_printf("长度%d字节\r\n", OTA_Info.Firelen[UpDataA.W25Q64_BlockNum]);

      if ((OTA_Info.Firelen[UpDataA.W25Q64_BlockNum] % 4) == 0)
      { // 擦除单片机片内Flash的A区，准备写入新APP
        STM32_EraseFlash(STM32_A_START_PAGE, STM32_A_PAGE_NUM);   // 从A区起始页，擦除整个A区
        // 循环：写完整1024个字节的整页
        for (i = 0; (uint32_t)i < OTA_Info.Firelen[UpDataA.W25Q64_BlockNum] / STM32_PAGE_SIZE; i ++)  // 整个大小整除1024
        { // 从W25Q64外部Flash读1024字节到缓冲区
          W25Q64_Read(UpDataA.UpDatabuff, i*1024 + UpDataA.W25Q64_BlockNum * 64 * 1024, STM32_PAGE_SIZE); // * 64 * 1024：一块区域是64k
          // 把缓冲区数据写入STM32片内A区Flash
          STM32_WriteFlash(STM32_A_SADDR + i * STM32_PAGE_SIZE, (uint32_t *)UpDataA.UpDatabuff, STM32_PAGE_SIZE);
        }
        if ((OTA_Info.Firelen[UpDataA.W25Q64_BlockNum] % 1024) != 0)
        {
          W25Q64_Read(UpDataA.UpDatabuff, i*1024 + UpDataA.W25Q64_BlockNum * 64 * 1024, OTA_Info.Firelen[UpDataA.W25Q64_BlockNum] % 1024);
          STM32_WriteFlash(STM32_A_SADDR + i * STM32_PAGE_SIZE, (uint32_t *)UpDataA.UpDatabuff, OTA_Info.Firelen[UpDataA.W25Q64_BlockNum] % 1024);
        }
        if (UpDataA.W25Q64_BlockNum == 0)
        {
          OTA_Info.OTA_flag = 0;
          M24C02_WriteOTAInfo(); // 把标记写入M24C02 EEPROM，以便BootLoader上电后读取，防止掉电丢失
        }
        u1_printf("A区升级完成\r\n");
        // 升级完成，系统软复位，重启后Bootloader就会直接跳A区APP
        HAL_NVIC_SystemReset();
      }
      else
      {
        u1_printf("长度错误\r\n");
        // 相当于把UPDATA_A_FLAG的二进制格式里为1的位取0:BootStaFlag = BootStaFlag & (取反~UPDATA_A_FLAG);由于BootStaFlag |= UPDATA_A_FLAG，所以这里是清零操作
        BootStaFlag &= ~UPDATA_A_FLAG;  //长度不对，清除内存里的升级标记，放弃本次升级
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
