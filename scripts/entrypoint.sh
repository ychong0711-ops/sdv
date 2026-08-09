#!/bin/bash
set -e

echo "[ENTRYPOINT] Starting SDV MPU (QRB2210) - SOME/IP (raw-socket) + AI"

# 1. CAN 인터페이스 설정 (있는 경우)
if ip link show can0 &>/dev/null; then
  echo "[ENTRYPOINT] Configuring CAN (loopback for demo)"
  ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on || true
  ip link set can0 up || true
fi

# 2. AI + SOME/IP Client (raw-socket 경량 구현) - 메인 프로세스
#    전송 방식 (env 우선, 미설정 시 Python 기본값 사용):
#      실보드(기본): SOMEIP_TRANSPORT=uart SOMEIP_UART_PORT=/dev/ttyS0 (LPUART1 터널)
#      PC 시뮬레이션: SOMEIP_TRANSPORT=udp SOMEIP_MCU_IP=127.0.0.1 (UDP 30490)
echo "[ENTRYPOINT] Starting AI (drowsiness.py)..."
trap 'exit 0' SIGTERM SIGINT

TRANSPORT_ARGS=()
if [ -n "$SOMEIP_TRANSPORT" ]; then
  TRANSPORT_ARGS+=(--transport "$SOMEIP_TRANSPORT")
fi
if [ -n "$SOMEIP_UART_PORT" ]; then
  TRANSPORT_ARGS+=(--uart-port "$SOMEIP_UART_PORT")
fi
if [ -n "$SOMEIP_MCU_IP" ]; then
  TRANSPORT_ARGS+=(--dest "$SOMEIP_MCU_IP")
fi

exec python3 /app/mpu/ai/drowsiness.py "${TRANSPORT_ARGS[@]}"
