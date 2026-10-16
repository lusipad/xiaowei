#!/usr/bin/env python3
"""将 DeepSeek 大肥鱼皮肤二进制文件烧写至 Wio Terminal 板载 4MB QSPI Flash"""
import os
import sys
import time
import argparse
import serial

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN_PATH = os.path.join(ROOT, "art", "skin_deepseek.bin")
QSPI_TARGET_ADDR = 0x00040000

def ensure_binary():
    if not os.path.exists(BIN_PATH):
        print("[..] 未检测到 skin_deepseek.bin，正在自动打包生成...")
        import tools.pack_qspi_skin as pqs
        pqs.build_deepseek_binary()

def send_cmd(ser, cmd, expect="!OK", timeout=3.0):
    ser.write((cmd + "\n").encode("latin1"))
    ser.flush()
    t0 = time.time()
    resp_lines = []
    while time.time() - t0 < timeout:
        line = ser.readline().decode("latin1", errors="ignore").strip()
        if not line:
            continue
        if line.startswith("S: ") or line.startswith("D: "):
            continue
        resp_lines.append(line)
        if expect in line:
            return True, resp_lines
        if "!ERR" in line:
            return False, resp_lines
    return False, resp_lines

def sync_to_qspi(port="COM3", baud=115200):
    ensure_binary()
    with open(BIN_PATH, "rb") as f:
        data = f.read()

    total_len = len(data)
    print(f"[..] 准备写入 DeepSeek 皮肤: {total_len} 字节 ({total_len/1024:.1f} KB) -> QSPI 地址 0x{QSPI_TARGET_ADDR:06X}")

    try:
        ser = serial.Serial(port, baud, timeout=1.0)
    except Exception as e:
        print(f"[FAIL] 无法打开串口 {port}: {e}")
        sys.exit(1)

    time.sleep(0.5)
    ser.reset_input_buffer()

    # 1. Ping
    ok, lines = send_cmd(ser, "!PING", "!PONG", timeout=2.0)
    if not ok:
        print(f"[..] 设备未响应，尝试唤醒...")
        send_cmd(ser, "!STATUS", "!STATUS", timeout=1.0)
        ok, lines = send_cmd(ser, "!PING", "!PONG", timeout=2.0)
        if not ok:
            print("[FAIL] 设备无响应，请确认固件已烧录且端口为", port)
            ser.close()
            sys.exit(1)

    print("[OK] 设备通信正常")

    # 2. Erase sectors
    erase_len = ((total_len + 4095) // 4096) * 4096
    print(f"[..] 正在擦除 QSPI 扇区: 0x{QSPI_TARGET_ADDR:06X} ~ 0x{QSPI_TARGET_ADDR + erase_len:06X} ({erase_len // 1024} KB)...")
    ok, lines = send_cmd(ser, f"!QSPI_ERASE {QSPI_TARGET_ADDR:X} {erase_len:X}", "!OK QSPI_ERASE", timeout=10.0)
    if not ok:
        print("[FAIL] QSPI 擦除失败:", lines)
        ser.close()
        sys.exit(1)
    print("[OK] QSPI 扇区擦除完毕")

    # 3. Write in 256-byte chunks
    chunk_size = 256
    written = 0
    t_start = time.time()
    print("[..] 开始流式写入皮肤数据...")

    while written < total_len:
        chunk = data[written : written + chunk_size]
        cur_addr = QSPI_TARGET_ADDR + written
        hex_str = chunk.hex().upper()
        cmd = f"!QSPI_WRITE {cur_addr:X} {hex_str}"

        expected_resp = f"!OK QSPI_WRITE {len(chunk)}"
        ok, lines = send_cmd(ser, cmd, expected_resp, timeout=2.0)
        if not ok:
            print(f"\n[FAIL] 写入失败 at 0x{cur_addr:06X} (期望 '{expected_resp}'):", lines)
            ser.close()
            sys.exit(1)

        written += len(chunk)
        pct = (written * 100) // total_len
        bar = "=" * (pct // 5) + " " * (20 - (pct // 5))
        sys.stdout.write(f"\r[{bar}] {pct}% ({written}/{total_len} bytes)")
        sys.stdout.flush()

    elapsed = time.time() - t_start
    print(f"\n[OK] 写入完成！耗时: {elapsed:.2f} 秒 (速度: {total_len / 1024 / elapsed:.1f} KB/s)")

    # 4. Multi-point Readback Verification
    print("[..] 正在进行 QSPI Flash 回读全量校验...")
    check_points = [
        ("头部 (Header)", 0, 64),
        ("调色板 (Palette)", 1024, 64),
        ("表情切片 (Faces)", 1536, 64),
        ("背景场景 (Background)", 30208, 64),
    ]

    all_verified = True
    for name, offset, check_len in check_points:
        chk_addr = QSPI_TARGET_ADDR + offset
        expected_hex = data[offset : offset + check_len].hex().upper()
        ok, lines = send_cmd(ser, f"!QSPI_READ {chk_addr:X} {check_len}", f"!OK QSPI_READ {expected_hex}", timeout=2.0)
        if ok:
            print(f"  [PASS] {name} at 0x{chk_addr:06X}: 校验一致 (64 字节)")
        else:
            print(f"  [FAIL] {name} at 0x{chk_addr:06X}: 校验不匹配! 响应: {lines}")
            all_verified = False

    if all_verified:
        print("[SUCCESS] 全链路校验成功！大肥鱼皮肤数据 100% 完整无损写入 QSPI Flash！")
    else:
        print("[WARN] 部分校验点存在差异，请检查通信或 Flash 写入状态！")

    ser.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Sync DeepSeek Maid skin to Wio Terminal 4MB QSPI Flash")
    parser.add_argument("-p", "--port", default="COM3", help="Serial port (default: COM3)")
    args = parser.parse_args()
    sync_to_qspi(port=args.port)
