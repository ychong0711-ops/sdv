# vsomeip 3.4.10 - SOME/IP (AUTOSAR Adaptive) - COVESA
# 독일 CARIAD/MB.OS 실제 사용 스택과 동일
# Source: https://github.com/COVESA/vsomeip

SUMMARY = "vsomeip - SOME/IP stack (COVESA)"
DESCRIPTION = "COVESA vsomeip implementation of SOME/IP and SOME/IP-SD for AUTOSAR Adaptive"
HOMEPAGE = "https://github.com/COVESA/vsomeip"
LICENSE = "MPL-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=9741c346eef56131163e13b9db1241b3"

# GitHub가 git:// 프로토콜 중단 + Kirkstone git fetcher가 git://을 github.com/git/으로
# 잘못 변환하는 문제 회피: 고정 SRCREV의 GitHub archive tarball 직접 다운로드 (검증 완료)
SRC_URI = "https://github.com/COVESA/vsomeip/archive/02c199dff8aba814beebe3ca417fd991058fe90c.tar.gz"
SRC_URI[sha256sum] = "c05bcb4000ef90c44bd1a45278daff34a1a1cb70eb87d39275714901ccac4d89"

SRCREV = "02c199dff8aba814beebe3ca417fd991058fe90c"

S = "${WORKDIR}/vsomeip-${SRCREV}"

inherit cmake pkgconfig systemd

DEPENDS = "boost dlt-daemon"

# GTEST_ROOT=n/a: 테스트 빌드 비활성화 — CMakeLists가 googletest
# FetchContent(네트워크)와 google benchmark 요구를 건너뜀.
EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_SIGNAL_HANDLING=1 \
    -DDIAGNOSIS_ADDRESS=0x10 \
    -DENABLE_COMPAT=OFF \
    -DGTEST_ROOT=n/a \
"

# vsomeip 설정 파일
SRC_URI += "file://vsomeip.json \
            file://vsomeip.service"

SYSTEMD_SERVICE:${PN} = "vsomeipd.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install:append() {
    install -d ${D}${sysconfdir}/vsomeip
    install -m 0644 ${WORKDIR}/vsomeip.json ${D}${sysconfdir}/vsomeip/vsomeip.json

    # vsomeip 3.4.10의 라우팅 매니저 데몬은 routingmanagerd 이름으로 빌드됨.
    # systemd 서비스(vsomeipd.service)가 /usr/bin/vsomeipd를 참조하므로 복사 설치.
    install -d ${D}${bindir}
    install -m 0755 ${B}/examples/routingmanagerd/routingmanagerd ${D}${bindir}/vsomeipd

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/vsomeip.service ${D}${systemd_system_unitdir}/vsomeipd.service

    # Wireshark dissector용 심볼릭 링크
    install -d ${D}${datadir}/vsomeip
}

FILES:${PN} += " \
    ${bindir}/vsomeipd \
    ${sysconfdir}/vsomeip \
    ${systemd_system_unitdir}/vsomeipd.service \
    ${datadir}/vsomeip \
    ${prefix}/etc/vsomeip \
"

# 독일 자동차: vsomeip는 QNX/Linux HPC에서 필수, 본 레시피로 Yocto에 포함 증명
