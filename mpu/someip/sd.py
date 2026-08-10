"""
SOME/IP Service Discovery (SD) — vsomeip 호환 와이어 포맷.
vsomeip 라이브러리 미사용, 경량 raw-socket 구현 (포트폴리오 목적).

와이어 포맷 (빅엔디안):
  SOME/IP 헤더 16B: FF FF | 81 00 | length u32 | 00 00(client) | session u16 |
                    01(proto) | 01(iface) | 02(type=Notification) | 00(code)
  SD 헤더 12B:     flags u8 | reserved 3B(0x000000) | entries_len u32
  entries...
  options_len u32
  options...
  length 필드 = 20 + len(entries) + len(options)
  (20 = client..code 8B + SD 헤더 12B 고정분, 패딩 없음 - vsomeip 길이 규약 그대로)

AUTOSAR PRS SOME/IP-SD 및 vsomeip 3.7.4 소스로 교차검증됨.
"""
import socket
import struct
import threading
import time

SD_SERVICE_ID = 0xFFFF
SD_METHOD_ID = 0x8100
SD_CLIENT_ID = 0x0000
SD_PORT = 30490            # VSOMEIP_SD_DEFAULT_PORT
SD_MULTICAST = "224.224.224.245"
SD_PROTOCOL_VERSION = 0x01
SD_INTERFACE_VERSION = 0x01
SD_MESSAGE_TYPE = 0x02     # MT_NOTIFICATION (SD 메시지 전부 이 값)
SD_RETURN_CODE = 0x00
FLAG_UNICAST = 0x40
FLAG_REBOOT = 0x80
TTL_DEFAULT = 0xFFFFFF     # "until next reboot"
TTL_STOP = 0x000000
L4_UDP = 0x11
L4_TCP = 0x06

# Entry type
ENTRY_FIND_SERVICE = 0x00
ENTRY_OFFER_SERVICE = 0x01   # TTL=0이면 StopOffer
ENTRY_REQUEST_SERVICE = 0x02
ENTRY_FIND_EVENT_GROUP = 0x04
ENTRY_PUBLISH_EVENTGROUP = 0x05
ENTRY_SUBSCRIBE_EVENTGROUP = 0x06  # TTL=0이면 StopSubscribe
ENTRY_SUBSCRIBE_EVENTGROUP_ACK = 0x07

# Option type
OPT_IP4_ENDPOINT = 0x04
OPT_IP4_MULTICAST = 0x14


def _pack_ttl(ttl: int) -> bytes:
    """TTL을 정확히 3바이트로 패킹 (앞 0x00 바이트 없음)."""
    return struct.pack("!I", ttl & 0xFFFFFF)[1:]


def build_service_entry(entry_type, service_id, instance_id, ttl,
                        minor_version=0, major_version=1,
                        index1=0, index2=0, n_opts_run1=0, n_opts_run2=0) -> bytes:
    """Service 엔트리 16바이트 빌드 (빅엔디안).

    type u8 | index1 u8 | index2 u8 | #opts u8((n1<<4)|n2) |
    service_id u16 | instance_id u16 | major u8 | ttl 3B | minor u32
    """
    n_opts = ((n_opts_run1 & 0x0F) << 4) | (n_opts_run2 & 0x0F)
    return struct.pack("!BBBBHHB",
                       entry_type & 0xFF, index1 & 0xFF, index2 & 0xFF, n_opts & 0xFF,
                       service_id & 0xFFFF, instance_id & 0xFFFF, major_version & 0xFF) \
        + _pack_ttl(ttl) \
        + struct.pack("!I", minor_version & 0xFFFFFFFF)


def build_eventgroup_entry(entry_type, service_id, instance_id, eventgroup_id, ttl,
                           major_version=1, index1=0, index2=0,
                           n_opts_run1=0, n_opts_run2=0) -> bytes:
    """Eventgroup 엔트리 16바이트 빌드 (빅엔디안).

    type u8 | index1 u8 | index2 u8 | #opts u8((n1<<4)|n2) |
    service_id u16 | instance_id u16 | major u8 | ttl 3B |
    reserved u16(0x0000) | eventgroup_id u16
    """
    n_opts = ((n_opts_run1 & 0x0F) << 4) | (n_opts_run2 & 0x0F)
    return struct.pack("!BBBBHHB",
                       entry_type & 0xFF, index1 & 0xFF, index2 & 0xFF, n_opts & 0xFF,
                       service_id & 0xFFFF, instance_id & 0xFFFF, major_version & 0xFF) \
        + _pack_ttl(ttl) \
        + struct.pack("!HH", 0x0000, eventgroup_id & 0xFFFF)


