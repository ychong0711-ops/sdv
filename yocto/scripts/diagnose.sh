#!/bin/bash
# diagnose.sh - Yocto 빌드 전 30초 자가진단
# Usage: ./yocto/scripts/diagnose.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=== SDV Yocto Diagnose (Kirkstone) ==="
echo "Date: $(date) | Host: $(uname -a)"
echo ""

FAIL=0

# 1. Disk
FREE_GB=$(df -BG . | awk 'NR==2 {print $4}' | sed 's/G//')
echo -n "[DISK] Free: ${FREE_GB}GB (need 100GB) ... "
if [ "$FREE_GB" -lt 50 ]; then
  echo -e "${RED}FAIL${NC} - Disk full! Run: bitbake -c cleanall"
  FAIL=1
elif [ "$FREE_GB" -lt 100 ]; then
  echo -e "${YELLOW}WARN${NC} - Low disk, but OK for incremental build"
else
  echo -e "${GREEN}OK${NC}"
fi

# 2. RAM
RAM_GB=$(free -g | awk '/Mem:/ {print $2}')
echo -n "[RAM] Total: ${RAM_GB}GB (need 16GB, min 8GB) ... "
if [ "$RAM_GB" -lt 8 ]; then
  echo -e "${RED}FAIL${NC} - RAM too low, set BB_NUMBER_THREADS=1"
  FAIL=1
elif [ "$RAM_GB" -lt 16 ]; then
  echo -e "${YELLOW}WARN${NC} - Set BB_NUMBER_THREADS=2 in local.conf"
  echo "      Fix: echo 'BB_NUMBER_THREADS = \"2\"' >> yocto/build/conf/local.conf"
else
  echo -e "${GREEN}OK${NC}"
fi

# 3. Docker
echo -n "[DOCKER] ... "
if command -v docker &>/dev/null; then
  echo -e "${GREEN}OK${NC} ($(docker --version | cut -d' ' -f3))"
else
  echo -e "${RED}FAIL${NC} - Docker not found (needed for sdv-yocto:4.0)"
  FAIL=1
fi

# 4. Locale
echo -n "[LOCALE] ... "
if locale | grep -q "en_US.UTF-8"; then
  echo -e "${GREEN}OK${NC}"
else
  echo -e "${YELLOW}WARN${NC} - Run: sudo locale-gen en_US.UTF-8"
fi

# 5. bblayers.conf
echo -n "[BBLAYERS] meta-sdv ... "
if [ -f "yocto/build/conf/bblayers.conf" ]; then
  if grep -q "meta-sdv" yocto/build/conf/bblayers.conf; then
    echo -e "${GREEN}OK${NC}"
  else
    echo -e "${RED}FAIL${NC} - meta-sdv missing"
    echo "      Fix: ./yocto/scripts/fix-build.sh"
    FAIL=1
  fi
else
  echo -e "${YELLOW}WARN${NC} - build/conf not initialized (run build-yocto.sh)"
fi

# 6. meta-sdv files
echo -n "[META-SDV] files ... "
if [ -f "yocto/meta-sdv/recipes-support/vsomeip/vsomeip_3.4.10.bb" ] && \
   [ -f "yocto/meta-sdv/recipes-app/sdv-mpu/sdv-mpu_1.0.bb" ] && \
   [ -f "yocto/meta-sdv/recipes-core/images/sdv-hpc-image.bb" ]; then
  echo -e "${GREEN}OK${NC}"
else
  echo -e "${RED}FAIL${NC} - meta-sdv files missing"
  FAIL=1
fi

# 7. vsomeip SRCREV
echo -n "[VSOMEIP] SRCREV ... "
if grep -q "SRCREV" yocto/meta-sdv/recipes-support/vsomeip/vsomeip_3.4.10.bb; then
  echo -e "${GREEN}OK${NC}"
else
  echo -e "${YELLOW}WARN${NC}"
fi

echo ""
if [ $FAIL -eq 0 ]; then
  echo -e "${GREEN}=== Diagnose PASSED - Ready to build ===${NC}"
  echo "Run: ./yocto/scripts/build-yocto.sh"
else
  echo -e "${RED}=== Diagnose FAILED - Fix above before build ===${NC}"
  echo "Run: ./yocto/scripts/fix-build.sh"
fi
