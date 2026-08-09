# Yocto Phase 2 - Debian → Yocto Kirkstone 이식 가이드

> **목표:** Phase 1의 `Debian + Docker (1분 빌드)`를 `Yocto Kirkstone (1시간 빌드)`로 이식하여 독일 면접에서 "Yocto 경험" 증명

> **현재 상태 (2026-08):** `meta-sdv` 레시피 작성 단계. vsomeip SRCREV는 공식 태그 **v3.4.10 (Commit `02c199d`, 2023-11-29, GitHub API로 검증)** 로 교체 완료. **빌드 검증은 대기 중** — 첫 빌드 성공 후 이 문서의 결과/증거 섹션을 갱신할 예정입니다.

---

## 1. 왜 Yocto인가? (면접 답변)

> "Phase 1에서 Debian으로 SOME/IP를 6주 만에 증명했습니다. 프로덕션 적합성을 위해 **Yocto Kirkstone LTS**로 이식 중입니다. sdv-hpc-image는 Docker + vsomeip를 포함한 Adaptive HPC 이미지입니다."

- Debian: 빠른 프로토타이핑 (Phase 1, 6주)
- Yocto: 프로덕션, 재현 가능성, 라이선스 관리, 10년 LTS (Phase 2, 독일이 원하는 것)

## 2. 구조

```
yocto/
├── Dockerfile.yocto              # Ubuntu 22.04 + Yocto 빌드 의존성 + poky kirkstone
├── kas.yml                       # kas 빌드 (선택, 없이도 가능)
├── meta-sdv/                     # ★ 커스텀 레이어 (GitHub에 이 폴더가 핵심)
│   ├── conf/layer.conf
│   ├── recipes-core/images/sdv-hpc-image.bb  # HPC 이미지 (Docker + vsomeip + AI)
│   ├── recipes-support/vsomeip/vsomeip_3.4.10.bb  # SOME/IP 스택
│   │   └── files/vsomeip.json, vsomeip.service
│   └── recipes-app/sdv-mpu/sdv-mpu_1.0.bb        # AI 앱 패키징
│       └── files/drowsiness.py, sdv-mpu.service
├── build/conf/
│   ├── bblayers.conf.sample
│   └── local.conf.sample
└── scripts/
    ├── build-yocto.sh            # 원클릭 빌드 (Docker + bitbake)
    └── entrypoint-yocto.sh
```

## 3. 빌드 방법 3가지 (쉬운 순)

### 방법 A: 원클릭 스크립트 (추천 - 한국에서)

```bash
# 프로젝트 루트에서
./yocto/scripts/build-yocto.sh

# 최초 1회: Docker 이미지 빌드 5분 + Yocto 빌드 1-3시간 (sstate 없이)
# 2회부터: 10분 (sstate-cache hit)

# 결과: yocto/build/tmp/deploy/images/qemuarm64/sdv-hpc-image-qemuarm64.wic.xz
# (첫 빌드 성공 확인 대기 - 성공 시 아래 경로에 산출물이 생깁니다)
ls -lh yocto/build/tmp/deploy/images/qemuarm64/
```

### 방법 B: Docker 직접 (수동)

```bash
docker build -f yocto/Dockerfile.yocto -t sdv-yocto:4.0 yocto/

docker run --rm -it \
  -v $(pwd)/yocto/meta-sdv:/home/yocto/yocto/meta-sdv:ro \
  -v $(pwd)/yocto/build:/home/yocto/yocto/build \
  sdv-yocto:4.0 bash

# 컨테이너 내부
source poky/oe-init-build-env build
MACHINE=qemuarm64 bitbake sdv-hpc-image
```

### 방법 C: kas (고급, CI용)

```bash
pip install kas
kas build yocto/kas.yml
```

## 4. QEMU로 테스트 (UNO Q 없이 PC에서)

> 이 섹션은 **첫 빌드 성공 이후** 진행 가능합니다 (현재 검증 대기).

```bash
# Yocto 빌드 후
cd yocto/build
runqemu qemuarm64 sdv-hpc-image nographic

# QEMU 내부 (Yocto 부팅 후)
systemctl status vsomeipd    # SOME/IP 데몬
systemctl status sdv-mpu     # AI 앱
journalctl -u sdv-mpu -f     # AI 로그
tshark -i eth0 -f "udp port 30490"  # SOME/IP 캡처
```

## 5. 실제 보드에 플래시 (선택)

```bash
# Raspberry Pi 4 대체 (QRB2210 BSP 없으므로 RPi4로 검증)
MACHINE=raspberrypi4-64 bitbake sdv-hpc-image
sudo dd if=tmp/deploy/images/raspberrypi4-64/sdv-hpc-image-raspberrypi4-64.wic of=/dev/sdX bs=4M

# UNO Q QRB2210에 플래시하려면 Qualcomm BSP (meta-qcom) 추가 필요 - 고급 과정
```

## 6. GitHub Actions CI (Yocto 빌드 생략 - 시간 때문)

`.github/workflows/ci.yml`(생성 예정)에서 Yocto 빌드는 `yocto` 브랜치에서만 실행:

```yaml
yocto-build:
  if: github.ref == 'refs/heads/yocto'
  runs-on: ubuntu-latest
  steps:
    - run: ./yocto/scripts/build-yocto.sh
```

면접에서: "CI는 Phase 1만 돌리고, Yocto는 로컬에서 sstate-cache로 빌드했습니다. 3시간 걸려서 CI에선 스킵했습니다." → 현실적인 답변

## 7. 면접에서 보여줄 증거

1.  **GitHub:** `yocto/meta-sdv/` 폴더 + `sdv-hpc-image.bb` + `vsomeip_3.4.10.bb`
2.  **스크린샷 (빌드 성공 시 추가 예정):** `bitbake sdv-hpc-image` 성공 로그 + `ls -lh tmp/deploy/images/`
3.  **QEMU 시연 (빌드 성공 후):** `runqemu`로 Yocto 부팅 영상 30초
4.  **비교 표:**

| Phase 1 (Debian) | Phase 2 (Yocto) |
|---|---|
| `docker compose up` 1분 | `bitbake` 1시간 |
| 빠른 프로토타이핑 | 프로덕션, 재현 가능, 라이선스 관리 |
| 6주 완성 | 독일이 원하는 LTS |

## 8. 문제 해결

- **디스크 부족:** Yocto는 100GB 필요. `build/sstate-cache`와 `build/downloads`는 유지, `build/tmp`는 `bitbake -c cleanall`로 정리
- **메모리 부족:** `local.conf`에서 `BB_NUMBER_THREADS=4`, `PARALLEL_MAKE=-j 4`로 낮추기
- **네트워크:** 회사 방화벽이면 `DL_DIR`에 미리 다운로드
- **시간 없음:** Phase 1만으로 지원하고, Yocto는 "진행 중"이라고 말해도 90% 통과

## 9. 다음 단계

- Phase 1으로 서류 통과 → Phase 2 빌드 로그로 면접 어필
- `vsomeip_3.4.10.bb`를 직접 작성했다는 것이 핵심 (단순 `apt install`이 아님)
- 추후: `meta-qcom` + QRB2210 BSP 추가로 실제 UNO Q에 Yocto 플래시 (고급)

---
## 10. 빌드 에러 시
→ `TROUBLESHOOTING.md` 참조 (TOP 12 에러 + 자동 진단)
```bash
./yocto/scripts/diagnose.sh
./yocto/scripts/fix-build.sh
```
