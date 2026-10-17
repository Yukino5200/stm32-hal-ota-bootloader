//
// Created by lenovo on 2026/10/10.
//
#include "AT24C02.h"

#include "delay_us.h"
#include "main.h"
#include "MyI2C.h"
#include "string.h"

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
 * @brief  AT24C02页写入，一页最多16个字节
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
        MyI2C_SendByte(wdata[i]);   // 1个循环写入1个字节（uint8_t）
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
 * @brief AT24C02连续多字节读取，从AT24C02的addr地址开始读，读strlen个字节，放入rdata中
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

/**
 * @brief  读取EEPROM(M24C02/AT24C02)内保存的OTA信息块
 * @note   读取前先清空结构体，避免旧脏数据干扰；数据从EEPROM地址0开始读取
 * @param  无
 * @retval 无
 */
void AT24C02_ReadOTAInfo(void)
{
    // 将OTA信息结构体整块内存预先清零，防止残留脏数据
    memset(&OTA_Info, 0, OTA_INFOCB_SIZE);

    // 从M24C02的0地址读取OTA_INFOCB_SIZE字节数据，存入OTA_Info结构体
    // (uint8_t *)强转：EEPROM读写函数是按字节操作，需要uint8_t类型缓冲区指针
    AT24C02_ReadData(0, (uint8_t *)&OTA_Info, OTA_INFOCB_SIZE);
}

/**
 * @brief  清除EEPROM里的OTA标志（把这次升级请求消费掉）
 * @note   OTA标志就是EEPROM地址0开始的4个字节(OTA_InfoCB.OTA_flag)，
 *         逐字节写0，写完标志 != OTA_SET_FLAG，下次复位就正常跳A区
 * @param  无
 * @retval 0：成功；1：写EEPROM失败（I2C无应答等）
 */
uint8_t AT24C02_ClearOTAFlag(void)
{
    uint8_t i;

    for(i = 0; i < sizeof(OTA_Info.OTA_flag); i++)
    {
        if(AT24C02_WriteByte(i, 0x00) != 0)     // 内部自带5ms烧写等待
        {
            return 1;
        }
    }

    OTA_Info.OTA_flag = 0;      // 同步清掉RAM缓存，别再用旧值判断

    return 0;
}

/**
 * @brief  将OTA信息结构体写入AT24C04 EEPROM
 * @note   AT24C04一页为16字节，此函数按页首地址分页写入
 *         每次从EEPROM页首(i*16)写入16字节，避免跨页回卷覆盖
 *         每写完一页延时5ms，等待EEPROM内部烧录完成
 * @attention 仅适用于OTA_INFOCB_SIZE是16整数倍的情况；
 *             若结构体长度不是16倍数，末尾不足16字节部分不会被写入
 * @param  无，全局结构体OTA_Info作为写入数据源
 * @retval 无
 */
void AT24C02_WriteOTAInfo(void)
{
    uint8_t i;
    uint8_t *wptr;

    wptr = (uint8_t *)&OTA_Info;
    // AT24C04一页16字节，循环分页写入(这里共3页)
    for(i=0; i<OTA_INFOCB_SIZE/16; i++)
    {
        AT24C02_WritePage(i*16, wptr+i*16);
        delay_us(5);
    }
}
