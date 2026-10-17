//
// Created by lenovo on 2026/10/10.
//
#include "AT24C02.h"
#include "main.h"
#include "MyI2C.h"

/**
 * @brief  AT24C02写入1个字节
 * @param  addr: EEPROM内部地址 0~255
 * @param  wdata: 需要写入的字节数据
 * @retval 0:收到应答成功  1：无应答，通信失败
 */
uint8_t AT24C02_WriteByte(uint8_t addr, uint8_t wdata)
{
    uint8_t ack;

    MyI2C_Start();                      //1、发起起始信号
    MyI2C_SendByte(AT24C02_WADDR);      //2、发送设备写地址
    ack = MyI2C_ReceiveAck();            //等待从机应答
    if(ack != 0)                         //没有应答直接退出
    {
        MyI2C_Stop();
        return 1;
    }

    MyI2C_SendByte(addr);                //3、发送EEPROM内部存储地址
    ack = MyI2C_ReceiveAck();
    if(ack != 0)
    {
        MyI2C_Stop();
        return 1;
    }

    MyI2C_SendByte(wdata);               //4、发送要写的数据
    ack = MyI2C_ReceiveAck();
    if(ack != 0)
    {
        MyI2C_Stop();
        return 1;
    }

    MyI2C_Stop();                        //5、停止信号

    HAL_Delay(5);                        //重要！EEPROM内部页烧写等待时间，最大5ms

    return 0;  //成功返回0
}

/**
 * @brief  AT24C02页写入，一页最多16字节
 * @param  addr: EEPROM起始内部地址
 * @param  wdata: 待写入数据缓冲区指针
 * @retval 0成功；1设备地址无应答；2存储器地址无应答；3+i 第i个数据发送的时候应答失败
 * @attention !!!起始地址不要跨页；写完之后内部烧写需要延时5ms
 */
uint8_t AT24C02_WritePage(uint8_t addr, uint8_t *wdata)
{
    uint8_t i;
    uint8_t ack;

    MyI2C_Start();
    MyI2C_SendByte(AT24C02_WADDR);
    ack = MyI2C_ReceiveAck();
    if(ack != 0)
    {
        MyI2C_Stop();
        return 1;
    }

    MyI2C_SendByte(addr);
    ack = MyI2C_ReceiveAck();
    if(ack != 0)
    {
        MyI2C_Stop();
        return 2;
    }

    for(i = 0; i < 16; i++)
    {
        MyI2C_SendByte(wdata[i]);
        ack = MyI2C_ReceiveAck();
        if(ack != 0)
        {
            MyI2C_Stop();
            return (3 + i);
        }
    }

    MyI2C_Stop();
    HAL_Delay(5);   // EEPROM内部烧写等待

    return 0;
}

/**
 * @brief AT24C02连续多字节读取
 * @param addr: EEPROM内部起始地址
 * @param rdata: 接收数据缓冲区指针
 * @param datalen: 需要读取多少字节(1~256)
 * @retval 0成功；1写设备地址失败；2写内部地址失败；3读设备地址应答失败
 */
uint8_t AT24C02_ReadData(uint8_t addr, uint8_t *rdata, uint16_t datalen)
{
    uint8_t i;
    uint8_t ack;

    MyI2C_Start();
    MyI2C_SendByte(AT24C02_WADDR);
    ack = MyI2C_ReceiveAck();
    if(ack != 0)
    {
        MyI2C_Stop();
        return 1;
    }

    MyI2C_SendByte(addr);
    ack = MyI2C_ReceiveAck();
    if(ack != 0)
    {
        MyI2C_Stop();
        return 2;
    }

    MyI2C_Start();                 // 重复起始信号！重要，不需要停止
    MyI2C_SendByte(AT24C02_RADDR);
    ack = MyI2C_ReceiveAck();
    if(ack != 0)
    {
        MyI2C_Stop();
        return 3;
    }

    //前面datalen‑1个：读完之后主机发送ACK(0)给从机，继续读下一个
    for(i = 0; i < datalen - 1; i++)
    {
        rdata[i] = MyI2C_ReceiveByte();
        MyI2C_SendAck(0);    //主机ACK，告诉从机继续发后面的数据
    }
    //最后一个字节读完，主机发送NACK(1)，通知从机结束传输
    rdata[datalen - 1] = MyI2C_ReceiveByte();
    MyI2C_SendAck(1);

    MyI2C_Stop();
    return 0;
}
