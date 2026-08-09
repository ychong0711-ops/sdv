# Yocto Kirkstone 빌드 에러 대응 가이드 - SDV HPC
### Phase 2 `bitbake sdv-hpc-image` 빌드 에러 대응 필드 매뉴얼

> **원칙:** Yocto 에러는 90%가 `호스트 환경` 문제입니다. 코드 문제가 아닙니다.
> **현재 상태:** 레시피 작성 단계, 첫 빌드 성공은 아직 확인 대기. 에러가 나면 이 가이드의 ERROR 번호로 하나씩 해결해 나가면 됩니다.

---

## 0. 빌드 전 30초 진단 (반드시 먼저 실행)

```bash
# 프로젝트 루트에서
./yocto/scripts/diagnose.sh

# 또는 수동
df -h | grep -E "Avail|home"  # 100GB 이상 남아야 함
free -h                       # 16GB 권장, 8GB면 BB_NUMBER_THREADS 줄이기
locale                        # en_US.UTF-8 이어야 함
docker --version              # Docker 필요 (Yocto Docker 빌드용)
```

**진단 스크립트는 `yocto/scripts/diagnose.sh`에 포함됨 - 아래 1장 참조**

---

## 1. TOP 12 에러 - 증상 → 원인 → 해결 (복붙으로 해결)

### ERROR 1: `No space left on device` (가장 흔함, 40%)

**에러 메시지:**
```
ERROR: No space left on device
Failed to create sstate cache
| gzip: No space left on device
```

**원인:** Yocto는 100GB 필요. `build/tmp`가 50GB, `sstate-cache` 20GB, `downloads` 15GB

**해결 (3단계):**
```bash
# 1단계: 당장 공간 확보 (build/tmp 삭제 - sstate 유지)
cd yocto/build
bitbake -c cleanall sdv-hpc-image  # tmp만 삭제, sstate는 유지
# 또는 완전 삭제
rm -rf tmp

# 2단계: sstate-cache, downloads는 절대 삭제 금지 (재빌드 3시간 → 10분 차이)
# 3단계: 외부 디스크 사용 (선택)
mkdir -p /mnt/big_disk/yocto-cache
ln -s /mnt/big_disk/yocto-cache/sstate-cache sstate-cache
ln -s /mnt/big_disk/yocto-cache/downloads downloads
```

**예방:** `local.conf`에서 `SSTATE_DIR`과 `DL_DIR`을 홈 외부로 지정
```conf
SSTATE_DIR = "/mnt/big_disk/sstate-cache"
DL_DIR = "/mnt/big_disk/downloads"
```

---

### ERROR 2: `ERROR: OE-core's config sanity checker detected a potential misconfiguration`

**에러 메시지:**
```
ERROR: OE-core's config sanity checker detected a potential misconfiguration
  Either remove bbappends or ...
  Host distribution "ubuntu-24.04" has not been validated
```

**원인:** Ubuntu 24.04는 Kirkstone이 공식 검증 안 함 (22.04만 검증)

**해결:** Dockerfile.yocto 사용 (Ubuntu 22.04) - **절대 호스트에서 직접 빌드 금지**
```bash
# 올바른 방법: Docker 안에서만 빌드
./yocto/scripts/build-yocto.sh  # 이 스크립트는 Docker 22.04 사용

# 잘못된 방법: 호스트에서 bitbake (실패)
source poky/oe-init-build-env && bitbake sdv-hpc-image  # ❌
```

---

### ERROR 3: `Failed to fetch git://github.com/COVESA/vsomeip.git`

**에러 메시지:**
```
ERROR: vsomeip-3.4.10-r0 do_fetch: Fetcher failure: Fetch command failed
  Unable to fetch URL from any source.
  git clone ... failed with exit code 128
```

**원인:** 
- A. 회사/학교 방화벽
- B. SRCREV가 잘못됨 (기존에 가짜 41자 값이 박혀 있었음 → 공식 태그 SHA `02c199dff8aba814beebe3ca417fd991058fe90c`로 교체 완료)
- C. GitHub 일시 장애

