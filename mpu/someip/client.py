"""
MPU SOME/IP Client.

vsomeip 라이브러리 대신 SOME/IP 와이어 포맷 경량 구현 (포트폴리오 목적).
- protocol.build_packet 으로 패킷 생성
- transport (UDP/UART) 로 전송
"""
import argparse
from typing import Optional

from mpu.someip import protocol
from mpu.someip import transport as someip_transport


def create_transport(
    transport_name: str = "uart",
    host: str = "127.0.0.1",
    port: int = protocol.SOMEIP_PORT,
    uart_port: Optional[str] = "/dev/ttyS0",
):
    """전송 팩토리.

    실보드 기본: 'uart' + /dev/ttyS0 (MPU-MCU LPUART1 물리 링크).
    PC 시뮬레이션: 'udp' + 127.0.0.1:30490 (host loopback).
    """
    if transport_name == "uart":
        if not uart_port:
            raise ValueError("uart transport requires uart_port (기본 /dev/ttyS0)")
        return someip_transport.UartTransport(uart_port)
    return someip_transport.UdpTransport(host, port)


def send_notify(level: int, transport, session: int = 1) -> None:
    """졸음 Notify 전송: EVENT_NOTIFY(0x8001), payload 1바이트 level."""
    transport.send(protocol.build_notify(level, session))


def send_heartbeat(counter: int, transport, session: int = 1) -> None:
    """Heartbeat 전송: EVENT_HEARTBEAT(0x8002), payload 1바이트 counter."""
    transport.send(protocol.build_heartbeat(counter, session))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SOME/IP notify 단일 전송 예시")
    parser.add_argument("--level", type=int, default=85, help="drowsiness level 0-100")
    parser.add_argument("--dest", default="127.0.0.1", help="MCU SOME/IP 대상 IP (UDP 시뮬레이션 전용)")
    parser.add_argument("--transport", default="uart", choices=["udp", "uart"],
                        help="전송 방식: uart(실보드 기본) / udp(PC 시뮬레이션)")
    parser.add_argument("--uart-port", default="/dev/ttyS0", help="uart transport 전용 포트 (기본 /dev/ttyS0)")
    args = parser.parse_args()

    t = create_transport(args.transport, host=args.dest,
                         port=protocol.SOMEIP_PORT, uart_port=args.uart_port)
    send_notify(args.level, t)
    print(f"[CLIENT] notify level={args.level} via {args.transport} "
          f"-> {args.dest}:{protocol.SOMEIP_PORT}")
    t.close()