def build_ipv4_endpoint_option(ip: str, port: int, l4_proto: int = L4_UDP) -> bytes:
    """IPv4 Endpoint 옵션 12바이트 빌드 (빅엔디안).

    length u16 = 9 | type u8 = OPT_IP4_ENDPOINT | discardable u8 = 0x00 |
    address 4B | reserved u8 = 0x00 | l4_proto u8 | port u16
    """
    return struct.pack("!HBB", 9, OPT_IP4_ENDPOINT, 0x00) \
        + socket.inet_aton(ip) \
        + struct.pack("!BBH", 0x00, l4_proto & 0xFF, port & 0xFFFF)


def build_sd_message(entries: bytes, options: bytes = b"",
                     session_id: int = 1, flags: int = FLAG_UNICAST) -> bytes:
    """전체 SD 패킷 빌드 (빅엔디안).

    SOME/IP 헤더 16B + SD 헤더 12B + entries + options_len u32 + options.
    length 필드 = 20 + len(entries) + len(options). 8바이트 정렬 패딩 없음.
    """
    length = 20 + len(entries) + len(options)
    someip_header = struct.pack(
        "!HHIHHBBBB",
        SD_SERVICE_ID,      # 0xFFFF
        SD_METHOD_ID,       # 0x8100
        length,             # 20 + entries + options
        SD_CLIENT_ID,       # 0x0000
        session_id & 0xFFFF,
        SD_PROTOCOL_VERSION,
        SD_INTERFACE_VERSION,
        SD_MESSAGE_TYPE,
        SD_RETURN_CODE,
    )
    sd_header = struct.pack("!B3sI", flags & 0xFF, b"\x00\x00\x00", len(entries))
    return someip_header + sd_header + entries + struct.pack("!I", len(options)) + options


# --- 편의 빌더 ----------------------------------------------------------------

def build_find_service(service_id, instance_id, session_id=1,
                       ttl=TTL_DEFAULT, flags=FLAG_UNICAST) -> bytes:
    """FindService: ENTRY_FIND_SERVICE 1개, 옵션 없음."""
    entry = build_service_entry(ENTRY_FIND_SERVICE, service_id, instance_id, ttl)
    return build_sd_message(entry, b"", session_id, flags)


def build_offer_service(service_id, instance_id, ip: str, port: int, session_id=1,
                        ttl=TTL_DEFAULT, minor_version=0,
                        flags=FLAG_UNICAST) -> bytes:
    """OfferService: ENTRY_OFFER_SERVICE 1개(index1=0, n_opts_run1=1) + IP4_ENDPOINT 옵션 1개."""
    entry = build_service_entry(ENTRY_OFFER_SERVICE, service_id, instance_id, ttl,
                                minor_version=minor_version, index1=0, n_opts_run1=1)
    option = build_ipv4_endpoint_option(ip, port)
    return build_sd_message(entry, option, session_id, flags)


def build_stop_offer_service(service_id, instance_id, session_id=1) -> bytes:
    """StopOffer: ENTRY_OFFER_SERVICE 1개, TTL=0, 옵션 없음."""
    entry = build_service_entry(ENTRY_OFFER_SERVICE, service_id, instance_id, TTL_STOP)
    return build_sd_message(entry, b"", session_id)


def build_subscribe_eventgroup(service_id, instance_id, eventgroup_id, ip: str, port: int,
                               session_id=1, ttl=TTL_DEFAULT) -> bytes:
    """SubscribeEventgroup: ENTRY_SUBSCRIBE_EVENTGROUP 1개 + IP4_ENDPOINT 옵션 1개."""
    entry = build_eventgroup_entry(ENTRY_SUBSCRIBE_EVENTGROUP, service_id, instance_id,
                                   eventgroup_id, ttl)
    option = build_ipv4_endpoint_option(ip, port)
    return build_sd_message(entry, option, session_id)


