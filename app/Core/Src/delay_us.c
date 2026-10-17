//
// Created by lenovo on 2026/10/9.
//

#include "delay_us.h"

extern TIM_HandleTypeDef htim2;

/*
 *  该函数用于延时us微秒
 *  参数:要延时的us微秒数
 *  注意：该函数需要在定时器2中断中调用，否则会导致计数器清零后，计数器值不准确
 */
void delay_us(uint32_t us)
{
    __HAL_TIM_SET_COUNTER(&htim2, 0);       // 计数器清零
    while(__HAL_TIM_GET_COUNTER(&htim2) < us)
    {
        // 等待计数到达目标值
    }
}
