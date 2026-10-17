//
// Created by lenovo on 2026/10/11.
//
#include "fmc.h"
#include "main.h"
#include "stm32f1xx_hal_flash_ex.h"

/**
 * @brief 擦除片内Flash单个页面，替代废弃HAL_FLASH_PageErase
 * @param page_addr: 页面起始物理地址，必须是1024个字节对齐，例0x08001000
 * @retval 0成功，非0失败
 */
uint32_t STM32_Flash_EraseSinglePage(uint32_t page_addr)
{
    FLASH_EraseInitTypeDef erase_init;
    uint32_t error_page = 0U;

    erase_init.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress= page_addr;
    erase_init.NbPages    = 1U;      // 只擦1页
    erase_init.Banks      = FLASH_BANK_1;

    if(HAL_FLASHEx_Erase(&erase_init, &error_page) != HAL_OK)
    {
        return error_page; // 返回出错地址
    }
    return 0U; //成功
}

/**
 * @brief STM32F103 片内Flash页擦除
 * @param start 起始页号
 * @param num 擦除页数
 */
void STM32_EraseFlash(uint16_t start, uint16_t num)
{
    uint16_t i;
    HAL_FLASH_Unlock();   //解锁，替代 fmc_unlock()
    for(i = 0; i < num; i++)
    {
        uint32_t page_addr = 0x08000000U + (uint32_t)(start + i) * 1024U;
        //STM32F1 页擦除，参数直接填页起始地址
        STM32_Flash_EraseSinglePage(page_addr);
    }
    HAL_FLASH_Lock();     //上锁，替代 fmc_lock()
}

/**
 * @brief  向片内Flash写入多组32位字数据，接口完全兼容GD32原版GD32_WriteFlash
 * @param  saddr:  Flash写入起始物理地址（4字节对齐）
 * @param  wdata:  待写入数据缓冲区，uint32_t类型数组指针
 * @param  wnum:   需要写入的总字节数量，每次循环写入4个字节
 * @note   1. 写入前必须预先擦除对应Flash页面；Flash仅能1->0，不能0->1
 * @note   2. STM32F1硬件最小写入单元为半字(2个字节)，内部自动将32bit拆分为两次半字写入
 * @note   3. 函数内部自带Unlock/Lock，调用外部无需重复解锁上锁
 * @retval 无返回值，和GD32原版fmc逻辑保持一致
 */
void STM32_WriteFlash(uint32_t saddr, uint32_t *wdata, uint32_t wnum)
{
    HAL_FLASH_Unlock();

    while(wnum)
    {
        // 拆成两个半字写入：低16bit + 高16bit
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, saddr, (uint16_t)(*wdata & 0xFFFFU));
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, saddr+2, (uint16_t)((*wdata >> 16) & 0xFFFFU));

        wnum -=4;
        saddr +=4;
        wdata++;
    }

    HAL_FLASH_Lock();
}
