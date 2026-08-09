"""
SOME/IP 프로토콜 유닛 테스트 (pytest).

protocol.py의 와이어 포맷(length = payload + 8, 8바이트 정렬)과
MCU e2e_crc8과 동일한 crc8 구현을 검증한다.
"""
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from mpu.someip.protocol import (  # noqa: E402
    SERVICE_ID,
    EVENT_NOTIFY,
    EVENT_HEARTBEAT,
    build_notify,
    build_heartbeat,
    parse_event,
    crc8,
)


def _independent_crc8(data: bytes) -> int:
    """독립 구현 (다항식 0x1D, init 0xFF, final XOR 0xFF) — MCU e2e_crc8과 동일 규칙."""
    crc = 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x1D) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc ^ 0xFF


def test_notify_packet_length_field():
    packet = build_notify(85)
    # length 필드 = payload(1) + 8 = 9 (기존 17 오류 수정 검증)
    length = int.from_bytes(packet[4:8], "big")
    assert length == 9
    # 8바이트 정렬: 16 헤더 + 1 payload + 7 패딩 = 24
    assert len(packet) == 24


def test_header_fields():
    packet = build_notify(85)
    service_id = int.from_bytes(packet[0:2], "big")
    method_id = int.from_bytes(packet[2:4], "big")
    assert service_id == SERVICE_ID == 0x1234
    assert method_id == EVENT_NOTIFY == 0x8001
    assert packet[12] == 0x01  # protocol_version
    assert packet[13] == 0x01  # interface_version
    assert packet[14] == 0x02  # message_type: Notification
    assert packet[15] == 0x00  # return_code


def test_parse_roundtrip():
    packet = build_notify(85)
    event_id, payload = parse_event(packet)
    assert event_id == EVENT_NOTIFY
    assert payload == b"\x55"


def test_heartbeat_roundtrip():
    packet = build_heartbeat(7)
    event_id, payload = parse_event(packet)
    assert event_id == EVENT_HEARTBEAT
    assert payload == b"\x07"


def test_crc8_known_vector():
    data = b"\x55\xaa\x01\x55\x00\x00"
    assert crc8(data) == _independent_crc8(data)
