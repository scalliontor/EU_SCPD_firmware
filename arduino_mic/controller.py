#!/usr/bin/env python3
"""
EU-SCPD MIC Controller
──────────────────────────────────────────────────────
Đọc 6 mic MAX4466 từ Arduino Mega 2560.

Cách dùng:
  python controller.py                  # VU Meter (mặc định)
  python controller.py monitor          # VU Meter
  python controller.py status           # Trạng thái
  python controller.py rate 100         # Mic mỗi 100ms
  python controller.py pause            # Tạm dừng
  python controller.py resume           # Tiếp tục
"""

import serial
import serial.tools.list_ports
import time
import sys
import glob
import argparse

BAUD_RATE = 115200


def find_arduino_port():
    """Tự động dò tìm cổng Arduino."""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = (port.description or "").lower()
        mfr = (port.manufacturer or "").lower()
        if ("arduino" in desc or "arduino" in mfr or
            "ch340" in desc or "2341" in (port.vid and f"{port.vid:04x}" or "")):
            print(f"  → Tìm thấy: {port.device} ({port.description})")
            return port.device

    candidates = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    if candidates:
        print(f"  → Dùng cổng: {candidates[0]}")
        return candidates[0]
    return None


def open_serial(port=None):
    """Mở kết nối Serial."""
    if port is None:
        print("Đang dò tìm Arduino...")
        port = find_arduino_port()
        if port is None:
            print("✗ KHÔNG tìm thấy Arduino!")
            sys.exit(1)

    print(f"Kết nối {port} @ {BAUD_RATE} baud...")
    ser = serial.Serial(port, BAUD_RATE, timeout=1)
    time.sleep(2)

    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            print(f"  Arduino: {line}")

    print("✓ Kết nối thành công!\n")
    return ser


def send_command(ser, cmd):
    """Gửi lệnh và đọc phản hồi."""
    ser.write((cmd.strip() + '\n').encode())
    time.sleep(0.1)

    responses = []
    deadline = time.time() + 0.5
    while time.time() < deadline:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line and not line[0].isdigit():
                responses.append(line)
                break
        time.sleep(0.01)
    return responses


def mic_monitor(ser):
    """VU Meter realtime 6 kênh."""
    print("=" * 60)
    print("  🎤 MIC MONITOR - 6 Channels (Ctrl+C để dừng)")
    print("=" * 60)
    print()

    for _ in range(10):
        print()

    try:
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if not line:
                    continue

                parts = line.split(',')
                if len(parts) >= 6 and parts[0].isdigit():
                    sys.stdout.write('\033[8A')
                    print("─" * 60)
                    for i, val_str in enumerate(parts[:6]):
                        try:
                            v = int(val_str)
                            bar_len = min(v // 8, 45)
                            bar = "█" * bar_len
                            if v > 400:
                                c = "\033[91m"    # Đỏ
                            elif v > 200:
                                c = "\033[93m"    # Vàng
                            else:
                                c = "\033[92m"    # Xanh
                            print(f"  A{i} │ {v:04d} │ {c}{bar}\033[0m\033[K")
                        except ValueError:
                            print(f"  A{i} │ ERR  │\033[K")
                    print("─" * 60 + "\033[K")
                    sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n\033[0J  Đã dừng MIC Monitor.")


def main():
    parser = argparse.ArgumentParser(
        description="EU-SCPD MIC Controller: 6x MAX4466",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Ví dụ:
  %(prog)s                   # VU Meter
  %(prog)s monitor           # VU Meter
  %(prog)s status            # Trạng thái
  %(prog)s rate 100          # Mic mỗi 100ms
  %(prog)s pause             # Tạm dừng
  %(prog)s resume            # Tiếp tục
        """
    )
    parser.add_argument("--port", type=str, default=None,
                        help="Cổng Serial (tự dò nếu bỏ trống)")
    parser.add_argument("command", nargs="?", default="monitor",
                        help="Lệnh: monitor, status, pause, resume, rate")
    parser.add_argument("value", nargs="?", default=None,
                        help="Giá trị (VD: rate 100)")

    args = parser.parse_args()
    ser = open_serial(args.port)

    try:
        if args.command in ("monitor", "mic"):
            mic_monitor(ser)

        elif args.command == "status":
            for r in send_command(ser, "STATUS"):
                print(f"  {r}")

        elif args.command == "pause":
            for r in send_command(ser, "MIC PAUSE"):
                print(f"  {r}")

        elif args.command == "resume":
            for r in send_command(ser, "MIC RESUME"):
                print(f"  {r}")

        elif args.command == "rate" and args.value:
            for r in send_command(ser, f"MIC RATE {args.value}"):
                print(f"  {r}")

        else:
            parser.print_help()

    except KeyboardInterrupt:
        print("\n  Đã dừng.")
    finally:
        ser.close()
        print("  Đã đóng Serial.")


if __name__ == '__main__':
    main()
