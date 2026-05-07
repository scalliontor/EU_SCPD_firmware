#!/usr/bin/env python3
"""
EU-SCPD Unified Controller v2
──────────────────────────────────────────────────────
Điều khiển 6 dải LED WS2812B + Đọc 6 mic MAX4466
Hỗ trợ thay đổi số bóng LED mỗi dải tại runtime.

Cách dùng:
  python controller.py                                  # Tương tác
  python controller.py led 0 numleds 60                 # Dải 0 → 60 bóng
  python controller.py led all numleds 30               # Tất cả → 30 bóng
  python controller.py led 0 color 255 0 0              # Dải 0 → Đỏ
  python controller.py led all clear                    # Tắt hết
  python controller.py led 3 mode 1                     # Dải 3 → Cầu vồng
  python controller.py led 2 speed 50                   # Tốc độ 50ms
  python controller.py led all brightness 80            # Sáng 80/255
  python controller.py led 1 pixel 5 0 255 0            # Bóng thứ 5 → Xanh
  python controller.py mic monitor                      # VU Meter
  python controller.py mic rate 100                     # Mic mỗi 100ms
  python controller.py status                           # Trạng thái
"""

import serial
import serial.tools.list_ports
import time
import sys
import glob
import argparse
import threading

BAUD_RATE = 115200


# ════════════════════════════════════════════════════════════════
#  AUTO-DETECT PORT
# ════════════════════════════════════════════════════════════════

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
            print("✗ KHÔNG tìm thấy Arduino! Kiểm tra cáp USB.")
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


# ════════════════════════════════════════════════════════════════
#  SEND COMMAND
# ════════════════════════════════════════════════════════════════

def send_command(ser, cmd):
    """Gửi lệnh và đọc phản hồi (bỏ qua dòng MIC:)."""
    ser.write((cmd.strip() + '\n').encode())
    time.sleep(0.05)

    responses = []
    deadline = time.time() + 0.5
    while time.time() < deadline:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line and not line.startswith("MIC:"):
                responses.append(line)
                break  # Lấy response đầu tiên
        else:
            time.sleep(0.01)

    # Drain thêm nếu có nhiều response (VD: LED ALL)
    time.sleep(0.1)
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line and not line.startswith("MIC:"):
            responses.append(line)

    return responses


# ════════════════════════════════════════════════════════════════
#  MIC MONITOR
# ════════════════════════════════════════════════════════════════

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
                if line.startswith("MIC:"):
                    parts = line[4:].split(',')
                    if len(parts) >= 6:
                        sys.stdout.write(f'\033[8A')
                        print("─" * 60)
                        for i, val_str in enumerate(parts[:6]):
                            try:
                                v = int(val_str)
                                bar_len = min(v // 8, 45)
                                bar = "█" * bar_len
                                if v > 400:
                                    c = "\033[91m"
                                elif v > 200:
                                    c = "\033[93m"
                                else:
                                    c = "\033[92m"
                                print(f"  A{i} │ {v:04d} │ {c}{bar}\033[0m\033[K")
                            except ValueError:
                                print(f"  A{i} │ ERR  │\033[K")
                        print("─" * 60 + "\033[K")
                        sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n\033[0J  Đã dừng MIC Monitor.")


# ════════════════════════════════════════════════════════════════
#  INTERACTIVE MODE
# ════════════════════════════════════════════════════════════════

def interactive_mode(ser):
    """Chế độ tương tác."""
    print("=" * 62)
    print("  EU-SCPD Controller v2 — 6 LED Strips + 6 Mics")
    print("=" * 62)
    print()
    print("  LED <0-5|ALL> NUMLEDS <count>       Số bóng cho dải")
    print("  LED <0-5|ALL> COLOR <r> <g> <b>      Đặt màu")
    print("  LED <0-5|ALL> BRIGHTNESS <0-255>     Độ sáng")
    print("  LED <0-5|ALL> CLEAR                  Tắt dải")
    print("  LED <0-5|ALL> MODE <0|1|2>           0=tĩnh 1=cầu vồng 2=chase")
    print("  LED <0-5|ALL> SPEED <10-500>         Tốc độ hiệu ứng (ms)")
    print("  LED <0-5|ALL> PIXEL <i> <r> <g> <b>  Đặt 1 bóng")
    print("  MIC PAUSE / MIC RESUME")
    print("  MIC RATE <20-500>                    Chu kỳ mic (ms)")
    print("  STATUS")
    print("  mic       → VU Meter")
    print("  exit      → Thoát")
    print()

    stop_event = threading.Event()

    def reader_thread():
        while not stop_event.is_set():
            try:
                if ser.in_waiting > 0:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line and not line.startswith("MIC:"):
                        print(f"  ← {line}")
            except (OSError, serial.SerialException):
                break
            time.sleep(0.01)

    t = threading.Thread(target=reader_thread, daemon=True)
    t.start()

    while True:
        try:
            cmd = input("scpd> ").strip()
            if not cmd:
                continue
            if cmd.lower() in ['exit', 'quit', 'q']:
                break
            if cmd.lower() == 'mic':
                mic_monitor(ser)
                continue

            ser.write((cmd + '\n').encode())
            time.sleep(0.15)  # Đợi response

        except (KeyboardInterrupt, EOFError):
            print()
            break

    stop_event.set()


# ════════════════════════════════════════════════════════════════
#  MAIN
# ════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="EU-SCPD Controller v2: 6 LED + 6 MIC",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Ví dụ:
  %(prog)s                                  # Tương tác
  %(prog)s led 0 numleds 60                 # Dải 0 → 60 bóng
  %(prog)s led all numleds 30               # Tất cả → 30 bóng
  %(prog)s led 0 color 255 0 0              # Dải 0 → Đỏ
  %(prog)s led all clear                    # Tắt hết
  %(prog)s led 3 mode 1                     # Cầu vồng
  %(prog)s led 2 speed 50                   # Tốc độ 50ms
  %(prog)s mic monitor                      # VU Meter
  %(prog)s mic rate 100                     # Mic mỗi 100ms
  %(prog)s status                           # Trạng thái
        """
    )
    parser.add_argument("--port", type=str, default=None,
                        help="Cổng Serial (tự dò nếu bỏ trống)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Baud rate (mặc định: 115200)")
    parser.add_argument("args", nargs="*",
                        help="Lệnh (VD: led 0 numleds 60)")

    parsed = parser.parse_args()

    global BAUD_RATE
    BAUD_RATE = parsed.baud

    ser = open_serial(parsed.port)

    try:
        if not parsed.args:
            interactive_mode(ser)

        elif (parsed.args[0].lower() == "mic" and
              len(parsed.args) > 1 and parsed.args[1].lower() == "monitor"):
            mic_monitor(ser)

        elif (parsed.args[0].lower() == "mic" and
              len(parsed.args) > 1 and parsed.args[1].lower() == "rate"):
            cmd = " ".join(parsed.args).upper()
            for r in send_command(ser, cmd):
                print(f"  {r}")

        elif parsed.args[0].lower() == "status":
            for r in send_command(ser, "STATUS"):
                print(f"  {r}")

        elif parsed.args[0].lower() == "led":
            cmd = " ".join(parsed.args).upper()
            for r in send_command(ser, cmd):
                print(f"  {r}")

        else:
            cmd = " ".join(parsed.args)
            for r in send_command(ser, cmd):
                print(f"  {r}")

    except KeyboardInterrupt:
        print("\n  Đã dừng.")
    finally:
        ser.close()
        print("  Đã đóng Serial.")


if __name__ == '__main__':
    main()
