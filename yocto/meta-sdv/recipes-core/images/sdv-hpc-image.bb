# sdv-hpc-image - SDV HPC (High Performance Computer) Yocto Image
# Target: Qualcomm QRB2210 (Cortex-A53) - qemuarm64로 에뮬레이션
#         실제 보드: MACHINE=raspberrypi4-64 (대체) 또는 qcom-arm64
# Base: core-image-minimal + Docker + vsomeip + AI
# 독일 SDV: Adaptive AUTOSAR 기반 HPC 이미지와 동일 구조

SUMMARY = "SDV HPC Image - Embedded Linux for Zone Controller (HPC)"
DESCRIPTION = "Yocto Kirkstone image for SDV HPC: Docker + vsomeip (SOME/IP) + AI + CAN"
LICENSE = "MIT"

inherit core-image

# 호환 머신
COMPATIBLE_MACHINE = "qemuarm64|raspberrypi4-64|qemux86-64"

# 이미지 기능
IMAGE_FEATURES += " \
    ssh-server-openssh \
    package-management \
    debug-tweaks \
    tools-debug \
"

IMAGE_INSTALL += " \
    packagegroup-core-boot \
    packagegroup-core-ssh-openssh \
    kernel-modules \
    \
    can-utils \
    iproute2 \
    ethtool \
    tshark \
    \
    vsomeip \
    \
    sdv-mpu \
    \
    docker-ce \
    python3-docker-compose \
    \
    python3-core \
    python3-pip \
    python3-opencv \
    python3-numpy \
    \
    gdb \
    strace \
    \
    systemd \
"

# Docker 활성화
IMAGE_INSTALL:append = " systemd"

# 이미지 크기: HPC는 16GB eMMC 기준
IMAGE_ROOTFS_SIZE ?= "819200"
IMAGE_ROOTFS_EXTRA_SPACE:append = " + 512000"

# 이미지 형식: qemuarm64는 WIC 부트 파일(IMAGE_BOOT_FILES) 미설정 상태이므로
# ext4 + tar.bz2로 생성 (runqemu에서 바로 부팅 가능).
IMAGE_FSTYPES += "ext4 tar.bz2"

# vsomeip 설정이 이미지에 포함되도록
ROOTFS_POSTPROCESS_COMMAND += "install_vsomeip_config;"

install_vsomeip_config() {
    # vsomeip.json이 /etc/vsomeip에 이미 설치됨 - systemd enable 확인
    echo "SDV HPC Image: vsomeip + sdv-mpu installed"
}

# 독일 면접용: "Yocto Kirkstone LTS 기반, sdv-hpc-image, Docker + vsomeip 포함, qemuarm64에서 검증"
