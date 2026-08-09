"""
SOME/IP wire format (vsomeip 호환).
vsomeip 라이브러리 미사용, 경량 raw-socket 구현 (포트폴리오 목적).

와이어 포맷 (빅엔디안, 16바이트 헤더):
  service_id u16 | method_id u16 | length u32 | client_id u16 | session_id u16 |
  protocol_version u8 | interface_version u8 | message_type u8 | return_code u8
  length = payload 길이 + 8   (1바이트 payload면 length=9)
  UDP 데이터그램은 8바이트 정렬: 16헤더 + 1payload = 17 -> +7 패딩 = 총 24 (패딩은 length에 미포함)
"""
import struct
import time
from typing import Optional

SERVICE_ID = 0x1234
INSTANCE_ID = 0x5678
EVENT_NOTIFY = 0x8001
EVENT_HEARTBEAT = 0x8002
SOMEIP_PORT = 30490


def crc8(data: bytes) -> int:
    """E2E CRC8 (MCU e2e_crc8과 동일): 다항식 0x1D, init 0xFF, final XOR 0xFF."""
    crc = 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x1D) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc ^ 0xFF


def build_packet(event_id: int, payload: bytes, session_id: int = 1) -> bytes:
    """
    SOME/IP Notification 패킷 빌드 (빅엔디안).

    length = len(payload) + 8 (패딩 미포함), session_id 증가,
    UDP 데이터그램 8바이트 정렬 패딩 추가.
    """
    length = len(payload) + 8
    header = struct.pack(
        "!HHIHHBBBB",
        SERVICE_ID,      # service_id
        event_id,        # method_id (Event ID)
        length,          # length = payload + 8
        0x0001,          # client_id
        session_id,      # session_id (호출자가 증가시킴)
        0x01,            # protocol_version
        0x01,            # interface_version
        0x02,            # message_type: Notification
        0x00,            # return_code
    )
    packet = header + payload
    # UDP 데이터그램 8바이트 정렬 (패딩은 length 필드에 미포함)
    pad = (8 - (len(packet) % 8)) % 8
    if pad:
        packet += b"\x00" * pad
    return packet


def build_notify(level: int, session_id: int = 1) -> bytes:
    """졸음 이벤트 패킷: EVENT_NOTIFY(0x8001), payload 1바이트 (level 0-100)."""
    return build_packet(EVENT_NOTIFY, bytes([level & 0xFF]), session_id)


def build_heartbeat(counter: int, session_id: int = 1) -> bytes:
    """heartbeat 패킷: EVENT_HEARTBEAT(0x8002), payload 1바이트 (0-255 증가 카운터)."""
    return build_packet(EVENT_HEARTBEAT, bytes([counter & 0xFF]), session_id)


def parse_event(packet: bytes) -> tuple:
    """SOME/IP 패킷 파싱 -> (event_id, payload). 테스트/도구용. 무효 시 ValueError."""
    if len(packet) < 16:
        raise ValueError(f"packet too short: {len(packet)} bytes")
    (service_id, event_id, length, client_id, session_id,
     protocol_version, interface_version, message_type, return_code) = \
        struct.unpack("!HHIHHBBBB", packet[:16])
    if service_id != SERVICE_ID:
        raise ValueError(f"unexpected service_id 0x{service_id:04X}")
    if message_type != 0x02:
        raise ValueError(f"unexpected message_type {message_type:#x}")
    payload_len = length - 8
    if payload_len < 0 or payload_len > len(packet) - 16:
        raise ValueError(f"length field {length} inconsistent with packet size")
    payload = packet[16:16 + payload_len]
    return (event_id, payload)


class StreamParser:
    """UART 바이트 스트림에서 SOME/IP 프레임 재조립/동기화.

    MCU 측 `someip_uart_receive()` RX 상태 머신과 동일한 로직:
    1) 16바이트 헤더 확보 -> 2) header.length 필드로 payload 길이 계산
       (전체 프레임 = 16 + (length - 8)) -> 3) 완전 프레임 수신 시 파싱/반환.
    불완전 프레임 상태로 `timeout_ms`(기본 50ms) 이상 데이터가 없으면
    버퍼를 리셋한다 (스트림 재동기).

    전송 측은 항상 8바이트 정렬 완전 프레임을 1회 write 로 보내므로,
    프레임 수신 완료 시 버퍼를 리셋한다 (패딩 포함 잔여 바이트 폐기) -
    MCU 측 동작과 동일.
    """

    HEADER_SIZE = 16
    DEFAULT_TIMEOUT_MS = 50

    def __init__(self, timeout_ms: int = DEFAULT_TIMEOUT_MS):
        self.timeout_ms = timeout_ms
        self._buf = bytearray()
        self._last_tick: Optional[float] = None

    def reset(self):
        """수신 버퍼 리셋 (스트림 재동기)."""
        self._buf.clear()
        self._last_tick = None

    def feed(self, byte: int) -> Optional[tuple]:
        """단일 바이트 투입.

        완전 프레임 도착 시 (event_id, payload) 반환, 아니면 None.
        유효하지 않은 프레임은 스킵하고 버퍼를 리셋한다.
        """
        now = time.monotonic()
        # 스트림 재동기: 불완전 프레임이 timeout_ms 이상 데이터 없으면 리셋
        if self._buf and self._last_tick is not None and \
                (now - self._last_tick) > (self.timeout_ms / 1000.0):
            self.reset()

        self._buf.append(byte & 0xFF)
        self._last_tick = time.monotonic()

        if len(self._buf) < StreamParser.HEADER_SIZE:
            return None

        # 헤더 확보 후 length 필드 sanity (payload 1바이트면 9)
        length = int.from_bytes(self._buf[4:8], "big")
        if length < 8 or length > 32:
            self.reset()
            return None

        total = StreamParser.HEADER_SIZE + (length - 8)
        if len(self._buf) < total:
            return None  # 완전 프레임 대기

        frame = bytes(self._buf[:total])
        self.reset()
        try:
            return parse_event(frame)
        except ValueError:
            return None  # 유효하지 않은 프레임은 스킵

    def feed_bytes(self, data: bytes) -> list:
        """여러 바이트 일괄 투입 -> 완성된 (event_id, payload) 목록 반환."""
        events = []
        for b in data:
            ev = self.feed(b)
            if ev is not None:
                events.append(ev)
        return events
