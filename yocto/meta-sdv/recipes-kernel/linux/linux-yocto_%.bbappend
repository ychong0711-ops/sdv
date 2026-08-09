# linux-yocto 커널에 Docker 데몬 필수 커널 옵션 적용
FILESEXTRAPATHS:prepend := "${THISDIR}/linux-yocto:"

SRC_URI += "file://docker.cfg"
