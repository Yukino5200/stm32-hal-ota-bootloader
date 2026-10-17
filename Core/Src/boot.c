//
// Created by lenovo on 2026/10/12.
//

#include "boot.h"

#include <time.h>

#include "main.h"
#include "usart.h"
#include "AT24C02.h"
#include "fmc.h"
#include "W25Q64.h"

load_a load_A;      // 定义load_a类型的函数

/**
 * @brief  Bootloader业务分支判断函数
 * @note   调用此函数前务必先调用AT24C02_ReadOTAInfo()，将AT24C02中OTA标记读取至RAM缓存OTA_Info
 * @param  无
 * @retval 无
 */
void BootLoader_Brance(void)
{
   if (BootLoader_Enter(5) == 0)
   {
       /* 判断OTA魔法标记是否匹配，匹配代表需要执行OTA固件更新流程 */
       if(OTA_Info.OTA_flag == OTA_SET_FLAG)
       {
           u1_printf("OTA更新\r\n");

           /* TODO：真正的OTA升级流程写在这里（从W25Q64取升级包 → 擦写A区Flash → 校验） */
           BootStaFlag |= UPDATA_A_FLAG;
           UpDataA.W25Q64_BlockNum = 0;
       }
       /* 标记不匹配，走正常业务，准备跳转A分区App */
       else
       {
           u1_printf("跳转A分区\r\n");
           LOAD_A(STM32_A_SADDR);
       }
   }
   else
   {
       u1_printf("进入BootLoader命令行\r\n");
       BootLoader_Info();
   }
}

/**
 * @brief  Bootloader启动等待函数，串口按键触发进入命令行
 * @param  timeout:等待超时时间（单位：秒）
 * @note   上电后在timeout秒内监听串口，收到字符'w'则进入Bootloader交互命令行；
 *         每秒轮询检测一次串口接收缓冲区U1_RxBuff[0]
 * @retval 1：检测到'w'，进入BootLoader命令行；0：等待超时，直接跳转APP
 */
uint8_t BootLoader_Enter(uint8_t timeout)
{
    u1_printf("%dms内输入小写字母 w ，以进入BootLoader命令行\r\n", timeout * 1000);
    while (timeout --)
    {
        HAL_Delay(1000);
        if (U1_RxBuff[0] == 'w')
        {
            return 1;                                       // 进入BootLoader命令行
        }
    }
    return 0;                                               // 超时未输入w，不进入BootLoader命令行
}

/**
 * @brief  打印Bootloader命令行菜单信息
 * @note   进入Bootloader交互模式后，串口输出功能菜单，展示所有可执行命令
 * @param  无
 * @retval 无
 */
void BootLoader_Info(void)
{
    u1_printf("\r\n");
    u1_printf("[1]擦除A区\r\n");
    u1_printf("[2]串口IAP下载A区程序\r\n");
    u1_printf("[3]设置OTA版本号\r\n");
    u1_printf("[4]查询OTA版本号\r\n");
    u1_printf("[5]向外部Flash下载程序\r\n");
    u1_printf("[6]使用外部Flash内程序\r\n");
    u1_printf("[7]重启\r\n");
    u1_printf("[8]清除OTA标志\r\n");
}

/**
 * @brief  Bootloader串口命令解析处理函数
 * @param  data:串口收到命令数据缓冲区指针
 * @param  datalen:本次收到数据字节长度
 * @note   靠 BootStaFlag 把输入分成几种【互斥的模式】（下面是一条 else if 链，先匹配先执行）：
 *         1) BootStaFlag == 0            ：空闲态，只认单字符菜单命令 '1'~'8'
 *         2) IAP_XMODEM_DATA_FALG        ：Xmodem 接收中，认 133 字节数据帧(0x01) 和 1 字节结束帧(0x04)
 *         3) SET_VERSION_FLAG            ：等版本号，把字节流攒满 26 个字符再校验
 *         4) UPDATA_TO_W25Q64_FLAG       ：等用户输入块号(1~9)，用于把固件下载进 W25Q64
 *         5) USE_W25Q64_FLAG             ：等用户输入块号(1~9)，用于选一块 W25Q64 里的固件升级 A 区
 *         ★ 只要不是空闲态，菜单键 '1'~'8' 都会被当前模式“吃掉”（不会当成命令执行），
 *           所以每个模式都应该给出退出方式（例如版本号模式按 ESC/回车退出）
 * @retval 无
 */
