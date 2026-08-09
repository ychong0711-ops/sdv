#!/usr/bin/env python3
"""
데모 산출물 생성 스크립트: pcap + candump 로그.

wireshark/someip_drowsiness.pcap 생성 (실제 SOME/IP 패킷 + Ethernet/IP/UDP 헤더 합성)
docs/candump_sim.log 생성 (MCU CAN TX 로직 미러링)

사용법:
  python scripts/generate_demos.py
"""
import struct
import time
from pathlib import Path
import os
import sys

_REPO_ROOT = Path(__file__).resolve().parents[1]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from mpu.someip.protocol import (
    build_notify,
    build_heartbeat,
    crc8,
)

# ===========================================================
# 1. pcap 생성 (Classic pcap format, LINKTYPE_RAW = 101)
# ===========================================================

def make_udp_payload(packet: bytes, src_ip: str, dst_ip: str, src_port: int, dst_port: int) -> bytes:
    """SOME/IP UDP 데이터그램에 Ethernet/IPv4/UDP 헤더를 합성하여 pcap 프레임 생성."""
    # Ethernet (14 bytes)
    eth = b'\x00' * 6 + b'\x00' * 6 + struct.pack('!H', 0x0800)

    # IPv4 header (20 bytes, no options)
    ip_total = 20 + 8 + len(packet)
    src = bytes(int(x) for x in src_ip.split('.'))
    dst = bytes(int(x) for x in dst_ip.split('.'))
    ip = struct.pack('!BBHHHBBH4s4s',
        0x45,           # version=4, ihl=5
        0,              # DSCP/ECN
        ip_total,       # total length
        0, 0,           # ID, flags/frag
        64,             # TTL
        17,             # protocol: UDP
        0,              # checksum (0 = not computed, acceptable for capture)
        src, dst)

    # UDP header (8 bytes)
    udp_len = 8 + len(packet)
    udp = struct.pack('!HHHH', src_port, dst_port, udp_len, 0)  # checksum=0 OK

    return eth + ip + udp + packet


def write_pcap(frames: list[bytes], path: str):
    """Classic pcap 파일 작성."""
    with open(path, 'wb') as f:
        # Global header (24 bytes)
        f.write(struct.pack('<IHHiIII',
            0xa1b2c3d4,  # magic
            2, 4,         # version
            0,            # thiszone
            0,            # sigfigs
            65535,        # snaplen
            101))         # LINKTYPE_RAW

        now = time.time()
        for i, frame in enumerate(frames):
            ts = now + i * 0.05  # 50ms 간격
            sec = int(ts)
            usec = int((ts - sec) * 1_000_000)
            f.write(struct.pack('<IIII',
                sec, usec,
                len(frame), len(frame)))
            f.write(frame)


# ===========================================================
# 2. CAN TX 시뮬레이션 로그 (candump format)
# ===========================================================

def sim_can_frame(level: int, counter: int) -> bytes:
    """MCU can_send_frame 로직 미러링: [55 AA 01 level 00 00 CRC counter]"""
    data = bytearray([0x55, 0xAA, 0x01, level & 0xFF, 0x00, 0x00, 0x00, counter & 0xFF])
    data[6] = crc8(bytes(data[:6]))
    return bytes(data)


def generate_candump_log(path: str, count: int = 30):
    """candump 포맷 로그 생성."""
    lines = []
    now = time.time()
    counter = 0
    for i in range(count):
        ts = now + i * 0.05
        # 시나리오: heartbeat 20개, 중간에 졸음 Notify 10개
        if i < 20:
            level = 0x00
        else:
            level = min(50 + (i - 20) * 5, 100)
        frame = sim_can_frame(level, counter)
        counter += 1
        data_str = ' '.join(f'{b:02X}' for b in frame)
        sec = int(ts)
        usec = int((ts - sec) * 1_000_000)
        lines.append(f'({ts:12.6f})  can0  18FF01F4   [{len(frame)}] {data_str}')

    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


# ===========================================================
# 3. 메인: 산출물 생성
# ===========================================================

def main():
    base = _REPO_ROOT

    # --- pcap ---
    pcap_path = base / 'wireshark' / 'someip_drowsiness.pcap'
    frames = []
    counter = 0
    for i in range(40):
        if i < 25:
            # heartbeat 50ms
            pkt = build_heartbeat(counter)
        else:
            # 졸음 Notify
            level = min(50 + (i - 25) * 10, 100)
            pkt = build_notify(level)
        frame = make_udp_payload(pkt, '192.168.7.1', '192.168.7.2', 30490, 30490)
        frames.append(frame)
        counter = (counter + 1) & 0xFF

    pcap_path.parent.mkdir(parents=True, exist_ok=True)
    write_pcap(frames, str(pcap_path))
    print(f'[OK] pcap: {pcap_path} ({len(frames)} frames, {pcap_path.stat().st_size} bytes)')

    # --- candump ---
    candump_path = base / 'docs' / 'candump_sim.log'
    candump_path.parent.mkdir(parents=True, exist_ok=True)
    generate_candump_log(str(candump_path))
    print(f'[OK] candump: {candump_path}')


if __name__ == '__main__':
    main()
