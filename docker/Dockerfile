# MPU: Qualcomm QRB2210 - Debian Linux + SOME/IP + AI
# Base: Debian 12 (bookworm) - 동일 이미지가 UNO Q 공식 Linux 이미지와 호환
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV VSOMEIP_CONFIGURATION=/app/config/vsomeip.json
ENV LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# 1. 시스템 의존성 + vsomeip 빌드 의존성
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config \
    libboost-system-dev libboost-thread-dev libboost-log-dev libboost-filesystem-dev \
    libpython3-dev python3 python3-pip python3-dev \
    libopencv-dev \
    can-utils iproute2 net-tools \
    tshark \
    && rm -rf /var/lib/apt/lists/*

# 2. vsomeip 3.4.10 빌드 (SOME/IP - AUTOSAR Adaptive 표준)
#    독일 CARIAD/MB.OS가 실제 사용하는 스택과 동일
#    주의: 현재 Python 클라이언트는 raw-socket 경량 구현을 사용하므로
#    vsomeip C++ 라이브러리는 Phase 2 통합(vsomeip-python binding)용으로만 빌드됨
#    -Wno-error 대신 소스 CMakeLists의 -Werror 제거: vsomeip 3.4.10은
#    add_compile_options(-Werror)로 직접 추가해 명령줄 플래그로 덮을 수 없음.
#    (2023-11 릴리스 코드 vs GCC 12+(bookworm) 경고 호환 문제 - 알려진 이슈)
WORKDIR /tmp
RUN git clone --depth 1 --branch 3.4.10 https://github.com/COVESA/vsomeip.git && \
    sed -i 's/-Werror[^ ]*//g' vsomeip/CMakeLists.txt && \
    mkdir vsomeip/build && cd vsomeip/build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_SIGNAL_HANDLING=1 .. && \
    make -j$(nproc) && make install && ldconfig && \
    rm -rf /tmp/vsomeip

# 3. Python 의존성 (AI + SOME/IP Python 바인딩)
# 주의: 빌드 컨텍스트는 프로젝트 루트 (docker-compose의 context: .. 과 일치)
COPY docker/requirements.txt /tmp/requirements.txt
RUN pip3 install --no-cache-dir --break-system-packages -r /tmp/requirements.txt

# 4. 앱 복사
WORKDIR /app
COPY config/ ./config/
COPY mpu/ai/ ./mpu/ai/
COPY mpu/someip/ ./mpu/someip/
COPY scripts/ ./scripts/

# SOME/IP 설정: UDP 30490 (PC 시뮬레이션 기본값)
# vsomeip.json은 Wireshark dissector와 호환 (port 30490) - 참조용 설정, 현재 클라이언트는 미사용
RUN mkdir -p /app/logs

# 헬스체크: AI 프로세스 (drowsiness.py) 생존 확인
HEALTHCHECK --interval=30s --timeout=5s --retries=3 \
    CMD pgrep -f drowsiness.py || exit 1

# 실행: AI + SOME/IP client (raw-socket 경량 구현)
CMD ["bash", "scripts/entrypoint.sh"]