void BootLoader_Event(uint8_t *data, uint16_t datalen)
{
    int i;
    static uint16_t VerLen = 0;      /* “设置版本号”状态下已收集的字符数 */

    /*--------------------------------------------------------------------------
     *                          无事件发生，前端选项供选择
     -------------------------------------------------------------------------*/
    if (BootStaFlag == 0)
    {
        if ((datalen == 1) && (data[0] == '1'))
        {
            u1_printf("擦除A区\r\n");
            STM32_EraseFlash(STM32_A_START_PAGE, STM32_A_PAGE_NUM);
        }
        else if ((datalen == 1) && (data[0] == '2'))
        {
            u1_printf("通过Xmodem协议，串口IAP下载A区程序，请使用.bin格式文件\r\n");
            STM32_EraseFlash(STM32_A_START_PAGE, STM32_A_PAGE_NUM);
            BootStaFlag |= (IAP_XMODEM_C_FLAG | IAP_XMODEM_DATA_FALG);
            UpDataA.XmodemTimer = 0;
            UpDataA.XmodemNum = 0;
        }
        else if ((datalen == 1) && (data[0] == '3'))
        {
            u1_printf("设置版本号\r\n");
            VerLen = 0;
            memset(OTA_Info.OTA_version, 0, sizeof(OTA_Info.OTA_version));  /* 清空，顺便保证字符串有 '\0' */
            BootStaFlag |= SET_VERSION_FLAG;
        }
        else if ((datalen == 1) && (data[0] == '4'))
        {
            AT24C02_ReadOTAInfo();
            u1_printf("版本号:%s\r\n", OTA_Info.OTA_version);
        }
        else if ((datalen == 1) && (data[0] == '5'))
        {
            u1_printf("[5]向外部Flash下载程序，输入需要使用的块编号（1~9）\r\n");
            BootStaFlag |= UPDATA_TO_W25Q64_FLAG;   // 切到“等块号”模式，真正的下载在下面那个分支里发起
        }
        else if ((datalen == 1) && (data[0] == '6'))
        {
            u1_printf("[6]使用外部Flash下载程序，输入需要使用的块编号（1~9）\r\n");
            BootStaFlag |= USE_W25Q64_FLAG;         // 切到“等块号”模式，选好后置 UPDATA_A_FLAG 走升级流程
        }
        else if ((datalen == 1) && (data[0] == '7'))
        {
            u1_printf("[7]重启\r\n");
            HAL_Delay(100);
            HAL_NVIC_SystemReset();
        }
        else if ((datalen == 1) && (data[0] == '8'))
        {
            u1_printf("[8]清除OTA标志\r\n");
            OTA_Info.OTA_flag = 0;      /* ① 先清 RAM 里的标志 */
            AT24C02_WriteOTAInfo();     /* ② 再整块写回 EEPROM（这样才真正生效） */
        }
    }

    /*--------------------------------------------------------------------------
     *                          发生Xmodem事件，进行处理
     --------------------------------------------------------------------------*/
    //========= 第一大块：收到Xmodem数据包(SOH帧 133字节)处理 =========
    else if (BootStaFlag & IAP_XMODEM_DATA_FALG)
    {   // 判断：收到长度133字节、首字节SOH(0x01)标准Xmodem数据包
        if (datalen == 133 && data[0] == 0x01)
        {
            BootStaFlag &= ~IAP_XMODEM_C_FLAG;  // 清除本次接收完成标志
            UpDataA.XmodemCRC = Xmodem_CRC16(&data[3], 128);
            if (UpDataA.XmodemCRC == (data[131] << 8) + data[132])
            {   // CRC校验通过
                UpDataA.XmodemNum ++;
                memcpy(&UpDataA.UpDatabuff[(UpDataA.XmodemNum - 1) % (STM32_PAGE_SIZE / 128) * 128], &data[3], 128);    // 将128字节固件拷贝至RAM页缓存
                // 判断RAM缓存是否攒满一整个Flash页，满则写入Flash
                if ((UpDataA.XmodemNum % (STM32_PAGE_SIZE / 128)) == 0)
                {
                    if (BootStaFlag & UPDATA_XMODEM_FLAG)
                    {   // 【存进 W25Q64】每收满 8 包(=8×128B=1024B) 就写进 W25Q64 的 4 个 256B 页
                        //   页号 = 本块内页偏移 + 块偏移
                        //   本块内页偏移 = (XmodemNum/8 - 1)*4 + i  ← 每个 1024B 占 4 页；i=0..3 依次取其中 256B
                        //   块偏移       = W25Q64_BlockNum * 64 * 4 ← 1 块 = 64KB = 256 页(256B/页)
                        for (i = 0; i < 4; i ++)
                        {
                            W25Q64_PageWrite(&UpDataA.UpDatabuff[i * 256], (UpDataA.XmodemNum / 8 - 1) * 4 + i +UpDataA.W25Q64_BlockNum * 64 * 4);
                        }
                    }
                    else
                    {   // 【存进片内 Flash】串口 IAP 模式：直接把攒好的 1KB 写进 A 区
                        // 发送完一整个页的数据到缓冲区，写入flash
                        STM32_WriteFlash(STM32_A_SADDR + (UpDataA.XmodemNum / (STM32_PAGE_SIZE / 128) - 1) * STM32_PAGE_SIZE, (uint32_t *)UpDataA.UpDatabuff, STM32_PAGE_SIZE);
                    }

                }
                u1_printf("\x06");  // 回复ACK(0x06)，通知上位机继续发下一包
            }
            else
            {   // CRC校验失败，回复NAK(0x15)，请求上位机重传当前包
                u1_printf("\x15");
            }
        }
        //========= 第二大块：收到EOT结束帧(0x04)，上位机全部数据传输完成 =========
        if (datalen == 1 && data[0] == 0x04)
        {
            u1_printf("\x06");  // 回复ACK(0x06)，告知上位机：收到结束信号
            // 缓存还有不足一页的剩余固件，补写入Flash
            if (UpDataA.XmodemNum % (STM32_PAGE_SIZE / 128) != 0)
            {   // 最后不足一整页的“零头”也要落盘，否则固件末尾几百字节会丢
                if (BootStaFlag & UPDATA_XMODEM_FLAG)
                {   // 同样按 256B/页 写 W25Q64；这里页偏移用 XmodemNum/8（不减 1），因为要接在已写数据之后
                    for (i = 0; i < 4; i ++)
                    {
                        W25Q64_PageWrite(&UpDataA.UpDatabuff[i * 256], (UpDataA.XmodemNum / 8) * 4 + i +UpDataA.W25Q64_BlockNum * 64 * 4);
                    }
                }
                else
                {   // 串口 IAP：零头按实际长度写进 A 区
                    STM32_WriteFlash(STM32_A_SADDR + UpDataA.XmodemNum / (STM32_PAGE_SIZE / 128) * STM32_PAGE_SIZE, (uint32_t *)UpDataA.UpDatabuff, UpDataA.XmodemNum % (STM32_PAGE_SIZE / 128) * 128);
                }
            }
            BootStaFlag &= ~IAP_XMODEM_DATA_FALG;   // 清除Xmodem数据接收标志位
            if (BootStaFlag & UPDATA_XMODEM_FLAG)
            {   // 本次是“下载进 W25Q64”：不重启，把这块固件的长度记进 EEPROM，然后回菜单等下一步
                u1_printf("向外部Flash下载程序完成\r\n");
                BootStaFlag &= ~UPDATA_XMODEM_FLAG;
                OTA_Info.Firelen[UpDataA.W25Q64_BlockNum] = UpDataA.XmodemNum * 128;   // 长度 = 包数 × 128 字节
                AT24C02_WriteOTAInfo();
                HAL_Delay(100);
            }
            else
            {   // 本次是“串口 IAP 直接下载 A 区”：写完整块新程序，软复位；
                // 复位后 Bootloader 发现 OTA 标志不匹配，就会直接跳进新 App
                HAL_Delay(100);
                HAL_NVIC_SystemReset();
            }
        }
    }

    /*--------------------------------------------------------------------------
     *                          发生设置版本号事件，进行处理
     --------------------------------------------------------------------------*/
    else if (BootStaFlag & SET_VERSION_FLAG)
    {
        /* 单独一个回车/换行/ESC → 放弃设置，退回菜单（否则这些键会被当成版本号字符吞掉） */
        if ((datalen == 1) && ((data[0] == '\r') || (data[0] == '\n') || (data[0] == 0x1B)))
        {
            VerLen = 0;
            BootStaFlag &= ~SET_VERSION_FLAG;
            u1_printf("\r\n已退出设置版本号\r\n");
            return;
        }

        /** 把本包里的字节逐个攒进 OTA_version：逐字符发/拆包发/整串发都能收全，
        * 回车换行直接跳过（不影响已攒内容）
        **/
        while (datalen > 0)
        {
            if ((*data != '\r') && (*data != '\n') && (VerLen < 26))
            {
                OTA_Info.OTA_version[VerLen++] = *data;
            }
            data++;
            datalen--;
        }

        if (VerLen == 26)                   /* 攒满 26 个字符 → 开始校验 */
        {
            /* ---- 手工解析版本号（替代原来的 sscanf：省下整个 scanf 引擎，约 2~3KB Flash）----
               期望格式：VER-x.y.z-YYYY/MM/DD-hh:mm   （共 26 个字符，每段数字位数不限）
               思路：先比对字面量 "VER-" ，然后“读一段数字 → 比对一个分隔符”，重复 8 次，
                     分隔符依次是  .  .  -  /  /  -  :  ，最后必须正好到字符串结尾        */
            const char *p = (const char *)OTA_Info.OTA_version;
            const char sep[8] = { '.', '.', '-', '/', '/', '-', ':', '\0' };
            uint8_t fmt_ok = 1;

            if ((p[0] != 'V') || (p[1] != 'E') || (p[2] != 'R') || (p[3] != '-'))
            {
                fmt_ok = 0;
            }
            p += 4;

            for (i = 0; (i < 8) && fmt_ok; i++)
            {
                if ((*p < '0') || (*p > '9'))       /* 每段数字至少要有一位 */
                {
                    fmt_ok = 0;
                    break;
                }
                while ((*p >= '0') && (*p <= '9'))  /* 跳过这一段的其余数字位 */
                {
                    p++;
                }
                if (sep[i] != '\0')
                {
                    if (*p != sep[i])               /* 分隔符必须对得上 */
                    {
                        fmt_ok = 0;
                        break;
                    }
                    p++;
                }
            }
            if (*p != '\0')                         /* 末尾不能有多余字符 */
            {
                fmt_ok = 0;
            }

            if (fmt_ok)
            {
                AT24C02_WriteOTAInfo();     /* 版本号已在 OTA_Info.OTA_version 里，整块写回 EEPROM */
                u1_printf("\r\n版本号正确\r\n");
                BootStaFlag &= ~SET_VERSION_FLAG;
            }
            else
            {
                u1_printf("\r\n版本号格式错误，请重试\r\n");
            }
            VerLen = 0;                     /* 复位计数，方便重新输入 */
        }
    }

    /*--------------------------------------------------------------------------
     *                          发生命令5事件，进行处理
     --------------------------------------------------------------------------*/
    else if (BootStaFlag & UPDATA_TO_W25Q64_FLAG)
    {   // 【[5] 的第二步】等用户选块号：先把这块 W25Q64(64KB) 擦干净，再进入 Xmodem 接收
        if(datalen == 1)
        {
            if ((data[0] >= '1') && (data[0] <= '9'))
            {
                UpDataA.W25Q64_BlockNum = data[0] - '0';    // ASCII码'n'转换成数字n
                // UPDATA_XMODEM_FLAG 是“分流开关”：置了就写 W25Q64，不置就写片内 A 区 Flash
                BootStaFlag |= (IAP_XMODEM_C_FLAG | IAP_XMODEM_DATA_FALG | UPDATA_XMODEM_FLAG);
                UpDataA.XmodemTimer = 0;
                UpDataA.XmodemNum = 0;
                OTA_Info.Firelen[UpDataA.W25Q64_BlockNum] = 0;   // 旧长度先清零，传完再写真实长度
                W25Q64_Erase64K(UpDataA.W25Q64_BlockNum);        // W25Q64 只能“先擦后写”，所以先擦整块
                BootStaFlag &= ~UPDATA_TO_W25Q64_FLAG;
                u1_printf("通过Xmodem协议，向W25Q64发送第%d块下载程序，请使用.bin格式文件\r\n", UpDataA.W25Q64_BlockNum);
            }
            else
            {
                u1_printf("编号错误，请重新输入\r\n");
            }
        }
        else
        {
            u1_printf("数据长度错误，请重新输入\r\n");
        }
    }

    /*--------------------------------------------------------------------------
     *                          发生命令6事件，进行处理
     --------------------------------------------------------------------------*/
    else if (BootStaFlag & USE_W25Q64_FLAG)
    {   // 【[6] 的第二步】等用户选块号：选好后置 UPDATA_A_FLAG，
        // 主循环就会按 Firelen[块号] 从这块 W25Q64 里读固件 → 擦写 A 区 → 软复位 → 跳新 App
        if(datalen == 1)
        {
            if ((data[0] >= '1') && (data[0] <= '9'))
            {
                UpDataA.W25Q64_BlockNum = data[0] - '0';    // ASCII码'n'转换成数字n
                BootStaFlag |= UPDATA_A_FLAG;               // 交给主循环去执行“从 W25Q64 升级 A 区”
                BootStaFlag &= ~USE_W25Q64_FLAG;
            }
            else
            {
                u1_printf("编号错误，请重新输入\r\n");
            }
        }
        else
        {
            u1_printf("数据长度错误，请重新输入\r\n");
        }
    }
}

