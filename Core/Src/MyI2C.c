#include <stdint.h>
#include "gpio.h"
#include "stm32f1xx_hal_gpio.h"
#include "delay_us.h"

/*
 *  该函数用于写I2C的SCL引脚电平
 *  BitValue:1表示写高电平，0表示写低电平
 */
void MyI2C_W_SCL(uint8_t BitValue)
{// BitValude只是形参（传入的值），(GPIO_PinState)是其结构体，强制转换成GPIO_PinState类型，BitValue是0或1
    HAL_GPIO_WritePin(SCL_GPIO_Port, SCL_Pin, (GPIO_PinState)BitValue);
}

/*
    该函数用于写I2C的SDA引脚电平
    BitValue:1表示写高电平，0表示写低电平
 */
void MyI2C_W_SDA(uint8_t BitValue)
{
    HAL_GPIO_WritePin(SDA_GPIO_Port, SDA_Pin, (GPIO_PinState)BitValue);
    //	Delay_us(10);		//延时10us，防止时序频率超过要求，为了提高效率，此处不进行延时
}

/*
 *  该函数用于读I2C的SDA引脚电平
 */
uint8_t MyI2C_R_SDA(void)
{
    uint8_t BitValue;
    BitValue = HAL_GPIO_ReadPin(SDA_GPIO_Port, SDA_Pin);
    //	Delay_us(10);		//延时10us，防止时序频率超过要求，为了提高效率，此处不进行延时
    return BitValue;		//返回SDA电平
}

/*
    该函数用于产生I2C的起始信号
 */
void MyI2C_Start(void)
{
    MyI2C_W_SDA(1);							//释放SDA，确保SDA为高电平
    MyI2C_W_SCL(1);							//释放SCL，确保SCL为高电平
    delay_us(2);
    MyI2C_W_SDA(0);							//在SCL高电平期间，拉低SDA，产生起始信号
    delay_us(2);
    MyI2C_W_SCL(0);							//起始后把SCL也拉低，即为了占用总线，也为了方便总线时序的拼接
}

/*
    该函数用于产生I2C的停止信号
 */
void MyI2C_Stop(void)
{
    MyI2C_W_SDA(0);							//拉低SDA，确保SDA为低电平
    MyI2C_W_SCL(1);							//释放SCL，使SCL呈现高电平
    delay_us(2);
    MyI2C_W_SDA(1);							//在SCL高电平期间，释放SDA，产生终止信号
}

/*
 *  该函数用于发送一个字节数据
 *  Byte:要发送的字节数据
 */
void MyI2C_SendByte(uint8_t Byte)
{
    uint8_t i;
    for (i = 0; i < 8; i ++)				//循环8次，主机依次发送数据的每一位
    {
        /*
         *例：选取到Byte的第四位数据时，0x80中的1移到第四位，与上Byte的第四位数据，0就直接为0，1与上1才为1
         *两个!可以对数据进行两次逻辑取反，作用是把非0值统一转换为1，即：!!(0) = 0，!!(非0) = 1
         */
        MyI2C_W_SDA(!!(Byte & (0x80 >> i)));//使用掩码的方式取出Byte的指定一位数据并写入到SDA线
        MyI2C_W_SCL(1);						//释放SCL，从机在SCL高电平期间读取SDA
        MyI2C_W_SCL(0);						//拉低SCL，主机开始发送下一位数据
    }
}

/*
 *  该函数用于接收一个字节数据
 */
uint8_t MyI2C_ReceiveByte(void)
{
    uint8_t i, Byte = 0x00;					//定义接收的数据，并赋初值0x00，此处必须赋初值0x00，后面会用到
    MyI2C_W_SDA(1);							//接收前，主机先确保释放SDA，避免干扰从机的数据发送
    for (i = 0; i < 8; i ++)				//循环8次，主机依次接收数据的每一位
    {
        MyI2C_W_SCL(1);						//释放SCL，主机机在SCL高电平期间读取SDA
        if (MyI2C_R_SDA())
        {
            Byte |= (0x80 >> i);		//读取SDA数据，并存储到Byte变量
        }
        //当SDA为1时，置变量指定位为1，当SDA为0时，不做处理，指定位为默认的初值0
        MyI2C_W_SCL(0);						//拉低SCL，从机在SCL低电平期间写入SDA
    }
    return Byte;							//返回接收到的一个字节数据
}

/*
 *  该函数用于发送应答位（主机发送应答，发送1就代表让从机停止发送数据，因为是SDA置1，代表主机释放SDA）
 *  AckBit:1表示发送应答位1，0表示发送应答位0
 */
void MyI2C_SendAck(uint8_t AckBit)
{
    MyI2C_W_SDA(AckBit);					        //主机把应答位数据放到SDA线
    MyI2C_W_SCL(1);							//释放SCL，从机在SCL高电平期间，读取应答位
    MyI2C_W_SCL(0);							//拉低SCL，开始下一个时序模块
}

/*
 *  该函数用于接收应答位（主机接收应答）
 */
uint8_t MyI2C_ReceiveAck(void)
{
    uint8_t AckBit;							        //定义应答位变量
    MyI2C_W_SDA(1);							//接收前，主机先确保释放SDA，避免干扰从机的数据发送
    MyI2C_W_SCL(1);							//释放SCL，主机机在SCL高电平期间读取SDA
    AckBit = MyI2C_R_SDA();					        //将应答位存储到变量里
    MyI2C_W_SCL(0);							//拉低SCL，开始下一个时序模块
    return AckBit;							        //返回定义应答位变量
}