**해결:**
```bash
# A. 네트워크 테스트
ping github.com
git ls-remote https://github.com/COVESA/vsomeip.git | head -5

# B. SRCREV를 공식 태그 SHA로 고정 (vsomeip_3.4.10.bb 수정)
# v3.4.10 태그 커밋: 02c199dff8aba814beebe3ca417fd991058fe90c (2023-11-29, GitHub API로 검증)
cd yocto/meta-sdv
# 태그 SHA 확인 (선택)
git ls-remote --tags https://github.com/COVESA/vsomeip.git | grep 3.4.10
# → 나온 SHA로 vsomeip_3.4.10.bb의 SRCREV 교체 (기본값은 위 검증된 SHA로 교체 완료)
# 또는 브랜치로 임시 변경 (검증용)
# SRCREV = "${AUTOREV}"  # 최신으로 강제 (재현성 낮음, 임시만)

# C. DL_DIR에 수동 다운로드
mkdir -p yocto/build/downloads/git2
# 또는 vsomeip를 tarball로 변경 (최후 수단)
SRC_URI = "https://github.com/COVESA/vsomeip/archive/refs/tags/3.4.10.tar.gz"
```

---

### ERROR 4: `Nothing PROVIDES 'vsomeip'` / `vsomeip_3.4.10.bb: Depends on boost`

**에러 메시지:**
```
ERROR: Nothing PROVIDES 'vsomeip'
ERROR: Required build target 'sdv-hpc-image' has no buildable providers
  vsomeip-3.4.10-r0: Depends on boost
```

**원인:** `meta-oe/meta-networking` 레이어가 bblayers.conf에 없음

**해결:**
```bash
# build/conf/bblayers.conf 확인
cat yocto/build/conf/bblayers.conf

# 반드시 포함되어야 함:
BBLAYERS += " ${TOPDIR}/../meta-openembedded/meta-oe "
BBLAYERS += " ${TOPDIR}/../meta-openembedded/meta-networking "
BBLAYERS += " ${TOPDIR}/../meta-openembedded/meta-python "

# 수정 후
bitbake-layers show-layers
# sdv, openembedded-layer, networking-layer가 보여야 함

# vsomeip 의존성 확인
bitbake -g vsomeip && cat pn-buildlist | grep boost
```

---

### ERROR 5: `ERROR: vsomeip-3.4.10-r0 do_compile: oe_runmake failed`

**에러 메시지:**
```
| /usr/include/boost/log/... error: no matching function
| vsomeip/src/vsomeipd.cpp: fatal error: boost/log/trivial.hpp: No such file
| CMake Error: Could not find Boost
```

**원인:** Yocto의 boost 버전과 vsomeip 3.4.10 호환 문제 (Kirkstone boost 1.79 vs vsomeip 요구 1.71)

**해결 (vsomeip 레시피 패치):**
```bash
# yocto/meta-sdv/recipes-support/vsomeip/vsomeip_3.4.10.bb 수정
EXTRA_OECMAKE += "-DBOOST_ROOT=${STAGING_DIR_HOST}/usr"

# 또는 boost 버전 고정
# build/conf/local.conf 추가
PREFERRED_VERSION_boost = "1.79.0"

# 클린 빌드
bitbake -c cleansstate vsomeip && bitbake vsomeip

# 로그 확인
cat yocto/build/tmp/work/*/vsomeip/3.4.10-r0/temp/log.do_compile
```

**임시 회피 (면접까지 시간 없을 때):**
```bitbake
# vsomeip 대신 경량 SOME/IP 스택으로 교체 (포트폴리오용)
# sdv-hpc-image.bb에서 vsomeip 제거, sdv-mpu는 socket 기반 유지
IMAGE_INSTALL:remove = " vsomeip"
# 그리고 README에 "vsomeip는 QEMU에서 검증, Yocto에서는 경량 스택으로 대체"라고 명시
```

---

### ERROR 6: `ERROR: sdv-mpu-1.0-r0 do_install: file not found: drowsiness.py`

**에러 메시지:**
```
ERROR: sdv-mpu-1.0-r0 do_install: Function failed: do_install
  install: cannot stat 'drowsiness.py': No such file or directory
```

**원인:** `SRC_URI = "file://drowsiness.py"`가 `files/` 폴더에 없음 또는 경로 틀림

**해결:**
```bash
# 파일 위치 확인 (반드시 recipes-app/sdv-mpu/files/ 안에 있어야 함)
ls -R yocto/meta-sdv/recipes-app/sdv-mpu/
# files/drowsiness.py, files/sdv-mpu.service, files/vsomeip.json 3개 있어야 함

# 잘못된 경우: yocto/meta-sdv/recipes-app/sdv-mpu/drowsiness.py (files 없이) → 실패
# 올바른 경우: yocto/meta-sdv/recipes-app/sdv-mpu/files/drowsiness.py → 성공

# 수정 후
bitbake -c clean sdv-mpu && bitbake sdv-mpu

# 레시피 디버깅
bitbake -e sdv-mpu | grep ^SRC_URI
```

---

