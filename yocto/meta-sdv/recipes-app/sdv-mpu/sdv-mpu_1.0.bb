# sdv-mpu - SDV HPC AI + SOME/IP Client (Python + TFLite)
# Phase 1의 mpu/ai/drowsiness.py를 Yocto 이미지로 패키징
# 독일 SDV: Adaptive Application (ARA::COM) - 본 레시피가 그 역할

SUMMARY = "SDV MPU - Driver Monitoring AI + SOME/IP Client"
DESCRIPTION = "Heterogeneous SDV MPU Application: TFLite Drowsiness Detection + vsomeip Notify"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Phase 1 소스 재사용 (yocto 빌드 시 ../mpu를 복사)
# 실제 빌드에서는 git SRC_URI 사용 권장, 여기서는 file://로 간소화
SRC_URI = " \
    file://drowsiness.py \
    file://trigger_test.py \
    file://sdv-mpu.service \
"

S = "${WORKDIR}"

RDEPENDS:${PN} = " \
    python3-core \
    python3-opencv \
    python3-numpy \
    python3-pillow \
    vsomeip \
    can-utils \
"

inherit systemd

SYSTEMD_SERVICE:${PN} = "sdv-mpu.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
    # AI 앱
    install -d ${D}${bindir}/sdv-mpu
    install -m 0755 ${WORKDIR}/drowsiness.py ${D}${bindir}/sdv-mpu/
    install -m 0755 ${WORKDIR}/trigger_test.py ${D}${bindir}/sdv-mpu/

    # systemd
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/sdv-mpu.service ${D}${systemd_system_unitdir}/
}

FILES:${PN} += " \
    ${bindir}/sdv-mpu \
"

# 독일 면접 포인트: "Adaptive Application을 Yocto 레시피로 패키징, systemd로 관리, vsomeip와 연동"
