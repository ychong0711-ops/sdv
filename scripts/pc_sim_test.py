#!/usr/bin/env python3
"""PC 시뮬레이션 통합 검증 오케스트레이터.

실제 MPU 코드(SdServer + client)를 멀티캐스트/UDP로 띄우고,
실제 MCU 프로토콜 코드(someip_sd.cpp)로 컴파일한 호스트 시뮬레이터를
실행해 SD 흐름(Find -> Offer -> Subscribe)과 이벤트 수신을 검증한다.

사용법 (repo root 에서):
  g++ -std=c++17 -Wall -Wextra -Werror -IMCU/src \
      MCU/tests/sd_host_sim.cpp MCU/src/someip_sd.cpp -o build/sd_host_sim
  python scripts/pc_sim_test.py build/sd_host_sim [--timeout 25]

반환: 전부 성공 시 0, 실패 시 1 (CI 연동용).
"""
import argparse
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from mpu.someip import client as someip_client
from mpu.someip import protocol
from mpu.someip import transport as someip_transport
from mpu.someip.sd import SdServer

SERVICE_ID = 0x1234
INSTANCE_ID = 0x5678
SD_PORT = 30490
HOST = "127.0.0.1"
MULTICAST = "224.224.224.245"


def main() -> int:
    parser = argparse.ArgumentParser(description="PC 시뮬레이션 통합 검증")
    parser.add_argument("sim_bin", help="sd_host_sim 실행 파일 경로")
    parser.add_argument("--timeout", type=float, default=25.0,
                        help="시뮬레이터 전체 타임아웃 (초)")
    args = parser.parse_args()

    sim = Path(args.sim_bin).resolve()
    if not sim.exists():
        print(f"[FAIL] 시뮬레이터 바이너리 없음: {sim}", file=sys.stderr)
        return 1

    failures = 0

    # 1. MPU SdServer: OfferService 멀티캐스트 광고 (실제 코드)
    sd_server = SdServer(SERVICE_ID, INSTANCE_ID, HOST, SD_PORT,
                         multicast=MULTICAST, sd_port=SD_PORT, period_ms=1000)
    sd_server.start()
    print("[MPU] SdServer 시작: OfferService 광고 (multicast "
          f"{MULTICAST}:{SD_PORT})")

    # 2. MPU client: Notify/Heartbeat 주기 전송 스레드 (실제 코드)
    stop_events = threading.Event()

    def event_sender():
        tx = someip_transport.UdpTransport(HOST, SD_PORT)
        session = 1
        while not stop_events.is_set():
            level = 30 + (session % 70)  # 30..99 시뮬레이션 값
            someip_client.send_notify(level, tx, session)
            someip_client.send_heartbeat(session, tx, session)
            session = (session % 0xFFFF) + 1
            stop_events.wait(0.5)

    sender = threading.Thread(target=event_sender, daemon=True,
                              name="mpu-event-sender")
    sender.start()
    print("[MPU] client 이벤트 전송 시작: Notify/Heartbeat (UDP "
          f"{HOST}:{SD_PORT})")

    try:
        # 3. MCU 시뮬레이터 실행 (실제 someip_sd.cpp 코드)
        deadline = time.monotonic() + args.timeout
        print(f"[SIM] 실행: {sim.name} (timeout {args.timeout:.0f}s)")
        proc = subprocess.run([str(sim)], capture_output=True, text=True,
                              encoding="utf-8", errors="replace",
                              timeout=args.timeout)
        elapsed = time.monotonic() + args.timeout - deadline
        print(proc.stdout)

        if proc.returncode != 0:
            print(f"[FAIL] 시뮬레이터 종료 코드 {proc.returncode}")
            if proc.stderr:
                print(proc.stderr, file=sys.stderr)
            failures += 1
        else:
            print(f"[OK] 시뮬레이터 성공 ({elapsed:.1f}s)")
    except subprocess.TimeoutExpired:
        print(f"[FAIL] 시뮬레이터 타임아웃 ({args.timeout:.0f}s)")
        failures += 1
    finally:
        stop_events.set()
        sd_server.stop()

    # 4. MPU 이벤트가 실제 도달했는지 검증은 시뮬레이터 내부 PASS 로그로 대체
    if failures == 0:
        print("ALL PC SIMULATION TESTS PASSED")
        return 0
    print(f"{failures} FAILURE(S)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