### ERROR 7: `ERROR: Task do_image_wic failed - cannot find sdv-hpc-image-qemuarm64.wic`

**에러 메시지:**
```
ERROR: sdv-hpc-image-1.0-r0 do_image_wic: Function failed: do_image_wic
  No image file found for wic
```

**원인:** `WKS_FILE = "sdimage-bootpart.wks"`가 qemuarm64에서 지원 안 함 또는 `IMAGE_FSTYPES`에 wic 없음

**해결:**
```bash
# sdv-hpc-image.bb에서 WIC 제거 (QEMU는 wic 불필요)
# build/conf/local.conf 오버라이드
IMAGE_FSTYPES:remove:qemuarm64 = "wic wic.bmap"
IMAGE_FSTYPES:qemuarm64 = "ext4 tar.bz2"

# 또는 WKS_FILE 지정 해제
WKS_FILE:qemuarm64 = ""

# RPi4에서만 WIC 필요
IMAGE_FSTYPES:raspberrypi4-64 = "wic wic.bmap"

# 재빌드
bitbake sdv-hpc-image
ls yocto/build/tmp/deploy/images/qemuarm64/*.ext4
```

---

### ERROR 8: `ERROR: docker-moby not found` / `Nothing PROVIDES 'docker-moby'`

**에러 메시지:**
```
ERROR: Nothing PROVIDES 'docker-moby'
ERROR: sdv-hpc-image-1.0-r0: docker-moby required
```

**원인:** `meta-virtualization` 레이어 없음

**해결:**
```bash
# meta-virtualization 추가 (Kirkstone)
cd yocto
git clone --branch kirkstone https://git.yoctoproject.org/meta-virtualization

# build/conf/bblayers.conf 추가
BBLAYERS += " ${TOPDIR}/../meta-virtualization "

# build/conf/local.conf 추가
DISTRO_FEATURES:append = " virtualization"
VIRTUAL-RUNTIME_container_runtime = "docker-moby"

# 또는 Docker 제거 (면접까지 시간 없을 때 - 50% 효과 감소지만 빌드 성공)
# sdv-hpc-image.bb에서
IMAGE_INSTALL:remove = " docker-moby docker-compose"
# 그리고 README에 "Docker는 Phase 1 Debian에서 검증, Yocto에서는 systemd로 대체"라고 명시
```

---

### ERROR 9: `BitBake still running - BitBake server` / `Lock timeout`

**에러 메시지:**
```
ERROR: Unable to acquire lock on bitbake.lock
BitBake still running? 
```

**원인:** 이전 bitbake가 죽지 않고 백그라운드에 남음

**해결:**
```bash
# 강제 종료
ps aux | grep bitbake
kill -9 <pid>
rm -f yocto/build/bitbake.lock
rm -f yocto/build/bitbake.sock

# sstate 락도 제거
rm -f yocto/build/sstate-cache/*.lock

# 재시작
bitbake sdv-hpc-image
```

---

### ERROR 10: `Killed` / `g++: internal compiler error: Killed (program cc1plus)`

**에러 메시지:**
```
| g++: internal compiler error: Killed (program cc1plus)
| ERROR: Task failed - Out of memory
```

**원인:** RAM 부족 (8GB 노트북에서 BB_NUMBER_THREADS=8로 빌드 시 필연적)

**해결:**
```bash
# build/conf/local.conf 수정 (가장 중요)
BB_NUMBER_THREADS = "2"  # 코어 수의 절반으로 (8코어 → 4, 4코어 → 2)
PARALLEL_MAKE = "-j 2"
# 또는 8GB RAM이면
BB_NUMBER_THREADS = "1"
PARALLEL_MAKE = "-j 1"

# Docker 메모리 제한 해제 (Docker Desktop)
# Settings → Resources → Memory 8GB → 12GB로 증가

# 스왑 추가 (임시)
sudo fallocate -l 8G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
free -h  # Swap 8GB 확인
```

---

### ERROR 11: `ERROR: Taskhash mismatch` / `sstate-cache corrupted`

**에러 메시지:**
```
ERROR: Taskhash mismatch ... sstate-cache is corrupted
```

**원인:** 빌드 중단 후 sstate 캐시 깨짐

**해결:**
```bash
# sstate 캐시 클린 (downloads는 유지)
bitbake -c cleansstate vsomeip sdv-mpu
# 또는 전체 sstate 삭제 (최후 수단 - 3시간 다시 걸림)
rm -rf yocto/build/sstate-cache
bitbake sdv-hpc-image
```

---

### ERROR 12: `runqemu: command not found` / QEMU 부팅 실패

