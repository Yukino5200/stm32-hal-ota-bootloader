#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA 上位机（命令行版）—— 与 D:\\BootLoader\\OTA 的 BootLoader 协议对齐

MCU 侧实现见 Core/Src/boot.c、Core/Src/main.c：
  · 上电 5 秒窗口内收到小写 'w' 进命令行（只认 U1_RxBuff[0]，所以必须单独发 'w'）
  · 菜单单字符命令：1 擦A区 / 2 串口IAP / 3 设版本 / 4 查版本 /
                    5 下 W25Q64(块1~9) / 6 用 W25Q64 / 7 重启 / 8 清标志
  · Xmodem：133B/帧 = 0x01 + seq + ~seq + 128B 数据 + CRC16(大端)；
            CRC16 多项式 0x1021、初值 0x0000；ACK=0x06、NAK=0x15、EOT=0x04
  · MCU 靠 IDLE 空闲中断划包，只接受「一次收到整 133 字节」的帧，
    所以这里整帧一次 write，帧间留 2ms 空隙

用法：
    python ota_host.py -p COM9 menu
    python ota_host.py -p COM9 ver-get
    python ota_host.py -p COM9 ver-set VER-1.0.0-2026/10/18-20:00
    python ota_host.py -p COM9 iap app.bin
    python ota_host.py -p COM9 push 1 app.bin
    python ota_host.py -p COM9 use 1
    python ota_host.py -p COM9 erase | reboot | clear-flag

