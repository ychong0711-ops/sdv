"""
SOME/IP 전송 계층: UDP(PC 시뮬레이션) / UART(실보드 LPUART1 터널).

UdpTransport는 매 send 호출마다 소켓을 생성/종료한다 (기존 패턴 유지, 스레드 안전).
UartTransport는 pyserial을 lazy import 하며, 미설치 시 명확한 RuntimeError를 발생시킨다.
"""
import socket

from mpu.someip.protocol import SOMEIP_PORT


class UdpTransport:
    """UDP transport: 127.0.0.1:30490 기본 (PC 시뮬레이션)."""

    def __init__(self, host: str = "127.0.0.1", port: int = SOMEIP_PORT):
        self.host = host
        self.port = port

    def send(self, packet: bytes) -> None:
        """매 호출 소켓 생성/종료 (스레드 안전)."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.sendto(packet, (self.host, self.port))
        finally:
            sock.close()

    def close(self) -> None:
        # UDP는 매 send 시 소켓을 닫으므로 정리할 상태가 없다
        pass


class UartTransport:
    """UART transport: 실보드 MPU-MCU 물리 링크 (LPUART1)."""

    def __init__(self, port: str, baud: int = 115200):
        try:
            import serial  # pyserial lazy import
        except ImportError as exc:
            raise RuntimeError(
                "pyserial not installed - run: pip install pyserial==3.5 "
                "(docker/requirements.txt 참고)"
            ) from exc
        self._serial = serial.Serial(port=port, baudrate=baud, timeout=0.1)

    def send(self, packet: bytes) -> None:
        self._serial.write(packet)
        self._serial.flush()

    def close(self) -> None:
        serial_obj = getattr(self, "_serial", None)
        if serial_obj is not None:
            try:
                serial_obj.close()
            except Exception:
                pass
