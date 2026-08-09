# Wireshark SOME/IP 캡처 가이드

## 캡처 파일 (현재 상태)

- `live_capture.pcapng` - `docker compose --profile debug up`으로 생성되는 실시간 캡처 (PC 시뮬레이션, UDP 30490)
- `someip_drowsiness.pcap` - **샘플 캡처는 아직 포함되어 있지 않습니다.** 실보드 검증 완료 후 `wireshark/capture_someip.py`로 생성해 추가 예정

## Wireshark 설정 (중요 - SOME/IP dissector 활성화)

1.  Wireshark 설치 (https://www.wireshark.org)
2.  `Edit → Preferences → Protocols → SOME/IP` 에서
    - `SOME/IP header` 활성화
3.  필터:
    ```
    someip                    # 모든 SOME/IP
    someip.serviceid == 0x1234  # 우리 서비스만
    udp.port == 30490         # SOME/IP 데이터
    ```

## 예상 캡처 시퀀스 (정상 - PC 시뮬레이션 UDP 30490)

Service Discovery(OfferService/SubscribeEventgroup)는 아직 미구현(Phase 2)이므로 SD 패킷은 잡히지 않는 것이 정상입니다.

```
No. Time      Source        Destination   Protocol  Info
1   0.000000  192.168.7.1   192.168.7.2   SOME/IP    Notify (0x1234.0x5678.0x8001) Data: 55 (85%)
2   0.050000  192.168.7.1   192.168.7.2   SOME/IP    Heartbeat (0x1234.0x5678.0x8002)
3   0.100000  192.168.7.1   192.168.7.2   SOME/IP    Heartbeat (0x1234.0x5678.0x8002)
...
```

## 스크린샷 (예정: docs/wireshark_someip.png)

Wireshark에서 패킷을 클릭하면:
- `Service ID: 0x1234 (DriverMonitoringService)`
- `Method ID: 0x8001 (NotifyDrowsiness)`
- `Message Type: Notification (0x02)`
- `Payload: 0x55 (85%)`

이 화면을 GitHub README에 첨부하면 독일 팀장은 "아, 이 친구는 CANoe 없이도 SOME/IP를 검증할 줄 아네"라고 판단합니다.

## CAN 덤프 동시 캡처

```bash
# 터미널 1: SOME/IP 캡처
tshark -i any -f "udp port 30490" -w wireshark/live_capture.pcapng

# 터미널 2: CAN 캡처
candump -L can0 > docs/candump_sim.log &

# 터미널 3: AI 트리거
python3 mpu/ai/trigger_test.py --level 85

# 결과: SOME/IP Notify 후 CAN 프레임이 나가는 것을 확인 (지연 수치는 실보드 검증 후 갱신 예정)
```

## 캡처 생성 방법 (UNO Q 없이 PC에서)

```bash
# PC에서 trigger_test.py로 SOME/IP 패킷 생성 가능
python3 mpu/ai/trigger_test.py --level 85 &
sudo tshark -i lo -f "udp port 30490" -w wireshark/test.pcapng &
# 5초 후 Ctrl+C, Wireshark로 test.pcapng 열기
```
