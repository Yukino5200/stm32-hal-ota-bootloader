//
// Created by lenovo on 2026/10/11.
//
#include "main.h"
#include "W25Q64.h"
#include "spi.h"


/**
 * @brief 等待W25Q64芯片操作完成（等待忙标志清除）
 * @note W25Q执行擦除/页写时，芯片内部需要时间，不能立刻接收新命令
 *       状态寄存器bit0=BUSY位：1=芯片正在忙；0=空闲，可以接收指令
 * @retval 无
 */
void W25Q64_WaitBusy(void)
{
	uint8_t res;
	do
	{
		CS_ENABLE;							// 拉低CS，选中W25Q64，开始本次SPI通信
		SPI1_ReadWriteByte(0x05);		// MOSI线：主机发送 0x05，从机接收； MISO线：从机发送数据，主机接收（函数内没写没保存，舍弃）
		res = SPI1_ReadWriteByte(0xFF);	// MOSI线：主机发哑字节0xFF，从机接受； MISO线：由于主机发送时钟信号激活从机，从机发送数据（函数保存到res里），主机接收
		CS_DISABLE;							//拉高CS，结束本次SPI通信
	}while ((res & 0x01) == 0x01);			// 判断BUSY位，BUSY=1就循环等待；等于0退出
}

/**
 * @brief W25Q64 写使能，开启写/擦除权限
 * @note W25Q芯片上电默认**写保护**，任何擦除、页写入操作前，必须先调用本函数
 */
void W25Q64_WriteEnable(void)
{
	W25Q64_WaitBusy();				// 先等待芯片空闲，防止上一次操作没完成
	CS_ENABLE;						// CS拉低选中W25Q64
	SPI1_ReadWriteByte(0x06);	// 发送命令0x06：Write Enable 写使能
	CS_DISABLE;						// CS拉高结束通信
}

/**
 * @brief W25Q64 64KB大块擦除（命令0xD8）
 * @param blockNum:64K块编号 0~127（W25Q64一共8MB，8M/64K=128块）
 * @note 擦除后，该块内全部字节变为 0xFF
 */
void W25Q64_Erase64K(uint8_t blockNum)
{
	uint8_t wdata[4];

	wdata[0] = 0xD8;                           // 0xD8 = 64KB块擦除命令
	wdata[1] = (blockNum * 64*1024) >> 16;     // 地址高8位
	wdata[2] = (blockNum * 64*1024) >> 8;      // 地址中8位
	wdata[3] = (blockNum * 64*1024) >> 0;      // 地址低8位

	W25Q64_WaitBusy();       // 等待芯片空闲
	W25Q64_WriteEnable();         // 开启写使能（擦除前必须打开）
	CS_ENABLE;
	SPI1_Write(wdata,4);     // 一次性发送4个字节：命令 + 24bit起始地址
	CS_DISABLE;
	W25Q64_WaitBusy();       // 等待擦除操作硬件执行完成（擦除很慢）
}

/**
 * @brief W25Q64 整页写入（一页固定256个字节）
 * @param wbuff 待写入数据缓冲区指针
 * @param PageNum 页编号 0~32767（W25Q64总8MB，8M/256=32768页）
 * @note 调用完本函数后，外部必须调用W25Q64_WaitBusy()等待烧写完成
 * @note 写入区域必须预先擦除为0xFF，不可跨页
 */
void W25Q64_PageWrite(uint8_t *wbuff, uint16_t PageNum)
{
	uint8_t wdata[4];

	wdata[0] = 0x02;                      // 按页写入命令
	wdata[1] = (PageNum * 256) >> 16;     // 地址高8位
	wdata[2] = (PageNum * 256) >> 8;      // 地址中8位
	wdata[3] = (PageNum * 256) >> 0;      // 地址低8位

	W25Q64_WaitBusy();
	W25Q64_WriteEnable();
	CS_ENABLE;
	SPI1_Write(wdata,4);			  // 先发【命令+地址】：0x02 + 24bit起始地址
	SPI1_Write(wbuff,256);		  // 紧接着直接发256字节数据
	CS_DISABLE;
}

/**
 * @brief W25Q64 读取数据
 * @param rbuff 接收数据缓冲区指针
 * @param addr Flash起始读取地址
 * @param datalen 需要读取的字节长度
 * @note 读操作不需要写使能，可连续读取，无256个字节限制
 */
void W25Q64_Read(uint8_t *rbuff, uint32_t addr, uint32_t datalen)
{
	uint8_t wdata[4];
	wdata[0] = 0x03;
	wdata[1] = addr >> 16;
	wdata[2] = addr >> 8;
	wdata[3] = addr >> 0;

	W25Q64_WaitBusy();
	CS_ENABLE;
	SPI1_Write(wdata,4);
	SPI1_Read(rbuff, datalen);
	CS_DISABLE;
}
