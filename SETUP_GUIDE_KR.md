# 하이브리드 Phase 1 - 5분 설치 가이드 (한국어)

## 0. 준비물
- Arduino UNO Q (€68) 1개
- USB Camera (UVC, 다이소 1만원짜리도 OK)
- USB-C 케이블 1개
- PC (Windows/Mac/Linux 모두 OK)

## 1. UNO Q 초기 세팅 (10분)
1. Arduino App Lab 설치: https://www.arduino.cc/en/software/#app-lab-section
2. UNO Q를 USB-C로 PC에 연결
3. App Lab에서 UNO Q가 잡히는지 확인 → `Debian` 터미널 열기
4. SSH 확인: `ssh arduino@<uno-q-ip>` (App Lab에서 IP 확인)

## 2. 프로젝트 업로드 (3분)
```bash
# PC에서
git clone https://github.com/YOUR_ID/sdv-zone-controller.git
# UNO Q에 복사 (SCP 또는 App Lab File Transfer)
scp -r sdv-zone-controller arduino@uno-q.local:/home/arduino/

# UNO Q에 SSH 접속
ssh arduino@uno-q.local
cd sdv-zone-controller
```

## 3. MPU 실행 (Docker) - 2분
```bash
# UNO Q 내부에서
docker compose -f docker/docker-compose.yml up --build -d
docker logs -f sdv-mpu
# [MPU-AI] SDV MPU AI Started ... 보이면 성공
```

## 4. MCU 플래시 (Zephyr) - 2분
```bash
# PC에서 Zephyr(west)로 빌드 (기본 빌드 경로)
west build -b arduino_uno_q MCU

# (대안) Arduino CLI - UNO Q의 Zephyr 코어로 빌드
arduino-cli compile --fqbn arduino:zephyr:uno_q ./MCU
arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:zephyr:uno_q ./MCU

# 로그 확인
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
# [00:00:01] SOME/IP listening on UDP 30490 ... 보이면 성공
```

> **전송:** PC 시뮬레이션에서는 SOME/IP 프레임을 **UDP 30490**으로 전송합니다 (기본). 실보드(UNO Q)에서는 **UART 터널 (LPUART1, 115200)** 로 전송을 선택할 수 있습니다 (USB RNDIS 아님).
> **Heartbeat:** MPU가 50ms마다 SOME/IP Heartbeat(Event 0x8002)를 보내고, MCU가 100ms 내 미수신하면 Safe State에 진입합니다.
> **검증 상태:** PC 시뮬레이션(UDP 30490) 검증 완료. 실보드(UART 터널) 검증은 **대기 중**입니다.

## 5. 테스트 (카메라 없이) - 1분
```bash
# UNO Q에서 (MPU 컨테이너 내부)
docker exec sdv-mpu python3 mpu/ai/trigger_test.py --level 85

# MCU 시리얼에서 확인
# [SOME/IP RX: drowsiness=85%]
# [CAN TX: ID 0x18FF01F4 ...]

# PC에서 Wireshark로 캡처하려면
docker compose --profile debug up tshark
# wireshark/live_capture.pcapng 파일을 PC로 다운로드 후 Wireshark로 열기
# 필터: someip.serviceid == 0x1234
```

## 6. GitHub에 업로드 (포트폴리오 완성)
```bash
# PC에서
git add .
git commit -m "Phase 1: Heterogeneous SDV Prototype (QRB2210+STM32U585)"
git push origin main
# README.md의 YOUR_ID를 본인 ID로 교체
# docs/wireshark_someip.png 스크린샷 추가
```

## 7. 이력서에 쓰기
```
프로젝트: Heterogeneous SDV Zone Controller (QRB2210 + STM32U585)
- Embedded Linux (Debian/Docker) + Zephyr RTOS 기반 Zone Controller
- SOME/IP wire protocol (vsomeip 호환 포맷) Service 0x1234/0x5678 구현 - Heartbeat(0x8002) + Notify(0x8001)
- CAN-FD (500k/2M) + E2E CRC8, ISO 26262 Safe State (Heartbeat 50ms / Watchdog 100ms)
- CI/CD (west + cppcheck MISRA + pytest, 수치 검증 예정)
- Hardware: Heterogeneous SoC Eval Board (Quad A53 + Cortex-M33)
```

## 문제 해결
- 카메라 없어도 `trigger_test.py`로 전부 시연 가능 (면접에서 카메라 없어도 OK)
- SOME/IP 전송이 안 되면: 기본은 UDP 30490(PC 시뮬레이션), 실보드는 UART 터널(lpuart1, 115200) 선택 여부 확인
- CAN 없으면 loopback으로 테스트: `can_gateway.cpp`에서 can_send 대신 LOG_INF만
- UNO Q 없으면? PC에서 전체 시뮬레이션 가능 (MPU Docker + MCU는 QEMU)

## 다음 단계 (Phase 2)
- `git checkout -b yocto` 생성 후 Yocto Kirkstone 빌드
- 면접에서 "Phase 2에서 Yocto로 이식 중"이라고 말하기