/**
 * @brief 设置主栈指针MSP（主堆栈指针）
 * @param addr 要设置的栈顶地址
 * @note Cortex-M内核有两个栈：MSP(主栈，Bootloader默认用)、PSP(进程栈)
 *       Bootloader跳转到APP前，必须先把MSP改成APP的栈顶地址
 *       APP固件的向量表首地址存放的就是APP栈顶地址
 */
static inline void MSR_SP(uint32_t addr)
{
    __asm__ volatile (
        "MSR MSP, %0 \n"
        :
        : "r"(addr)
    );
}

/**
 * @brief 跳转到APP应用程序，HAL/GCC版本，逻辑和原代码保持一致
 * @param addr APP向量表起始地址（Flash上APP的首地址）
 * @note 1. 读取向量表首项作为APP栈顶，校验栈顶是否在合法RAM区间
 *          STM32_RAM_SADDR(0x20000000) ~ STM32_RAM_EADDR(0x20005000)
 *          注意上界就是20K RAM的栈顶：向量表首项是“栈顶=第一个不使用的地址”，
 *          App的 _estack = ORIGIN(RAM)+LENGTH(RAM) 正好等于0x20005000，必须算合法
 *       2. 合法则切换MSP主栈，再读取复位入口赋值给函数指针
 *       3. 调用load_A()即可跳转，跳转成功后不再返回Bootloader
 * @warning 跳转前必须关闭全局中断，关闭I2C/SPI等外设
 */
