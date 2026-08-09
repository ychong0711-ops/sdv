# Architecture Diagram (GitHub README용)

```
[USB Camera] ──► [MPU: QRB2210] ──SOME/IP wire protocol──► [MCU: STM32U585] ──CAN-FD──► [Vehicle]
                  Debian/Docker                           Zephyr RTOS
                  Python (raw-socket)                     C++ / 경량 SOME/IP 파서 + Safety

전송 경로:
- PC 시뮬레이션 (기본): SOME/IP 프레임을 UDP 30490으로 전송 (호스트 loopback)
- 실보드 (Arduino UNO Q): UART 터널 (LPUART1, 115200)로 전송 선택 가능 (USB RNDIS 아님)

Timing (목표값 - 실측치는 실보드 검증 후 갱신 예정):
Camera 15fps → AI 100ms → SOME/IP 5ms → CAN 10ms → Total <150ms (Euro NCAP DMS 요구 <200ms)

Safety:
MPU heartbeat 50ms (SOME/IP Event 0x8002) → MCU watchdog 100ms → Safe State (CAN warning + LED 점멸 + 의도적 watchdog 유지)
heartbeat 복구 시 정상 복귀
Freedom from Interference: MPU와 MCU 메모리 분리, Watchdog 독립
```
