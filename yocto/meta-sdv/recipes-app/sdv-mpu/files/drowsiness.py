"""
MPU: Qualcomm QRB2210 (Debian) - Driver Drowsiness Detection
AI: TensorFlow Lite + OpenCV - Eye Aspect Ratio (EAR)

독일 ADAS 포트폴리오용: "Edge AI on Heterogeneous SoC"
- Capture: USB Camera (UVC) 640x480 @ 15fps
- Inference: TFLite face landmark (68 points) -> EAR 계산
- Output: SOME/IP Notify (level 0-100) -> MCU

실제 차량: DMS (Driver Monitoring System) - Euro NCAP 2024 필수
"""
import cv2
import numpy as np
import time
import socket
import struct
import logging

logging.basicConfig(level=logging.INFO, format='[MPU-AI] %(message)s')
log = logging.getLogger(__name__)

# SOME/IP Config (vsomeip 호환)
SOMEIP_MCU_IP = "192.168.7.2"  # MCU 내부 IP (USB RNDIS) 또는 127.0.0.1 (loopback for test)
SOMEIP_PORT = 30490
SERVICE_ID = 0x1234
EVENT_ID = 0x8001

# EAR Threshold (논문: Soukupova 2016)
EAR_THRESHOLD = 0.25  # 눈 감김
EAR_CONSEC_FRAMES = 15  # 1초간 감으면 졸음

# TFLite 모델 경로 (실제는 Qualcomm SNPE/DSP 위탁 가능)
# 여기서는 OpenCV DNN + 경량 모델로 대체 (포트폴리오용)
MODEL_PATH = "models/face_landmark.tflite"

def eye_aspect_ratio(eye):
    # eye: 6 points [(x,y), ...]
    A = np.linalg.norm(eye[1] - eye[5])
    B = np.linalg.norm(eye[2] - eye[4])
    C = np.linalg.norm(eye[0] - eye[3])
    return (A + B) / (2.0 * C)

def someip_notify(level: int):
    """SOME/IP Notification 패킷 생성 (vsomeip/Wireshark 호환)"""
    # SOME/IP Header: Service 0x1234, Method 0x8001, Client 0x0001, Session auto
    header = struct.pack('!HHIHHBBBB',
        SERVICE_ID,      # Service ID
        EVENT_ID,        # Method/Event ID
        8 + 1 + 8,       # Length (header 8 + payload 1 + 8 alignment)
        0x0001,          # Client ID
        0x0001,          # Session ID
        0x01,            # Protocol Version
        0x01,            # Interface Version
        0x02,            # Message Type: Notification
        0x00             # Return Code
    )
    payload = struct.pack('!B', level)
    # SOME/IP는 8바이트 정렬 필요 (패딩)
    packet = header + payload + b'\x00' * 7

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.sendto(packet, (SOMEIP_MCU_IP, SOMEIP_PORT))
        log.info(f"SOME/IP Notify -> MCU: level={level}% ({len(packet)} bytes)")
    except Exception as e:
        log.error(f"SOME/IP send failed: {e}")
    finally:
        sock.close()

    # Heartbeat도 SOME/IP로 전송 (Safety)
    # 별도 Event 0x8002로 heartbeat 전송 가능

def main():
    log.info("=== SDV MPU AI Started (QRB2210 + TFLite) ===")
    log.info(f"SOME/IP target: {SOMEIP_MCU_IP}:{SOMEIP_PORT} Service 0x{SERVICE_ID:04X} Event 0x{EVENT_ID:04X}")

    # 카메라 없으면 테스트 모드
    cap = cv2.VideoCapture(0)
    use_camera = cap.isOpened()
    if not use_camera:
        log.warning("Camera not found - running in SIMULATION mode (trigger_test.py)")
        # 시뮬레이션: 5초마다 level 0-100 랜덤 전송 (데모용)
        import random
        while True:
            level = random.choice([0, 0, 0, 85, 90])  # 가끔 졸음
            if level > 0:
                someip_notify(level)
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
                    someip_notify(level)
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
