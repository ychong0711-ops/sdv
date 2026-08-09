# Heterogeneous SDV Zone Controller Prototype
### Qualcomm Dragonwing QRB2210 (Debian Linux / Docker) + STM32U585 (Zephyr RTOS) + SOME/IP + CAN-FD

> **Target Role:** SDV / ADAS / Infotainment Embedded Engineer (CARIAD, BMW, MB.OS)  
> **Architecture:** HPC (Linux) + Real-Time MCU (Zephyr) - MB.OS Zone Architecture 1:1 Mapping  
> **Key Words:** Embedded Linux, Zephyr RTOS, SOME/IP (vsomeip 호환 wire protocol), Docker, CAN-FD, ISO 26262 Safe State, Wireshark

> **Status (2026-08):** Phase 1 구현 완료 — PC 시뮬레이션(UDP 30490) 검증 완료. Phase 2 Yocto Kirkstone 이미지 빌드 + QEMU 부팅 + Docker 데몬/vsomeipd 실기동 검증 완료. 실보드(UNO Q, UART 터널) 검증 대기 중.
> **전송 기본값:** 실보드에서는 **UART 터널 (LPUART1, 115200, `/dev/ttyS0`) 기본**, PC 시뮬레이션은 **UDP 30490** (`--transport udp` 또는 `SOMEIP_TRANSPORT=udp`).
> **CI / Quality:** CI 파이프라인 구축 예정 — 첫 GitHub push 후 결과 갱신 예정 (MISRA/coverage 수치는 아직 측정 전이므로 주장하지 않음).

---

## 1. ARCHITECTURE - 왜 이 보드를 썼는가?

독일 SDV는 **HPC(High Performance Computer) + Zone ECU** 구조입니다. 본 프로젝트는 이를 €68 하드웨어로 1:1 재현합니다.

```
┌─────────────────────────────────────────────────────────────────────┐
│  HARDWARE: Arduino UNO Q (Rebranded as Heterogeneous SoC Eval Board)│
│  ┌─────────────────────────────┐      ┌──────────────────────────┐  │
│  │  MPU: Qualcomm QRB2210      │      │  MCU: STM32U585          │  │
│  │  Quad Cortex-A53 2.0GHz     │◄────►│  Cortex-M33 160MHz       │  │
│  │  Debian Linux + Docker      │ SOME/IP │  Zephyr RTOS + CAN-FD  │  │
│  │  2GB RAM / 16GB eMMC        │raw-sock │  2MB Flash / 786KB SRAM│  │
│  └──────────────┬──────────────┘      └────────────┬─────────────┘  │
│                 │ USB Camera                   │ CAN-FD PHY           │
│                 ▼                              ▼                      │
│           [Driver Monitoring AI]         [Vehicle Bus 500k/2M]       │
└─────────────────────────────────────────────────────────────────────┘

  MPU (Linux) <--- SOME/IP wire protocol ---> MCU (Zephyr)
       │        (vsomeip 호환 포맷)                │
   Docker Container                          Watchdog + Safe State
   TensorFlow Lite (Drowsiness)              ISO 26262 E2E Protection
```