# --- 파서 ---------------------------------------------------------------------

def parse_sd(packet: bytes) -> dict:
    """SD 패킷 파싱 -> dict. 무효 시 ValueError.

    offsets: SOME/IP 헤더 16B -> flags@16, entries_len@20, entries@24,
             options_len@(24+len(entries)), options@(28+len(entries))
    서비스 엔트리(0x00~0x02): minor_version@12~16, eventgroup_id 없음.
    이벤트그룹 엔트리(0x04~0x07): eventgroup_id@14~16, minor_version 없음.
    """
    if len(packet) < 16:
        raise ValueError(f"SD packet too short: {len(packet)} bytes")
    (service_id, method_id, length, client_id, session_id,
     protocol_version, interface_version, message_type, return_code) = \
        struct.unpack("!HHIHHBBBB", packet[:16])
    if service_id != SD_SERVICE_ID or method_id != SD_METHOD_ID:
        raise ValueError(
            f"unexpected SD header: service 0x{service_id:04X} method 0x{method_id:04X}")
    if message_type != SD_MESSAGE_TYPE:
        raise ValueError(f"unexpected message_type {message_type:#x} (SD는 0x02)")

    flags = packet[16]
    entries_len = int.from_bytes(packet[20:24], "big")
    if 24 + entries_len + 4 > len(packet):
        raise ValueError(f"entries_len {entries_len} inconsistent with packet size {len(packet)}")
    if entries_len % 16 != 0:
        raise ValueError(f"entries_len {entries_len} not a multiple of 16")
    entries = packet[24:24 + entries_len]

    options_offset = 24 + entries_len
    options_len = int.from_bytes(packet[options_offset:options_offset + 4], "big")
    if options_offset + 4 + options_len > len(packet):
        raise ValueError(f"options_len {options_len} inconsistent with packet size {len(packet)}")
    options = packet[options_offset + 4: options_offset + 4 + options_len]

    parsed_entries = []
    for i in range(0, entries_len, 16):
        chunk = entries[i:i + 16]
        entry = {
            "entry_type": chunk[0],
            "service_id": int.from_bytes(chunk[4:6], "big"),
            "instance_id": int.from_bytes(chunk[6:8], "big"),
            "major_version": chunk[8],
            "ttl": int.from_bytes(chunk[9:12], "big"),
        }
        if 0x00 <= entry["entry_type"] <= 0x02:
            entry["minor_version"] = int.from_bytes(chunk[12:16], "big")
        elif 0x04 <= entry["entry_type"] <= 0x07:
            entry["eventgroup_id"] = int.from_bytes(chunk[14:16], "big")
        parsed_entries.append(entry)

    parsed_options = []
    pos = 0
    while pos < len(options):
        if pos + 4 > len(options):
            raise ValueError("truncated option header")
        opt_length = int.from_bytes(options[pos:pos + 2], "big")
        opt_type = options[pos + 2]
        block = opt_length + 3  # vsomeip 규약: length 필드는 type 필드를 제외한 나머지 길이
        if pos + block > len(options):
            raise ValueError("option length inconsistent with packet size")
        opt_bytes = options[pos:pos + block]
        opt = {"option_type": opt_type}
        if opt_type in (OPT_IP4_ENDPOINT, OPT_IP4_MULTICAST) and block >= 12:
            opt["address"] = socket.inet_ntoa(opt_bytes[4:8])
            opt["l4_proto"] = opt_bytes[9]
            opt["port"] = int.from_bytes(opt_bytes[10:12], "big")
        parsed_options.append(opt)
        pos += block

    return {
        "session_id": session_id,
        "flags": flags,
        "entries": parsed_entries,
        "options": parsed_options,
    }


# --- 클래스 -------------------------------------------------------------------

