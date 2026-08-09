#!/bin/bash
set -e

# Yocto Build Host Entrypoint
# Usage: docker run -it --rm -v $(pwd)/yocto/meta-sdv:/home/yocto/yocto/meta-sdv sdv-yocto:4.0
# 항상 oe-init-build-env를 source한다 (README 분기에서도 bitbake PATH 필요).

echo "=== SDV Yocto Kirkstone Build Host ==="
echo "Poky: $(grep DISTRO_VERSION /home/yocto/yocto/poky/meta-poky/conf/distro/poky.conf | head -1)"
echo "User: $(whoami) | Workdir: $(pwd)"

# poky 환경 설정 (없으면 생성)
if [ ! -f "build/conf/bblayers.conf" ]; then
  echo "[INIT] Initializing Yocto build environment..."
  source poky/oe-init-build-env build
else
  echo "[READY] Build env exists - sourcing..."
  source poky/oe-init-build-env build
fi

# 필요한 레이어를 bblayers.conf에 추가 (중복 없이)
BBLAYERS_FILE="conf/bblayers.conf"

# meta-sdv + meta-openembedded 레이어 확인 및 추가
if [ -d "/home/yocto/yocto/meta-sdv" ]; then
  if ! grep -q "meta-sdv" "$BBLAYERS_FILE"; then
    echo "BBLAYERS += \" \${TOPDIR}/../meta-sdv \"" >> "$BBLAYERS_FILE"
    echo "  + meta-sdv added"
  fi
fi
for layer in meta-oe meta-networking meta-python meta-filesystems; do
  if [ -d "/home/yocto/yocto/meta-openembedded/$layer" ]; then
    if ! grep -q "$layer" "$BBLAYERS_FILE"; then
      echo "BBLAYERS += \" \${TOPDIR}/../meta-openembedded/$layer \"" >> "$BBLAYERS_FILE"
      echo "  + $layer added"
    fi
  fi
done
if [ -d "/home/yocto/yocto/meta-virtualization" ]; then
  if ! grep -q "meta-virtualization" "$BBLAYERS_FILE"; then
    echo "BBLAYERS += \" \${TOPDIR}/../meta-virtualization \"" >> "$BBLAYERS_FILE"
    echo "  + meta-virtualization added"
  fi
fi

# 전달된 명령 실행 (없으면 bash)
if [ $# -eq 0 ]; then
  exec bash
else
  exec "$@"
fi
