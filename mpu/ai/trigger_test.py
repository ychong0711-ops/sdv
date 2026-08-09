#!/usr/bin/env python3
"""
테스트용 SOME/IP 트리거 - 카메라 없이 MCU 동작 검증
사용법:
  python trigger_test.py --level 85            # 실보드 UART (기본)
  python trigger_test.py --transport udp       # PC 시뮬레이션 (UDP 127.0.0.1:30490)
  python trigger_test.py --loop                # 5초마다 notify 반복
  python trigger_test.py --heartbeat           # 50ms heartbeat 루프 (데모/검증용)
  python trigger_test.py --transport uart --uart-port /dev/ttyS1
Wireshark로 SOME/IP 패킷 확인 가능 (udp.port==30490, UDP 모드)
"""
import argparse
import os
import sys
import time

# mpu가 최상위 패키지가 아니므로 repo root를 sys.path에 삽입
_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from mpu.someip import client as someip_client  # noqa: E402
from mpu.someip.protocol import (  # noqa: E402
    EVENT_HEARTBEAT,
    EVENT_NOTIFY,
    SOMEIP_PORT,
    build_heartbeat,
    build_packet,
)


def dump_hex(packet: bytes) -> str:
    return " ".join(f"{b:02X}" for b in packet)


def send(level: int, transport) -> None:
    """notify 패킷 전송 (protocol.build_packet 사용, 기존 struct.pack 직접 구성 제거)."""
    packet = build_packet(EVENT_NOTIFY, bytes([level & 0xFF]))
    transport.send(packet)
    print(f"[TEST] SOME/IP Notify sent: level={level} ({len(packet)} bytes)")
    print(f"       hex: {dump_hex(packet)}")
    print(f"       Wireshark filter: udp.port=={SOMEIP_PORT} && someip")


def main():
    parser = argparse.ArgumentParser(description="SOME/IP 트리거 테스트")
    parser.add_argument("--level", type=int, default=90, help="drowsiness 0-100")
    parser.add_argument("--transport",
                        default=os.environ.get("SOMEIP_TRANSPORT", "uart"),
                        choices=["udp", "uart"],
                        help="전송 방식: uart(실보드 기본) / udp(PC 시뮬레이션). env SOMEIP_TRANSPORT")
    parser.add_argument("--dest", default="127.0.0.1",
                        help="MCU SOME/IP 대상 IP (UDP 시뮬레이션 전용)")
    parser.add_argument("--uart-port",
                        default=os.environ.get("SOMEIP_UART_PORT", "/dev/ttyS0"),
                        help="uart transport 전용 포트 (기본 /dev/ttyS0). env SOMEIP_UART_PORT")
    parser.add_argument("--loop", action="store_true", help="5초마다 notify 반복")
    parser.add_argument("--heartbeat", action="store_true",
                        help="50ms heartbeat 루프 (데모/검증용, Ctrl+C 중단)")
    args = parser.parse_args()

    transport = someip_client.create_transport(
        args.transport, host=args.dest, port=SOMEIP_PORT, uart_port=args.uart_port)

    if args.heartbeat:
        print(f"[TEST] heartbeat loop (50ms, EVENT 0x{EVENT_HEARTBEAT:04X}) - Ctrl+C to stop")
        counter = 0
        try:
            while True:
                packet = build_heartbeat(counter)
                transport.send(packet)
                print(f"[TEST] Heartbeat: counter={counter} ({len(packet)} bytes) "
                      f"hex: {dump_hex(packet)}")
                counter = (counter + 1) & 0xFF
                time.sleep(0.05)
        except KeyboardInterrupt:
            print("[TEST] stopped")
        finally:
            transport.close()
        return

    try:
        if args.loop:
            while True:
                send(args.level, transport)
                time.sleep(5)
        else:
            send(args.level, transport)
    finally:
        transport.close()


if __name__ == "__main__":
    main()
