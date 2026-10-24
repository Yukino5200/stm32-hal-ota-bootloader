# OTA 上位机（与 D:\BootLoader\OTA 的 BootLoader 配套）

一个命令行上位机，把 BootLoader 的菜单操作全自动化：查/设版本号、擦 A 区、Xmodem 直刷 A 区、下载到 W25Q64 再搬进 A 区。

- 脚本：`ota_host.py`（Python 3.8+，只依赖 `pyserial`）
- 对齐的 MCU 代码：`OTA/Core/Src/boot.c`、`OTA/Core/Src/main.c`

## 一、准备

```powershell
# 本机已装好（pyserial 3.5）
D:\Python312-32\python.exe .\ota_host.py --help

# 换一台机器时
pip install pyserial
```

板子接的是 USB 转串口（CH340），确认端口号（设备管理器看 COMx），**别让 SecureCRT 同时占着这个串口**，否则打不开。

## 二、常用操作

```powershell
# 1) 进命令行并打印菜单
python ota_host.py -p COM9 menu

# 2) 查版本号（读 EEPROM）
python ota_host.py -p COM9 ver-get

# 3) 设版本号（正好 26 字符）
python ota_host.py -p COM9 ver-set VER-1.0.0-2026/10/18-20:00

# 4) 串口 Xmodem 直刷 A 区（等价菜单 [2]）
python ota_host.py -p COM9 iap app.bin

# 5) 先把固件存进 W25Q64 第 1 块，再搬到 A 区（等价菜单 [5] → [6]）
python ota_host.py -p COM9 push 1 app.bin
python ota_host.py -p COM9 use 1

# 6) 其它
python ota_host.py -p COM9 erase        # [1] 擦 A 区
python ota_host.py -p COM9 clear-flag   # [8] 清 OTA 标志
python ota_host.py -p COM9 reboot       # [7] 软复位
```

调试时加 `-v` 看原始字节、加 `--log rxtx.txt` 把板子输出存成文件：

```powershell
python ota_host.py -p COM9 -v --log rxtx.txt iap app.bin
```

## 三、协议（与 boot.c 一一对应）

| 环节 | 细节 | boot.c / main.c |
|---|---|---|
| 进命令行 | 上电 5 s 内收到小写 `w`；**只认 `U1_RxBuff[0]`**，所以只能单独发 `w`、别带回车 | `BootLoader_Enter(5)` |
| 菜单键 | 单字符 `1`~`8`，不带 `\r\n` | `BootLoader_Event` 空闲分支 |
| [2] 直刷 | 擦 A 区 → 每秒发一次 `'C'` → 等 133 B 整帧 → 逐帧 ACK；EOT 后软复位 | `main.c` 的 `IAP_XMODEM_C_FLAG` |
| [5] 存外部 Flash | 选块号 `1`~`9` → 擦该 64 KB 块 → 同 [2] 收帧 → EOT 时写 `Firelen[块号] = 包数 × 128` | `boot.c` UPDATA_XMODEM 分支 |
| [6] 搬进 A 区 | 选块号 → 主循环按 `Firelen[]` 读 W25Q64 → 擦写 A 区 → 软复位 | `main.c` UPDATA_A_FLAG |
| Xmodem 帧 | `0x01` + seq + `~seq` + 128 B 数据 + CRC16（大端） | `boot.c` 168~198 行 |
| CRC16 | poly `0x1021`，init `0x0000`（= 标准 XMODEM CRC，已验证 `"123456789"` → `0x31C3`） | `Xmodem_CRC16()` |
| 应答 | `ACK=0x06`、`NAK=0x15`、`EOT=0x04` | 同上 |
| 串口 | 921600 8N1 无流控（改波特率要同时改 `usart.c` 并重烧） | `usart.c` |

## 四、已知的坑（工具已经处理，但你要知道为什么）

1. **5 秒窗口只认第一个字节**：先到的任何杂字节（Ctrl+V 的 `0x16`、自动回车、驱动 XON）都会顶掉 `w`。所以脚本先 `dtr = False`，再单独发 `w`，不发送任何其他字符。
2. **MCU 只接受「一次收到整 133 字节」**：它靠 IDLE 空闲中断划包，因此脚本整帧一次 `write`，帧间留 2 ms。逐字节发必然失败。
3. **末尾补齐字节**：最后不足 128 B 的帧用 `--pad`（默认 `0xFF`）补齐；`Firelen` 记的是「包数 × 128」，所以补齐字节会留在 Flash 里，用 `0xFF` 最接近擦除态。
4. **块号只有 1~9**：`[5]`/`[6]` 只接受 `'1'~'9'`，块 0 只能由"上电自动升级"分支使用。
5. **版本号 26 字符**，多一个少一个都报格式错误；中途发 ESC/回车可以退出。

## 五、下一步可以加（都写进简历的素材）

- [ ] **整包 CRC32 校验**：脚本已经打印 `CRC32`，MCU 端在 EOT 时算一遍对比，补上 README 里"没有整包完整性校验"的遗留项
- [ ] **一键升级**：`upgrade app.bin` = 进菜单 → [2] → 传完 → 读版本号 → 打印结果
- [ ] **图形界面**：Tkinter 包一层（串口下拉框 + 选文件 + 进度条 + 日志窗）
- [ ] **自动化测试**：`--script` 模式跑一串命令并断言输出，给 CI 用
- [ ] **断点续传**：MCU 端记录已收包数，掉线后从第 N 帧续传