class SdServer:
    """멀티캐스트로 OfferService 주기 광고 서버.

    첫 광고는 flags 0xC0 (FLAG_REBOOT | FLAG_UNICAST), 이후 0x40
    (vsomeip 세션/리부트 규약). 매 offer_once마다 소켓 생성/종료
    (transport.py의 UdpTransport 패턴, 스레드 안전).
    """

    def __init__(self, service_id, instance_id, ip: str, port: int,
                 multicast=SD_MULTICAST, sd_port=SD_PORT, period_ms=2000):
        self.service_id = service_id
        self.instance_id = instance_id
        self.ip = ip
        self.port = port
        self.multicast = multicast
        self.sd_port = sd_port
        self.period_ms = period_ms
        self._session = 1
        self._first = True
        self._stop = threading.Event()
        self._thread = None

    def offer_once(self):
        """OfferService 1회 전송 (세션 증가). 첫 호출은 flags 0xC0, 이후 0x40."""
        flags = (FLAG_REBOOT | FLAG_UNICAST) if self._first else FLAG_UNICAST
        self._first = False
        packet = build_offer_service(self.service_id, self.instance_id, self.ip, self.port,
                                     session_id=self._session, flags=flags)
        self._session += 1
        if self._session > 0xFFFF:
            self._session = 1
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.sendto(packet, (self.multicast, self.sd_port))
        finally:
            sock.close()

    def start(self):
        """daemon 스레드 시작 (period_ms 주기로 offer_once). 이미 시작됐으면 no-op."""
        if self._thread is not None and self._thread.is_alive():
            return
        self._stop.clear()

        def _run():
            while not self._stop.is_set():
                try:
                    self.offer_once()
                except OSError:
                    pass  # 멀티캐스트 송신 실패는 다음 주기에 재시도
                self._stop.wait(self.period_ms / 1000.0)

        self._thread = threading.Thread(target=_run, daemon=True, name="someip-sd")
        self._thread.start()

    def stop(self):
        """스레드 종료/join. start 전 stop은 no-op."""
        if self._thread is None:
            return
        self._stop.set()
        if self._thread.is_alive():
            self._thread.join()
        self._thread = None


class SdClient:
    """FindService 전송 후 OfferService 응답 수신 클라이언트.

    SD_PORT에 bind + IP_ADD_MEMBERSHIP 후 멀티캐스트로 FindService 전송,
    수신된 패킷을 parse_sd로 해석해 IP4_ENDPOINT 옵션을 가진
    OfferService 엔트리를 dict 리스트로 반환한다.
    """

    def __init__(self, multicast=SD_MULTICAST, sd_port=SD_PORT, timeout_s=2.0):
        self.multicast = multicast
        self.sd_port = sd_port
        self.timeout_s = timeout_s

    def discover(self, service_id=None, instance_id=None, timeout_s=None) -> list:
        """FindService 전송 후 OfferService 응답 수집.

        service_id/instance_id가 None이면 와일드카드(0xFFFF)로 검색.
        같은 서비스(service_id, instance_id)는 dict로 dedupe,
        timeout 내 수신 데이터가 없으면 빈 리스트.
        """
        if timeout_s is None:
            timeout_s = self.timeout_s
        find_service_id = service_id if service_id is not None else 0xFFFF
        find_instance_id = instance_id if instance_id is not None else 0xFFFF
        packet = build_find_service(find_service_id, find_instance_id)

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("", self.sd_port))
            mreq = struct.pack("4s4s", socket.inet_aton(self.multicast),
                               socket.inet_aton("0.0.0.0"))
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
            sock.sendto(packet, (self.multicast, self.sd_port))

            results = {}
            deadline = time.monotonic() + timeout_s
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                sock.settimeout(remaining)
                try:
                    data, _addr = sock.recvfrom(4096)
                except socket.timeout:
                    break
                try:
                    parsed = parse_sd(data)
                except ValueError:
                    continue
                for entry in parsed["entries"]:
                    if entry["entry_type"] != ENTRY_OFFER_SERVICE:
                        continue
                    if find_service_id != 0xFFFF and entry["service_id"] != find_service_id:
                        continue
                    if find_instance_id != 0xFFFF and entry["instance_id"] != find_instance_id:
                        continue
                    for opt in parsed["options"]:
                        if opt["option_type"] == OPT_IP4_ENDPOINT and "address" in opt:
                            key = (entry["service_id"], entry["instance_id"])
                            results[key] = {
                                "service_id": entry["service_id"],
                                "instance_id": entry["instance_id"],
                                "ip": opt["address"],
                                "port": opt["port"],
                            }
                            break
        finally:
            sock.close()
        return list(results.values())