void LOAD_A(uint32_t addr)          // 调用时 addr = STM32_A_SADDR = 0x08005000
{
    uint32_t app_sp    = *(uint32_t *)addr;         // 向量表第0项：App的栈顶(MSP初值)     app_sp    = 0x20005000
    uint32_t app_reset = *(uint32_t *)(addr + 4);   // 向量表第1项：App的Reset_Handler   app_reset = 0x0800878D

    if((app_sp >= STM32_RAM_SADDR) && (app_sp <= STM32_RAM_EADDR))   // 栈顶必须落在 RAM 里
    {
        MSR_SP(app_sp);                             // 设置SP
        load_A = (load_a)app_reset;
        BootLoader_Clear();                         // 清除B区使用的复位外设
        load_A();                                   // 跳过去就不回来了
    }
    else
    {
        u1_printf("A区栈顶非法: 0x%08X，取消跳转\r\n", app_sp);   // 失败必须留痕
    }
}

/**
 * @brief Bootloader跳转APP前，注销复位外设
 * @note 关闭串口、GPIO，把外设恢复成默认状态，防止残留外设干扰APP运行
 */
void BootLoader_Clear(void)
{
    HAL_UART_DeInit(&huart1);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_All);
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_All);
}

/**
 * @brief  Xmodem协议 CRC16 校验计算函数
 * @param  data: 待计算CRC的数据缓冲区起始指针
 * @param  datalen: 需要计算校验的字节总长度
 * @retval uint16_t: 返回计算完成的16位CRC校验值
 * @note   多项式 0x1021，初始值Crcinit = 0x0000；
 *         算法流程：
 *         1. 取出1字节数据左移8位，与当前CRC寄存器异或
 *         2. 循环8次逐bit处理，判断最高位0x8000
 *            - 最高位为1：CRC左移1位后异或多项式0x1021
 *            - 最高位为0：CRC仅左移1位
 *         适用：Xmodem传输帧校验，用于OTA固件包完整性校验
 */
uint16_t Xmodem_CRC16(uint8_t *data, uint16_t datalen)
{
    uint8_t i;
    uint16_t Crcinit = 0x0000;
    uint16_t Crcipoly = 0x1021;

    while (datalen --)
    {
        Crcinit = (*data << 8) ^ Crcinit;   // 按位异或：二进制中异同取1
        for (i = 0; i < 8; i ++)
        {
            if (Crcinit & 0x8000)
                Crcinit = (Crcinit << 1) ^ Crcipoly;
            else
                Crcinit = Crcinit << 1;
        }
        data ++;
    }
    return Crcinit;
}