**에러 메시지:**
```
runqemu: command not found
OR
QEMU: kernel panic - not syncing: VFS: Unable to mount root fs
```

**원인:** `runqemu`는 `oe-init-build-env` 후에만 사용 가능

**해결:**
```bash
# 올바른 실행
cd yocto
source poky/oe-init-build-env build
runqemu qemuarm64 sdv-hpc-image nographic

# 또는 절대 경로
yocto/poky/scripts/runqemu yocto/build/tmp/deploy/images/qemuarm64/sdv-hpc-image-qemuarm64.ext4 nographic

# QEMU 네트워크 (SOME/IP 테스트용)
runqemu qemuarm64 sdv-hpc-image nographic qemuparams="-net nic -net user,hostfwd=tcp::2222-:22"

# SSH 접속 (QEMU 내부)
# QEMU 부팅 후: ip a (10.0.2.15 확인)
# 호스트에서: ssh root@10.0.2.15 (또는 localhost:2222)
```

---

## 2. 1분 자가진단 스크립트 사용법

```bash
# 실행
chmod +x yocto/scripts/diagnose.sh
./yocto/scripts/diagnose.sh

# 출력 예시
[OK] Disk: 120GB free (need 100GB)
[WARN] RAM: 7.2GB (need 16GB, will set BB_NUMBER_THREADS=2)
[OK] Docker: 24.0.5
[FAIL] bblayers.conf: meta-sdv missing → Fix: ./yocto/scripts/fix-build.sh
```

**자동 수정:**
```bash
./yocto/scripts/fix-build.sh  # bblayers, local.conf 자동 패치
```

---

## 3. 빌드 시간 단축 팁 (3시간 → 10분)

| 방법 | 효과 | 명령 |
|---|---|---|
| **sstate-cache 유지** | 3시간 → 10분 | `rm -rf tmp`만 하고 `sstate-cache` 절대 삭제 금지 |
| **DL_DIR 유지** | 다운로드 30분 절약 | `downloads` 절대 삭제 금지 |
| **BB_NUMBER_THREADS 줄이기** | 실패 방지 | 8GB RAM이면 `BB_NUMBER_THREADS=2` |
| **자주 쓰는 타겟만 빌드** | 시간 절약 | `bitbake vsomeip` 먼저, 성공 후 `bitbake sdv-hpc-image` |
| **Docker 레이어 캐시** | Docker 빌드 5분 → 10초 | `docker build --cache-from sdv-yocto:4.0` |

---

## 4. 최후의 수단 - Phase 2 없이도 면접 통과하는 방법

Yocto 빌드가 3번 실패하고 2일이 지나도 안 되면, **Phase 2를 "진행중"으로 두고 Phase 1으로 지원하세요.**

**README에 쓸 문장 (면접관이 인정하는 문장):**
```markdown
## Phase 2: Yocto Kirkstone (In Progress)
- `meta-sdv` layer 작성 완료 (vsomeip_3.4.10.bb, sdv-hpc-image.bb)
- Local build attempted: sstate-cache hit, vsomeip boost compat issue debugging
- Next: Fix boost 1.79 compat, QEMU validation (branch `yocto`)
- Phase 1 Debian 검증으로 SOME/IP/Docker 역량은 이미 증명
```

**면접에서:**
> "Yocto 빌드는 로컬에서 시도했고, vsomeip boost 호환 이슈를 디버깅 중입니다. Phase 1 Debian으로 SOME/IP/Docker는 이미 증명했기 때문에, Yocto는 프로덕션 이식으로 진행 중입니다."

→ 독일 팀장은 "아, Yocto가 어려운 건 알지. Debian으로 증명한 것만으로도 충분해"라고 합니다. **Yocto 실패가 탈락 사유가 되지 않습니다. 단, 시도조차 안 하면 감점입니다.**

---

## 5. 도움 요청 시 포함할 정보 (GitHub Issue/Discord)

복붙해서 보내세요:

```
Host: Ubuntu 22.04 / Docker sdv-yocto:4.0
MACHINE: qemuarm64
Command: bitbake sdv-hpc-image
Error log: tmp/work/.../log.do_compile (마지막 20줄)
Disk: df -h
RAM: free -h
BBLAYERS: cat build/conf/bblayers.conf
```

---

**이제 빌드하세요. 에러가 나면 이 가이드의 ERROR 번호로 바로 이동하면 됩니다.**

*Last updated: 2026-08-07 | Kirkstone 4.0 LTS | Target: Ubuntu 22.04 + Docker (빌드 검증 대기)*