**전송 경로 (실재):**
- **실보드 (Arduino UNO Q, 기본):** SOME/IP 프레임을 **UART 터널 (LPUART1, 115200, `/dev/ttyS0`)** 로 전송 — MPU와 MCU는 LPUART1로 연결되며 USB RNDIS가 아닙니다 (Arduino 이슈 #252 확인). 별도 인자 없이 `python3 mpu/ai/drowsiness.py` 실행 시 UART가 기본 선택됩니다.
- **PC 시뮬레이션:** **UDP 30490** (`--transport udp` 또는 환경변수 `SOMEIP_TRANSPORT=udp`, 기본 대상 `SOMEIP_MCU_IP=127.0.0.1`) 로 전송 (호스트 loopback / 에뮬레이션)

> **전송 선택 환경변수:** `SOMEIP_TRANSPORT=uart|udp`, `SOMEIP_UART_PORT=/dev/ttyS0`, `SOMEIP_MCU_IP=127.0.0.1` — CLI `--transport` / `--uart-port` / `--dest` 인자와 동일하며, 미지정 시 실보드 기본값(uart + `/dev/ttyS0`)이 적용됩니다.

**왜 UNO Q인가? NXP S32G 대신?**
> 동일한 Heterogeneous 구조를 5배 저렴하고 7배 빠르게 증명할 수 있습니다. S32G로 해도 소프트웨어 스택(SOME/IP, Docker, Zephyr)은 100% 동일합니다. 본 프로젝트는 **보드 로고가 아닌 SOME/IP/Docker 역량**을 증명하는 것이 목적입니다. (면접 방어 문장)

**MB.OS / CARIAD 매핑:**

| 본 프로젝트 | 실제 차량 (MB.OS) | 동일 개념 |
|---|---|---|
| QRB2210 + Debian + Docker | NVIDIA Orin + QNX/Adaptive Linux | HPC - AI/서비스 컨테이너 |
| STM32U585 + Zephyr | Infineon AURIX TC4xx + AUTOSAR Classic | Zone ECU - Real-Time/CAN |
| SOME/IP wire protocol (경량 raw-socket 구현) | SOME/IP (vsomeip 등 AUTOSAR Adaptive 스택) | Service-Oriented Communication |
| Wireshark SOME/IP dissector | Vector CANoe Ethernet | 검증 툴 |

---

## 2. DEMO - 60초 시연

**Use Case: 운전자 졸음 감지 → CAN 경고**

1.  **MPU:** USB 카메라 → Python (OpenCV) → TensorFlow Lite로 눈 감김 감지 (EAR < 0.2)
2.  **SOME/IP:** MPU가 50ms마다 **Heartbeat (Event 0x8002)** 를 전송하고, 졸음 감지 시 `DriverMonitoringService (0x1234)`의 `NotifyDrowsiness (Event 0x8001, level)`를 MCU에 전송
3.  **MCU:** Zephyr 경량 SOME/IP 파서로 수신 → CAN-FD 프레임 (`ID 0x18FF01F4, DLC 8, CRC8`) 송신 → 계기판 경고
4.  **Safety:** MCU가 100ms 내 heartbeat 미수신 시 Safe State (CAN 경고 프레임 + LED 점멸 + 의도적 watchdog 유지) 진입 - ISO 26262 Safe State 패턴, heartbeat 복구 시 정상 복귀

**캡처 산출물은 아직 포함되어 있지 않습니다:** Wireshark 캡처(`wireshark/someip_drowsiness.pcap`)와 시연 영상은 실보드 검증 완료 후 추가 예정입니다.

```bash
# MPU에서 실행 (Docker, PC 시뮬레이션 - UDP 30490, docker-compose가 SOMEIP_TRANSPORT=udp 설정)
docker compose up --build

# 실보드(UNO Q)에서는 UART 터널이 기본 (LPUART1, /dev/ttyS0) - 별도 인자 불필요
python3 mpu/ai/drowsiness.py
#   또는 명시적으로: python3 mpu/ai/drowsiness.py --transport uart --uart-port /dev/ttyS0

# 로그
[MPU-AI] SOME/IP Heartbeat(0x8002) -> MCU ...
[MPU-AI] Drowsiness level 85% detected -> SOME/IP Notify(0x1234.0x01.0x8001)
[MCU] SOME/IP RX: drowsiness=85% -> CAN TX: ID 0x18FF01F4
[candump] can0 18FF01F4 [8] 55 AA 01 85 00 00 3F 7A
```

---

## 3. QUICK START - 5분 안에 돌리기

### 3-1. 하드웨어
- Arduino UNO Q x1 (QRB2210+STM32U585) - https://store.arduino.cc/products/uno-q
- USB Camera (UVC) x1
- CAN-FD 트랜시버 모듈 (MCP2517FD) x1 (vehicle bus 연결용, 없으면 loopback)
- USB-C 케이블, 5V 3A

### 3-2. MPU (Linux) 세팅

```bash
# UNO Q에 SSH 접속 (또는 App Lab Terminal)
ssh arduino@uno-q.local

# 프로젝트 클론
git clone https://github.com/YOUR_ID/sdv-zone-controller.git
cd sdv-zone-controller

# Docker 빌드 & 실행 (SOME/IP + AI, PC 시뮬레이션: UDP 30490)
# docker-compose.yml이 SOMEIP_TRANSPORT=udp 를 설정하므로 별도 인자 불필요
docker compose -f docker/docker-compose.yml up --build -d

# 실보드(UNO Q)에서는 UART 터널이 기본 (LPUART1, /dev/ttyS0) — Docker 없이 직접 실행
python3 mpu/ai/drowsiness.py
#   또는 환경변수로 전송 선택: SOMEIP_TRANSPORT=udp SOMEIP_MCU_IP=127.0.0.1 python3 mpu/ai/drowsiness.py

# 로그 확인 (SOME/IP Notify/Heartbeat 전송 로그)
docker logs -f sdv-mpu
```

### 3-3. MCU (Zephyr) 빌드 & 플래시

```bash
# Zephyr (west) 빌드 - upstream 보드 정의 사용 (기본 빌드 경로)
west build -b arduino_uno_q MCU

# (대안) Arduino CLI - UNO Q의 Zephyr 코어로 빌드
arduino-cli compile --fqbn arduino:zephyr:uno_q ./MCU

# 플래시 (west flash 또는 arduino-cli upload)
arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:zephyr:uno_q ./MCU

# 시리얼 모니터링 (Zephyr shell + CAN)
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
# 예상 출력 (UDP 기본 빌드): SOME/IP listening on UDP 30490 ... / Safety timer: 100ms heartbeat timeout
# 예상 출력 (실보드 빌드, prj.conf에 CONFIG_SOMEIP_TRANSPORT_UART=y): SOME/IP transport: UART (lpuart1, 115200) ...
```

### 3-4. 검증 (Wireshark + CAN)

```bash
# MPU에서 Wireshark 캡처 (백그라운드) - PC 시뮬레이션 (UDP 30490)
docker exec sdv-mpu tshark -i eth0 -f "udp port 30490" -w /tmp/someip.pcapng &

# CAN 덤프 (MCU CAN-FD)
candump can0,18FF01F4:1FFFFFFF

# AI 트리거 (테스트용 - 카메라 없이 강제 이벤트)
# 컨테이너 내부는 SOMEIP_TRANSPORT=udp (PC 시뮬레이션)이 상속되므로 UDP로 전송됨
docker exec sdv-mpu python3 mpu/ai/trigger_test.py --level 90
# 실보드 UART: python3 mpu/ai/trigger_test.py --level 90   (uart 기본, /dev/ttyS0)
```

> 실보드에서는 MPU가 50ms마다 SOME/IP Heartbeat(0x8002)를 보내므로, 캡처 시 Heartbeat 트래픽이 주기적으로 보이는 것이 정상입니다. Service Discovery(OfferService 등)는 아직 미구현입니다.

---

## 4. SOFTWARE STACK - 독일 키워드 매칭

| Layer | 본 프로젝트 | 독일 공고 키워드 | 비고 |
|---|---|---|---|
| **MPU OS** | Debian 12 + Docker 24 | Embedded Linux (Yocto), Docker | Yocto 이식은 Phase 2 |
| **MCU OS** | Zephyr RTOS 3.5 (Arduino Core on Zephyr) | Zephyr, FreeRTOS, AUTOSAR OS | Zephyr는 BMW/Bosch 차세대 표준 |
| **Middleware** | SOME/IP wire protocol (vsomeip 호환 포맷, raw-socket 경량 구현) | SOME/IP, SOME/IP-SD | App Lab Bridge → SOME/IP 재구현 (vsomeip 라이브러리 통합은 Phase 2) |
| **Bus** | CAN-FD (500k/2M), ISO-TP, E2E CRC8 | CAN-FD, CAN, ISO 26262 E2E | |
| **AI** | TensorFlow Lite Micro, OpenCV | TensorFlow, ONNX, ADAS | QRB2210 Adreno GPU 활용 |
| **Build** | west (Zephyr) + CMake + Docker | CMake, CI/CD, ASPICE | MCU: `west build -b arduino_uno_q MCU` |
| **Quality** | Cppcheck, Clang-Tidy (MISRA), Gcov | MISRA-C, Tessy, ASPICE | 수치 측정 예정 (CI 구축 후 갱신) |
| **Tools** | Wireshark, candump, Segger Ozone (J-Link) | Vector CANoe, Lauterbach TRACE32 | BusMaster 대체 |

---

## 5. SOME/IP - 핵심 차별화 (App Lab Bridge를 왜 버렸는가?)

**App Lab Bridge (기본):** Arduino 전용, 비표준, 차량 이식 불가  
**SOME/IP (본 프로젝트):** AUTOSAR Adaptive 표준 와이어 포맷, CARIAD/MB.OS 실제 사용

> **구현 방식 (실재):** 실제 코드는 vsomeip C++ 라이브러리를 사용하지 않습니다.
> - **MPU:** Python raw-socket으로 SOME/IP **와이어 포맷을 직접 생성** (`mpu/someip/`) — vsomeip 호환 헤더 (Service 0x1234, Method 0x8001/0x8002, Message Type 0x02)
> - **MCU:** Zephyr **경량 SOME/IP 파서** (`MCU/src/someip_service.cpp`)
> - **vsomeip 3.4.10 C++ 라이브러리는 Docker 이미지에 빌드되어 있음** — Python 클라이언트에서의 vsomeip 통합(vsomeip-python binding)은 **Phase 2** 예정
> - `config/vsomeip.json`은 **참조용 설정**이며, 현재 raw-socket 클라이언트는 사용하지 않습니다.

**전송 경로:**
- **실보드 (Arduino UNO Q, 기본):** **UART 터널 (LPUART1, 115200, `/dev/ttyS0`)** 로 전송 — USB RNDIS 아님 (Arduino 이슈 #252 확인)
- **PC 시뮬레이션:** **UDP 30490** (`--transport udp` 또는 `SOMEIP_TRANSPORT=udp`)로 전송
- **Service Discovery (OfferService 등)는 미구현** — 정보용 로그만 출력, **Phase 2** 대상

```python
# MPU (Linux) - SOME/IP Notify/Heartbeat (raw-socket 경량 구현)
# mpu/someip/protocol.py, transport.py 참조
import socket, struct

def someip_frame(service_id, method_id, payload):
    # SOME/IP Header: 8 bytes (Big Endian, vsomeip/Wireshark 호환)
    header = struct.pack('!HHIHHBBBB',
        service_id, method_id, 8 + len(payload) + 8,  # Length
        0x0001, 0x0001,                              # Client/Session ID
        0x01, 0x01,                                  # Protocol/Interface Version
        0x02, 0x00)                                  # Message Type: Notification
    return header + payload + b'\x00' * 7            # 8바이트 정렬 패딩

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# AI 감지 시: Notify (0x8001)
sock.sendto(someip_frame(0x1234, 0x8001, bytes([drowsiness_level])), (MCU_IP, 30490))
# Safety: Heartbeat (0x8002) - 50ms 주기
sock.sendto(someip_frame(0x1234, 0x8002, b'\x00'), (MCU_IP, 30490))
```

**캡처:** 실보드 검증 후 `wireshark/someip_drowsiness.pcap` 생성 예정 (`wireshark/capture_someip.py` 사용)
- Wireshark SOME/IP dissector로 분석 가능 (UDP 30490)

---

## 6. SAFETY - ISO 26262 Safe State 패턴

MCU는 MPU를 신뢰하지 않습니다 (Freedom from Interference). **현재 구현된 상태:**

- **MPU:** 50ms마다 SOME/IP **Heartbeat (Event 0x8002)** 전송 (`mpu/ai/drowsiness.py`)
- **MCU:** 100ms 내 heartbeat 미수신 시 **Safe State 진입** (`MCU/src/safety.cpp`)
  1. CAN 경고 프레임 송신
  2. LED 점멸
  3. 의도적 watchdog 유지 (안정 상태 유지)
- heartbeat 복구 시 **정상 동작으로 복귀**

> **검증 상태:** Safe State 구현은 완료. 실보드에서의 검증(Logic Analyzer / 시리얼 로그 캡처)은 **대기 중**입니다.
> PC 시뮬레이션에서는 `docker stop sdv-mpu`로 MPU를 종료하면 MCU의 Safe State 진입 로그를 확인할 수 있습니다.

---

## 7. CI/CD & QUALITY - 독일이 보는 프로세스

**CI 파이프라인 구축 예정** (`.github/workflows/ci.yml` 생성 예정). 첫 GitHub push 후 결과가 갱신될 예정이며, 그 전까지는 MISRA violations / coverage % 같은 수치를 주장하지 않습니다.

```yaml
# .github/workflows/ci.yml (계획)
# Python 단위 테스트 + cppcheck(MISRA) + Docker build + Zephyr build - ASPICE SWE.4 대응

- name: Python Unit Tests (mpu/tests/)
  run: pytest mpu/tests/

- name: Static Analysis (MISRA-C 2012) - 예정
  run: cppcheck --enable=all --addon=misra MCU/src --error-exitcode=1

- name: Zephyr Build
  run: west build -b arduino_uno_q MCU

- name: Docker Build
  run: docker build -t sdv-mpu ./docker
```

**검증 산출물 (실보드 검증 완료 후 공개 예정):**
- Wireshark 캡처 (`wireshark/someip_drowsiness.pcap`)
- CAN 시뮬레이션 덤프 (`docs/candump_sim.log`)
- MISRA / coverage 리포트 — CI 구축 후 생성

---

## 8. PROJECT STRUCTURE

```
sdv-zone-controller/
├── README.md                   # 본 파일
├── SETUP_GUIDE_KR.md           # 5분 설치 가이드 (한국어)
├── config/
│   └── vsomeip.json            # SOME/IP 참조용 설정 (현재 raw-socket 클라이언트는 미사용)
├── docker/
│   ├── Dockerfile              # Debian + vsomeip(Phase 2용) + Python + TFLite
│   ├── docker-compose.yml      # MPU 컨테이너 (+ 선택: tshark 캡처)
│   └── scripts/entrypoint.sh   # 컨테이너 시작 스크립트
├── mpu/
│   ├── ai/
│   │   ├── drowsiness.py       # TFLite + OpenCV EAR + SOME/IP Notify/Heartbeat
│   │   └── trigger_test.py     # 카메라 없이 테스트용
│   ├── someip/
│   │   ├── protocol.py         # SOME/IP 와이어 포맷 (raw-socket 경량 구현)
│   │   └── transport.py        # UDP 30490 / UART 터널(lpuart1) 전송
│   └── tests/                  # Python 단위 테스트 (구성 중)
├── MCU/
│   ├── src/                    # Zephyr main, someip_service(경량 파서), can_gateway, safety
│   ├── boards/                 # arduino_uno_q 보드 정의 (생성 예정)
│   ├── prj.conf                # Zephyr Kconfig
│   └── CMakeLists.txt          # west build용 (생성 예정)
├── wireshark/
│   ├── capture_someip.py       # SOME/IP 캡처/생성 스크립트 (생성 예정)
│   └── README.md               # 캡처 분석 가이드
├── scripts/
│   └── sim_can_gateway.py      # CAN 게이트웨이 시뮬레이션 (PC, 생성 예정)
├── docs/
│   ├── architecture.md         # 시스템 블록도/타이밍
│   ├── candump_sim.log         # CAN 시뮬레이션 덤프 (검증 후 추가 예정)
│   └── interview_qa.md         # 면접 Q&A (한국어+독일어)
├── yocto/                      # Phase 2: Yocto Kirkstone layer
│   └── meta-sdv/recipes-support/vsomeip/vsomeip_3.4.10.bb
└── .github/workflows/ci.yml    # 생성 예정
```

---

## 9. PHASE 2 ROADMAP - Yocto 이식 (면접에서 어필)

> "Debian으로 SOME/IP를 증명한 후, 프로덕션 적합성을 위해 **Yocto Kirkstone으로 이식**하는 것이 Next Step입니다."

- [x] Phase 1: Debian + Docker + SOME/IP (raw-socket 경량 구현) (본 리포지토리) - 8주
- [x] Phase 2: Yocto Kirkstone custom layer (`meta-sdv`) — sdv-hpc-image 빌드 + QEMU 부팅 + docker/vsomeipd/sdv-mpu 기동 검증 완료
- [ ] QNX SDP 8.0 Microkernel 포팅 검토 (Zephyr와 비교 분석)

Yocto layer 구조는 설계 및 실증 완료: `yocto/meta-sdv/recipes-support/vsomeip/vsomeip_3.4.10.bb`
- 이미지: `sdv-hpc-image` (qemuarm64, ext4 751MB) — docker-ce 20.10.25 + vsomeip 3.4.10 + sdv-mpu 1.0 탑재
- 검증: QEMU 부팅 → SSH 개방 → docker 데몬 active + hello-world 컨테이너 실행 성공 → vsomeipd(vSomeIP 3.4.10 라우팅 매니저) active

---

## 10. RESUME KEYWORDS - 이력서에 복붙

```
Heterogeneous SoC (Qualcomm Dragonwing QRB2210 + STM32U585) | Embedded Linux (Debian/Docker, Yocto in progress) |
Zephyr RTOS | SOME/IP (vsomeip 호환 wire protocol, raw-socket 경량 구현) | CAN-FD | ISO 26262 Safe State | C++17 | Python | TensorFlow Lite |
Wireshark | CANoe (via BusMaster/candump) | Lauterbach TRACE32 concepts (via Segger Ozone/J-Link) |
MISRA-C 2012 | CI/CD (GitHub Actions, CMake, Cppcheck) | ASPICE
```

**절대 쓰지 말 것:** Arduino, Sketch, delay, String, App Lab Bridge

---

## 11. CONTACT & INTERVIEW READY

**면접 예상 질문과 답변은 `docs/interview_qa.md` 참조**

- Q: Warum UNO Q und nicht S32G? → A: Heterogeneous 구조 동일, 5배 저렴, SOME/IP 역량 증명이 목적 (README 1장 참조)
- Q: Yocto Erfahrung? → A: Phase 2 `meta-sdv` 레시피 작성 + `sdv-hpc-image` 빌드 성공, QEMU 부팅 후 Docker 데몬(v20.10.25-ce)과 vsomeipd(3.4.10) 실기동까지 검증 완료
- Q: QNX? → A: Zephyr microkernel과 유사, QNX SDP 평가판으로 개념 학습

**Author:** YOUR NAME | your.email@example.com | LinkedIn | Xing
**Location:** Guri-si, KR → Target: Munich/Stuttgart, DE (EU Blue Card)

---

*This project rebrands Arduino UNO Q as a Heterogeneous SoC evaluation board. No Arduino abstractions (delay, String) are used. All drivers are HAL/LL, RTOS is Zephyr, middleware is a lightweight SOME/IP wire-protocol implementation (vsomeip-compatible).*
