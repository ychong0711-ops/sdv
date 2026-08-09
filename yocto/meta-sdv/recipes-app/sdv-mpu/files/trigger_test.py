#!/usr/bin/env python3
"""
테스트용 SOME/IP 트리거 - 카메라 없이 MCU 동작 검증
사용법: python3 trigger_test.py --level 85
Wireshark로 SOME/IP 패킷 확인 가능
"""
import argparse
import struct
import socket
import time

SOMEIP_MCU_IP = "127.0.0.1"  # UNO Q 내부 loopback (MPU<->MCU는 USB RNDIS, 테스트는 loopback)
SOMEIP_PORT = 30490
SERVICE_ID = 0x1234
EVENT_ID = 0x8001

def send(level):
    header = struct.pack('!HHIHHBBBB', SERVICE_ID, EVENT_ID, 8+1+8, 0x0001, 0x0001, 0x01, 0x01, 0x02, 0x00)
    payload = struct.pack('!B', level)
    packet = header + payload + b'\x00'*7
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.sendto(packet, (SOMEIP_MCU_IP, SOMEIP_PORT))
    print(f"[TEST] SOME/IP Notify sent: level={level} to {SOMEIP_MCU_IP}:{SOMEIP_PORT} ({len(packet)} bytes)")
    # Wireshark 필터: udp.port==30490 && someip
    sock.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--level", type=int, default=90, help="drowsiness 0-100")
    parser.add_argument("--loop", action="store_true", help="5초마다 반복")
    args = parser.parse_args()
    if args.loop:
        while True:
            send(args.level)
            time.sleep(5)
    else:
        send(args.level)
