"""
MPU: Qualcomm QRB2210 (Debian) - Driver Drowsiness Detection
AI: TensorFlow Lite + OpenCV - Eye Aspect Ratio (EAR)

독일 ADAS 포트폴리오용: "Edge AI on Heterogeneous SoC"
- Capture: USB Camera (UVC) 640x480 @ 15fps
- Inference: TFLite face landmark (68 points) -> EAR 계산
- Output: SOME/IP Notify (level 0-100) -> MCU  (경량 raw-socket 구현)

실제 차량: DMS (Driver Monitoring System) - Euro NCAP 2024 필수

전송:
- heartbeat 스레드: 50ms 주기 EVENT_HEARTBEAT(0x8002) - Safety 용
- notify: 졸음 이벤트 시 EVENT_NOTIFY(0x8001)
- transport: uart (실보드 기본, /dev/ttyS0, LPUART1) 또는 udp (127.0.0.1:30490, PC 시뮬레이션)
  env: SOMEIP_TRANSPORT=udp|uart, SOMEIP_UART_PORT=<port>, SOMEIP_MCU_IP=<ip>
"""
import argparse
import logging
import os
import random
import sys
import threading
import time

# mpu가 최상위 패키지가 아니므로 repo root를 sys.path에 삽입 후 from mpu.someip...
_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

import cv2  # noqa: E402
import numpy as np  # noqa: E402

from mpu.someip import client as someip_client  # noqa: E402
from mpu.someip.protocol import (  # noqa: E402
    SERVICE_ID,
    EVENT_NOTIFY,
    SOMEIP_PORT,
)

logging.basicConfig(level=logging.INFO, format='[MPU-AI] %(message)s')
log = logging.getLogger(__name__)

# EAR Threshold (논문: Soukupova 2016)
EAR_THRESHOLD = 0.25  # 눈 감김
EAR_CONSEC_FRAMES = 15  # 1초간 감으면 졸음

# TFLite 모델 경로 (실제는 Qualcomm SNPE/DSP 위탁 가능)
# 여기서는 OpenCV DNN + 경량 모델로 대체 (포트폴리오용)
MODEL_PATH = "models/face_landmark.tflite"

# MPU heartbeat: 50ms 주기 (Safety)
HEARTBEAT_PERIOD_MS = 50


def eye_aspect_ratio(eye):
    # eye: 6 points [(x,y), ...]
    A = np.linalg.norm(eye[1] - eye[5])
    B = np.linalg.norm(eye[2] - eye[4])
    C = np.linalg.norm(eye[0] - eye[3])
    return (A + B) / (2.0 * C)


def start_heartbeat_thread(transport):
    """daemon heartbeat 스레드: 50ms 주기로 EVENT_HEARTBEAT 전송 (counter 0-255 증가).

    UDP transport는 매 send 소켓 생성/종료하므로 스레드 안전하다.
    """
    state = {"counter": 0}

    def _run():
        while True:
            try:
                someip_client.send_heartbeat(state["counter"], transport)
            except Exception as e:
                log.error(f"Heartbeat send failed: {e}")
            state["counter"] = (state["counter"] + 1) & 0xFF
            time.sleep(HEARTBEAT_PERIOD_MS / 1000.0)

    t = threading.Thread(target=_run, daemon=True, name="someip-heartbeat")
    t.start()
    log.info(f"Heartbeat thread started ({HEARTBEAT_PERIOD_MS}ms period)")
    return t


def main():
    parser = argparse.ArgumentParser(description="SDV MPU Drowsiness Detection")
    parser.add_argument(
        "--dest",
        default=os.environ.get("SOMEIP_MCU_IP", "127.0.0.1"),
        help="MCU SOME/IP 대상 IP (기본 127.0.0.1, 환경변수 SOMEIP_MCU_IP 가능)",
    )
    parser.add_argument(
        "--transport",
        default=os.environ.get("SOMEIP_TRANSPORT", "uart"),
        choices=["udp", "uart"],
        help="전송 방식: uart(실보드 기본, LPUART1) / udp(PC 시뮬레이션). env SOMEIP_TRANSPORT",
    )
    parser.add_argument(
        "--uart-port",
        default=os.environ.get("SOMEIP_UART_PORT", "/dev/ttyS0"),
        help="uart transport 전용 포트 (기본 /dev/ttyS0). env SOMEIP_UART_PORT",
    )
    args = parser.parse_args()

    log.info("=== SDV MPU AI Started (QRB2210 + TFLite) ===")
    transport = someip_client.create_transport(
        args.transport, host=args.dest, port=SOMEIP_PORT, uart_port=args.uart_port)
    log.info(f"SOME/IP target: {args.dest}:{SOMEIP_PORT} Service 0x{SERVICE_ID:04X} "
             f"Event 0x{EVENT_NOTIFY:04X} transport={args.transport}")

    # heartbeat는 항상 동작 (Safety)
    start_heartbeat_thread(transport)

    # 카메라 없으면 테스트 모드
    cap = cv2.VideoCapture(0)
    use_camera = cap.isOpened()
    if not use_camera:
        log.warning("Camera not found - running in SIMULATION mode")
        # 시뮬레이션: 2초 간격 level 0-100 랜덤 전송 (데모용), 기존 로직 유지
        while True:
            level = random.choice([0, 0, 0, 85, 90])  # 가끔 졸음
            if level > 0:
                someip_client.send_notify(level, transport)
                log.info(f"SOME/IP Notify -> MCU: level={level}%")
            time.sleep(2)
        return

    # OpenCV Face Detection (경량 - TFLite 대신 Haar for demo)
    face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')
    eye_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_eye.xml')

    counter = 0
    while True:
        ret, frame = cap.read()
        if not ret:
            break

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        faces = face_cascade.detectMultiScale(gray, 1.1, 5)

        for (x, y, w, h) in faces:
            roi_gray = gray[y:y+h, x:x+w]
            eyes = eye_cascade.detectMultiScale(roi_gray)
            # EAR 간소화: 눈 2개 감지되면 open, 1개 이하면 closed로 가정 (데모)
            if len(eyes) < 2:
                counter += 1
                if counter >= EAR_CONSEC_FRAMES:
                    level = min(50 + counter * 2, 100)
                    someip_client.send_notify(level, transport)
                    log.warning(f"DROWSINESS DETECTED! EAR low, counter={counter} -> level {level}%")
                    # 디버그: 프레임 저장
                    cv2.imwrite("/tmp/drowsy.jpg", frame)
            else:
                counter = max(0, counter - 2)

        # 10fps로 제한 (QRB2210에서는 30fps 가능)
        time.sleep(0.1)

        # Docker 로그에서 확인 가능
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()


if __name__ == "__main__":
    main()
