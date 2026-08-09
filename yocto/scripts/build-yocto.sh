#!/bin/bash
set -e
# Yocto Phase 2 빌드 스크립트 - UNO Q/Debian -> Yocto 이식
# Host에서 실행: ./yocto/scripts/build-yocto.sh
# Docker에서 실행: docker exec sdv-yocto-build bitbake sdv-hpc-image

YOCTO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$YOCTO_DIR/build"

echo "=== SDV Yocto Build (Kirkstone) ==="
echo "YOCTO_DIR: $YOCTO_DIR"

# Docker 빌드 (최초 1회)
if ! docker image inspect sdv-yocto:4.0 &>/dev/null; then
  echo "[1/4] Building Yocto Docker image (5-10분, 최초 1회)..."
  docker build -f "$YOCTO_DIR/Dockerfile.yocto" -t sdv-yocto:4.0 "$YOCTO_DIR"
else
  echo "[1/4] Docker image exists, skip build"
fi

# 빌드 디렉토리 준비
mkdir -p "$BUILD_DIR/sstate-cache" "$BUILD_DIR/downloads"

# Yocto 빌드 실행
echo "[2/4] Starting Yocto build container..."
docker run --rm -it \
  -v "$YOCTO_DIR/meta-sdv:/home/yocto/yocto/meta-sdv:ro" \
  -v "$BUILD_DIR/sstate-cache:/home/yocto/yocto/build/sstate-cache" \
  -v "$BUILD_DIR/downloads:/home/yocto/yocto/build/downloads" \
  -v "$BUILD_DIR/conf:/home/yocto/yocto/build/conf" \
  sdv-yocto:4.0 bash -c "
    set -e
    source poky/oe-init-build-env build

    echo '[3/4] Bitbake sdv-hpc-image (최초 빌드 1-3시간, sstate hit 시 10분)...'
    # QEMU용 (PC에서 테스트) - UNO Q QRB2210은 qemuarm64로 에뮬레이션
    # 실제 UNO Q에 플래시하려면 MACHINE=raspberrypi4-64 또는 qcom-arm64
    export MACHINE=qemuarm64
    bitbake sdv-hpc-image

    echo '[4/4] Build complete!'
    ls -lh tmp/deploy/images/qemuarm64/sdv-hpc-image*.wic* 2>/dev/null || ls -lh tmp/deploy/images/qemuarm64/ | head -20
  "

echo ""
echo "=== Build Artifacts ==="
ls -lh "$BUILD_DIR"/tmp/deploy/images/qemuarm64/sdv-hpc-image* 2>/dev/null || echo "Check $BUILD_DIR"
echo ""
echo "QEMU 실행:"
echo "  runqemu qemuarm64 sdv-hpc-image nographic"
echo ""
echo "WIC 이미지 플래시 (SD카드):"
echo "  sudo dd if=build/tmp/deploy/images/qemuarm64/sdv-hpc-image-qemuarm64.wic of=/dev/sdX bs=4M status=progress"