依赖：pip install pyserial
"""

import argparse
import os
import sys
import time
import zlib

try:
    import serial
except ImportError:
    sys.exit("缺少 pyserial：请先执行  pip install pyserial")

# 控制台是 GBK 时，个别字符（× … ）可能编不出来，别让它把脚本打断
try:
    sys.stdout.reconfigure(errors="replace")
except Exception:
    pass

SOH, EOT, ACK, NAK, CAN = 0x01, 0x04, 0x06, 0x15, 0x18
CRC_CHAR = ord("C")

FLASH_A_SIZE = 44 * 1024     # A 区 44KB
W25Q64_BLOCK = 64 * 1024     # 一块 64KB


def log(msg=""):
    print(msg, flush=True)


def crc16_xmodem(data: bytes) -> int:
    """与 boot.c 的 Xmodem_CRC16 完全一致：poly=0x1021, init=0x0000。"""
    crc = 0x0000
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


class Link:
    def __init__(self, port, baud, verbose=False, logfile=None):
        self.verbose = verbose
        self.buf = bytearray()
        self.fh = open(logfile, "a", encoding="utf-8") if logfile else None
        self.ser = serial.Serial(port, baud, timeout=0.05, write_timeout=2)
        # 不要用 DTR/RTS 复位板子：CH340 的 DTR 可能接 RST，一拉就打断 5 秒窗口
        try:
            self.ser.dtr = False
            self.ser.rts = False
        except Exception:
            pass
        time.sleep(0.2)

    def write(self, data: bytes):
        self.ser.write(data)
        self.ser.flush()
        if self.verbose:
            log(f">>> {data!r}")

    def read_some(self, timeout):
        end = time.time() + timeout
        got = b""
        while time.time() < end:
            chunk = self.ser.read(256)
            if chunk:
                got += chunk
                end = time.time() + 0.05
        if got:
            self.buf += got
            if self.fh:
                self.fh.write(got.decode("gbk", "replace"))
            if self.verbose:
                log(f"<<< {got!r}")
        return got

    def wait_for(self, targets, timeout):
        """等 targets 中任意一个出现；返回命中的那个，超时返回 None。"""
        if isinstance(targets, (bytes, int)):
            targets = [targets]
        end = time.time() + timeout
        while time.time() < end:
            for t in targets:
                needle = bytes([t]) if isinstance(t, int) else t
                if needle in self.buf:
                    return t
            self.read_some(0.1)
        return None

    def clear(self):
        self.buf.clear()
        try:
            self.ser.reset_input_buffer()
        except Exception:
            pass

    def text(self):
        return bytes(self.buf).decode("gbk", "replace")

    def close(self):
        try:
            self.ser.close()
        finally:
            if self.fh:
                self.fh.close()


def enter_menu(link, timeout=12.0):
    """反复发 'w'，直到 MCU 打印『进入BootLoader命令行』。"""
    log("[*] 等待 5 秒启动窗口…（现在给板子复位/上电）")
    link.clear()
    deadline = time.time() + timeout
    while time.time() < deadline:
        link.write(b"w")
        hit = link.wait_for(["进入BootLoader命令行", "跳转A分区"], 1.2)
        if hit == "进入BootLoader命令行":
            link.read_some(0.5)
            log(link.text().strip())
            link.clear()
            return True
        if hit == "跳转A分区":
            log("[!] 已过启动窗口（板子跳 A 区了）。复位后立刻重跑。")
            return False
    log("[!] 超时：没进命令行。检查串口是否被 SecureCRT 占用、波特率是否 921600。")
    return False


def xmodem_send(link, payload: bytes, pad=0xFF, retries=10):
    total = (len(payload) + 127) // 128
    log(f"[*] Xmodem 发送：{len(payload)}B / {total} 帧  "
        f"CRC32=0x{zlib.crc32(payload) & 0xFFFFFFFF:08X}")

    log("[*] 等 MCU 发 'C' …")
    if link.wait_for(CRC_CHAR, 30.0) is None:
        log("[!] 没等到 'C'：可能没进下载模式，或串口被占用。")
        return False
    link.clear()

    seq = 1
    for idx in range(total):
        chunk = payload[idx * 128:(idx + 1) * 128]
        if len(chunk) < 128:
            chunk += bytes([pad]) * (128 - len(chunk))
        frame = bytes([SOH, seq & 0xFF, (0xFF - seq) & 0xFF]) + chunk
        frame += crc16_xmodem(chunk).to_bytes(2, "big")

        for attempt in range(1, retries + 1):
            link.write(frame)                      # 整帧一次写出
            resp = link.wait_for([ACK, NAK], 3.0)
            if resp == ACK:
                break
            log(f"    {'NAK' if resp == NAK else '超时'}，重传第 {idx + 1} 帧"
                f"（第 {attempt} 次）")
            link.clear()
        else:
            log(f"[!] 第 {idx + 1} 帧重试 {retries} 次仍失败，中止。")
            link.write(bytes([CAN, CAN]))
            return False

        link.clear()
        if (idx + 1) % 8 == 0 or idx + 1 == total:
            log(f"    {idx + 1}/{total} 帧 ({(idx + 1) * 100.0 / total:5.1f}%)")
        seq = (seq + 1) & 0xFF
        time.sleep(0.002)                          # 帧间 2ms，防 USB 串口打散

    log("[*] 发 EOT …")
    link.write(bytes([EOT]))
    ok = link.wait_for([ACK], 5.0)
    link.read_some(1.0)
    tail = link.text().strip()
    if tail:
        log(tail)
    link.clear()
    if ok is None:
        log("[!] EOT 没收到 ACK，看板子输出确认。")
    return ok is not None


def _load_bin(path, limit, label):
    if not os.path.isfile(path):
        log(f"[!] 找不到文件：{path}")
        return None
    data = open(path, "rb").read()
    if not data:
        log("[!] 文件是空的")
        return None
    if len(data) > limit:
        log(f"[!] {len(data)}B 超过{label}上限 {limit}B")
        return None
    log(f"[*] 固件 {path}：{len(data)}B  CRC32=0x{zlib.crc32(data) & 0xFFFFFFFF:08X}")
    return data


# ---------------- 各命令 ----------------
def do_menu(a, link):
    link.read_some(0.5)
    log(link.text().strip())


def do_erase(a, link):
    link.clear()
    link.write(b"1")
    link.wait_for("擦除A区", 5.0)
    link.read_some(0.5)
    log(link.text().strip())
    link.clear()


def do_clear_flag(a, link):
    link.clear()
    link.write(b"8")
    link.wait_for("清除OTA标志", 3.0)
    link.read_some(0.5)
    log(link.text().strip())
    link.clear()


def do_reboot(a, link):
    link.clear()
    link.write(b"7")
    link.read_some(0.5)
    log(link.text().strip())


def do_ver_get(a, link):
    link.clear()
    link.write(b"4")
    link.wait_for("版本号", 3.0)
    link.read_some(0.5)
    log(link.text().strip())
    link.clear()


def do_ver_set(a, link):
    ver = a.ver
    if len(ver) != 26:
        log(f"[!] 版本号必须正好 26 字符，当前 {len(ver)}：{ver}")
        log("    格式 VER-x.y.z-YYYY/MM/DD-hh:mm  例 VER-1.0.0-2026/10/18-20:00")
        return
    link.clear()
    link.write(b"3")
    link.wait_for("设置版本号", 3.0)
    link.read_some(0.2)
    link.write(ver.encode())          # MCU 是流式攒满 26 字符，逐字符发也行
    hit = link.wait_for(["版本号正确", "版本号格式错误"], 5.0)
    link.read_some(0.5)
    log(link.text().strip())
    link.clear()
    log("[+] 版本号已写入 EEPROM" if hit == "版本号正确" else "[!] 版本号校验失败")


def do_iap(a, link):
    data = _load_bin(a.file, FLASH_A_SIZE, " A 区")
    if data is None:
        return
    link.clear()
    link.write(b"2")
    if link.wait_for("请使用.bin格式文件", 5.0) is None:
        log("[!] 没看到 [2] 的提示，可能不在空闲菜单（先发 ESC/回车回菜单）")
        return
    link.read_some(0.3)
    log(link.text().strip())
    link.clear()
    if xmodem_send(link, data, pad=int(a.pad, 0)):
        log("[+] 串口 IAP 完成，板子会自动软复位并跳新 App。")


def do_push(a, link):
    if not 1 <= a.block <= 9:
        log("[!] 块号只能是 1~9（boot.c 的 [5] 只收 '1'~'9'）")
        return
    data = _load_bin(a.file, W25Q64_BLOCK, f"第 {a.block} 块")
    if data is None:
        return
    link.clear()
    link.write(b"5")
    if link.wait_for("输入需要使用的块编号", 3.0) is None:
        log("[!] 没进入 [5] 选块模式")
        return
    link.read_some(0.2)
    link.write(str(a.block).encode())
    if link.wait_for("向W25Q64发送第", 5.0) is None:
        log("[!] 块号没被接受：")
        link.read_some(0.3)
        log(link.text().strip())
        return
    link.read_some(0.3)
    log(link.text().strip())
    link.clear()
    if xmodem_send(link, data, pad=int(a.pad, 0)):
        log(f"[+] 已下载到 W25Q64 第 {a.block} 块；再执行  use {a.block}  就会搬到 A 区。")


def do_use(a, link):
    if not 1 <= a.block <= 9:
        log("[!] 块号只能是 1~9")
        return
    link.clear()
    link.write(b"6")
    if link.wait_for("输入需要使用的块编号", 3.0) is None:
        log("[!] 没进入 [6] 选块模式")
        return
    link.read_some(0.2)
    link.write(str(a.block).encode())
    hit = link.wait_for(["长度", "编号错误", "数据长度错误"], 8.0)
    link.read_some(1.5)
    log(link.text().strip())
    link.clear()
    if hit == "长度":
        log("[+] 已开始从 W25Q64 搬到 A 区（看到『A区升级完成』即成功，随后自动复位）。")


HANDLERS = {
    "menu": do_menu, "erase": do_erase, "clear-flag": do_clear_flag,
    "reboot": do_reboot, "ver-get": do_ver_get, "ver-set": do_ver_set,
    "iap": do_iap, "push": do_push, "use": do_use,
}


def build_parser():
    ap = argparse.ArgumentParser(
        description="STM32 OTA BootLoader 上位机（命令行）",
        epilog="示例：python ota_host.py -p COM9 iap app.bin")
    ap.add_argument("-p", "--port", required=True, help="串口号，如 COM9")
    ap.add_argument("-b", "--baud", type=int, default=921600,
                    help="波特率，默认 921600（必须与 usart.c 一致）")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印原始收发字节")
    ap.add_argument("--log", help="把板子输出另存为文件（调试用）")
    ap.add_argument("--pad", default="0xFF", help="末帧补齐字节，默认 0xFF")
    ap.add_argument("--no-enter", action="store_true", help="不自动发 w（已在菜单里时用）")

    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("menu", help="进命令行并打印菜单")
    sub.add_parser("erase", help="[1] 擦除 A 区")
    sub.add_parser("clear-flag", help="[8] 清除 OTA 标志")
    sub.add_parser("reboot", help="[7] 软复位")
    sub.add_parser("ver-get", help="[4] 查询版本号")

    p = sub.add_parser("ver-set", help="[3] 设置版本号")
    p.add_argument("ver", help="26 字符版本串，如 VER-1.0.0-2026/10/18-20:00")

    p = sub.add_parser("iap", help="[2] 串口 Xmodem 直刷 A 区")
    p.add_argument("file", help=".bin 文件路径")

    p = sub.add_parser("push", help="[5] 下载固件到 W25Q64 某块")
    p.add_argument("block", type=int, help="块号 1~9")
    p.add_argument("file", help=".bin 文件路径")

    p = sub.add_parser("use", help="[6] 用 W25Q64 某块升级 A 区")
    p.add_argument("block", type=int, help="块号 1~9")
    return ap


def main():
    a = build_parser().parse_args()
    link = Link(a.port, a.baud, verbose=a.verbose, logfile=a.log)
    try:
        if not a.no_enter and not enter_menu(link):
            return 1
        HANDLERS[a.cmd](a, link)
        return 0
    except KeyboardInterrupt:
        log("\n[!] 手动中止")
        return 130
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main())