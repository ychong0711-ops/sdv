"""
SOME/IP-SD 유닛 테스트 (pytest) — 네트워크 I/O 전혀 없음.

sd.py의 빌더/파서 와이어 포맷과 SdServer 세션 상태 전이를 검증한다.
vsomeip 3.7.4 소스 및 AUTOSAR PRS SOME/IP-SD 스펙 기준으로 교차검증됨.
"""
import socket
import sys
from pathlib import Path

import pytest

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from mpu.someip.sd import (  # noqa: E402
    build_find_service,
    build_offer_service,
    build_stop_offer_service,
    build_subscribe_eventgroup,
    build_ipv4_endpoint_option,
    parse_sd,
    SdServer,
    SD_PORT,
    L4_UDP,
    OPT_IP4_ENDPOINT,
    ENTRY_FIND_SERVICE,
    ENTRY_OFFER_SERVICE,
    ENTRY_SUBSCRIBE_EVENTGROUP,
    TTL_DEFAULT,
    FLAG_UNICAST,
    FLAG_REBOOT,
)

SERVICE = 0x1234
INSTANCE = 0x5678
HOST = "10.0.2.15"
PORT = 30490


def test_find_service_header():
    packet = build_find_service(SERVICE, INSTANCE)
    assert packet[0:2] == b"\xff\xff"
    assert packet[2:4] == b"\x81\x00"
    assert packet[8:10] == b"\x00\x00"
    assert packet[12] == 0x01  # protocol_version
    assert packet[13] == 0x01  # interface_version
    assert packet[14] == 0x02  # message_type: Notification
    assert packet[15] == 0x00  # return_code
    assert packet[16] == FLAG_UNICAST  # flags@16 == 0x40
    assert int.from_bytes(packet[10:12], "big") == 0x0001  # session@10


def test_find_service_length():
    packet = build_find_service(SERVICE, INSTANCE)
    length = int.from_bytes(packet[4:8], "big")
    assert length == 36  # 20 + 16 (client..code 8B + SD 헤더 12B + entry 16B)
    # SOME/IP length는 Request ID부터 세므로 총 패킷 = 8(service+method+length) + length
    assert len(packet) == 8 + length == 44


def test_offer_service_length():
    packet = build_offer_service(SERVICE, INSTANCE, HOST, PORT)
    length = int.from_bytes(packet[4:8], "big")
    assert length == 48  # 20 + 16 + 12
    assert len(packet) == 8 + length == 56


def test_service_entry_layout():
    packet = build_find_service(SERVICE, INSTANCE)
    entry = packet[24:40]
    assert entry[0] == ENTRY_FIND_SERVICE
    assert int.from_bytes(entry[4:6], "big") == SERVICE
    assert int.from_bytes(entry[6:8], "big") == INSTANCE
    assert entry[8] == 1  # major_version
    assert entry[9:12] == b"\xff\xff\xff"  # ttl 3B
    assert int.from_bytes(entry[12:16], "big") == 0  # minor_version


def test_offer_entry_options():
    packet = build_offer_service(SERVICE, INSTANCE, HOST, PORT)
    entry = packet[24:40]
    assert entry[0] == ENTRY_OFFER_SERVICE
    assert entry[3] == 0x10  # #opts: n1=1, n2=0
    assert entry[1] == 0  # index1
    options_len = int.from_bytes(packet[40:44], "big")
    assert options_len == 12
    opt = packet[44:56]
    assert int.from_bytes(opt[0:2], "big") == 9
    assert opt[2] == OPT_IP4_ENDPOINT
    assert opt[9] == L4_UDP
    assert int.from_bytes(opt[10:12], "big") == PORT
    assert opt[4:8] == socket.inet_aton(HOST)


def test_ipv4_endpoint_option_layout():
    opt = build_ipv4_endpoint_option(HOST, PORT)
    assert int.from_bytes(opt[0:2], "big") == 9
    assert opt[2] == 0x04
    assert opt[3] == 0x00
    assert opt[4:8] == socket.inet_aton(HOST)
    assert opt[8] == 0x00
    assert opt[9] == 0x11
    assert int.from_bytes(opt[10:12], "big") == PORT


def test_stop_offer_ttl_zero():
    packet = build_stop_offer_service(SERVICE, INSTANCE)
    entry = packet[24:40]
    assert entry[0] == ENTRY_OFFER_SERVICE
    assert entry[9:12] == b"\x00\x00\x00"  # TTL=0
    assert int.from_bytes(packet[40:44], "big") == 0  # options_len


def test_parse_sd_roundtrip():
    packet = build_offer_service(SERVICE, INSTANCE, HOST, PORT, session_id=3)
    info = parse_sd(packet)
    assert info["session_id"] == 3
    assert info["flags"] == FLAG_UNICAST
    assert len(info["entries"]) == 1
    e = info["entries"][0]
    assert e["entry_type"] == ENTRY_OFFER_SERVICE
    assert e["service_id"] == SERVICE
    assert e["instance_id"] == INSTANCE
    assert e["major_version"] == 1
    assert e["ttl"] == TTL_DEFAULT
    assert e["minor_version"] == 0
    assert len(info["options"]) == 1
    o = info["options"][0]
    assert o["option_type"] == OPT_IP4_ENDPOINT
    assert o["address"] == HOST
    assert o["port"] == PORT
    assert o["l4_proto"] == L4_UDP


def test_parse_sd_subscribe():
    packet = build_subscribe_eventgroup(SERVICE, INSTANCE, 0x0001, "192.168.1.10", 30500)
    info = parse_sd(packet)
    assert len(info["entries"]) == 1
    e = info["entries"][0]
    assert e["entry_type"] == ENTRY_SUBSCRIBE_EVENTGROUP
    assert e["eventgroup_id"] == 0x0001
    assert e["service_id"] == SERVICE
    assert e["instance_id"] == INSTANCE
    assert e["major_version"] == 1
    assert info["options"][0]["port"] == 30500


def test_parse_sd_invalid():
    with pytest.raises(ValueError):
        parse_sd(b"\x00" * 5)


def test_server_session_increment(monkeypatch):
    # socket.sendto를 패치해 실제 네트워크 전송 무력화
    monkeypatch.setattr(socket.socket, "sendto", lambda *a, **k: len(a[1]))
    server = SdServer(SERVICE, INSTANCE, ip=HOST, port=PORT)
    assert server._first is True
    assert server._session == 1
    server.offer_once()
    assert server._session == 2  # 세션 증가
    assert server._first is False  # 첫 광고 이후 flag 전이
