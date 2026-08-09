#!/bin/bash
# fix-build.sh - Yocto 빌드 자동 수정 (진단 후 실행)
# Usage: ./yocto/scripts/fix-build.sh

set -e

echo "=== SDV Yocto Auto Fix ==="

# 1. RAM에 따라 local.conf 자동 패치
RAM_GB=$(free -g | awk '/Mem:/ {print $2}')
BUILD_CONF="yocto/build/conf/local.conf"

if [ -f "$BUILD_CONF" ]; then
  if [ "$RAM_GB" -lt 8 ]; then
    echo "[FIX] RAM ${RAM_GB}GB → BB_NUMBER_THREADS=1"
    sed -i 's/^BB_NUMBER_THREADS.*/BB_NUMBER_THREADS = "1"/' "$BUILD_CONF" || echo 'BB_NUMBER_THREADS = "1"' >> "$BUILD_CONF"
    sed -i 's/^PARALLEL_MAKE.*/PARALLEL_MAKE = "-j 1"/' "$BUILD_CONF" || echo 'PARALLEL_MAKE = "-j 1"' >> "$BUILD_CONF"
  elif [ "$RAM_GB" -lt 16 ]; then
    echo "[FIX] RAM ${RAM_GB}GB → BB_NUMBER_THREADS=2"
    sed -i 's/^BB_NUMBER_THREADS.*/BB_NUMBER_THREADS = "2"/' "$BUILD_CONF" || echo 'BB_NUMBER_THREADS = "2"' >> "$BUILD_CONF"
    sed -i 's/^PARALLEL_MAKE.*/PARALLEL_MAKE = "-j 2"/' "$BUILD_CONF" || echo 'PARALLEL_MAKE = "-j 2"' >> "$BUILD_CONF"
  fi
else
  echo "[WARN] $BUILD_CONF not found - run build-yocto.sh first"
fi

# 2. bblayers.conf에 meta-sdv 추가 (없으면)
BBLAYERS_CONF="yocto/build/conf/bblayers.conf"
if [ -f "$BBLAYERS_CONF" ]; then
  if ! grep -q "meta-sdv" "$BBLAYERS_CONF"; then
    echo "[FIX] Adding meta-sdv to bblayers.conf"
    cat >> "$BBLAYERS_CONF" << 'EOF'

# SDV custom layer (auto-added by fix-build.sh)
BBLAYERS += " ${TOPDIR}/../meta-sdv "
EOF
    echo "[OK] meta-sdv added"
  else
    echo "[OK] meta-sdv already in bblayers.conf"
  fi
fi

# 3. sstate-cache, downloads 심볼릭 링크 확인
if [ ! -d "yocto/build/sstate-cache" ]; then
  echo "[FIX] Creating sstate-cache dir"
  mkdir -p yocto/build/sstate-cache
fi
if [ ! -d "yocto/build/downloads" ]; then
  echo "[FIX] Creating downloads dir"
  mkdir -p yocto/build/downloads
fi

# 4. bitbake.lock 제거 (이전 빌드 잔재)
if [ -f "yocto/build/bitbake.lock" ]; then
  echo "[FIX] Removing stale bitbake.lock"
  rm -f yocto/build/bitbake.lock yocto/build/bitbake.sock
fi

echo ""
echo "=== Fix Complete ==="
echo "Next: ./yocto/scripts/diagnose.sh && ./yocto/scripts/build-yocto.sh"
