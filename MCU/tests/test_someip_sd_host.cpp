/**
 * SOME/IP-SD 호스트 단위 테스트 (Zephyr 무의존)
 *
 * someip_sd.cpp 를 호스트(clang++/g++)로 직접 컴파일하여 wire format 을 검증한다.
 * 기준 벡터는 Python 구현 (mpu/someip/sd.py, pytest 16건 통과) 이 생성한 바이트와
 * 동일해야 하므로, 양쪽 구현의 상호 호환성을 보장한다.
 *
 * 빌드/실행 (로컬):
 *   clang++ -std=c++17 -Wall -Wextra -Werror \
 *       MCU/tests/test_someip_sd_host.cpp MCU/src/someip_sd.cpp -o test_someip_sd
 *   ./test_someip_sd
 */
#include "someip_sd.h"

#include <cstdio>
#include <cstring>

/* ---- Python (mpu/someip/sd.py) 생성 기준 벡터 ---- */
// build_find_service(0x1234, 0x5678) len=44
static const uint8_t PY_FIND[] = {
    0xFF, 0xFF, 0x81, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x01,
    0x01, 0x01, 0x02, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x01, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// build_offer_service(0x1234, 0x5678, '10.0.2.15', 30490) len=56
static const uint8_t PY_OFFER[] = {
    0xFF, 0xFF, 0x81, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x01,
    0x01, 0x01, 0x02, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x01, 0x00, 0x00, 0x10, 0x12, 0x34, 0x56, 0x78, 0x01, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x09, 0x04, 0x00,
    0x0A, 0x00, 0x02, 0x0F, 0x00, 0x11, 0x77, 0x1A};

// build_subscribe_eventgroup(0x1234, 0x5678, 0x0001, '10.0.2.15', 30490) len=56
static const uint8_t PY_SUBSCRIBE[] = {
    0xFF, 0xFF, 0x81, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x01,
    0x01, 0x01, 0x02, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x06, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x01, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x09, 0x04, 0x00,
    0x0A, 0x00, 0x02, 0x0F, 0x00, 0x11, 0x77, 0x1A};

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* ---- 빌더: Python 출력과 바이트 단위 일치 ---- */

static void test_build_find_matches_python(void) {
    uint8_t buf[64];
    int n = sd_build_find_service(buf, sizeof(buf), 0x1234, 0x5678, 1);
    CHECK(n == (int)sizeof(PY_FIND));
    CHECK(n > 0 && memcmp(buf, PY_FIND, (size_t)n) == 0);
}

static void test_build_subscribe_matches_python(void) {
    const uint8_t ip[4] = {10, 0, 2, 15};
    uint8_t buf[64];
    int n = sd_build_subscribe_eventgroup(buf, sizeof(buf), 0x1234, 0x5678,
                                          0x0001, ip, 30490, 1);
    CHECK(n == (int)sizeof(PY_SUBSCRIBE));
    CHECK(n > 0 && memcmp(buf, PY_SUBSCRIBE, (size_t)n) == 0);
}

static void test_build_overflow_returns_minus1(void) {
    uint8_t small[8];
    CHECK(sd_build_find_service(small, sizeof(small), 0x1234, 0x5678, 1) == -1);
    CHECK(sd_build_find_service(NULL, 64, 0x1234, 0x5678, 1) == -1);
    CHECK(sd_build_subscribe_eventgroup(small, sizeof(small), 0x1234, 0x5678,
                                        1, NULL, 30490, 1) == -1);
}

/* ---- 파서: Python 벡터에서 필드 추출 ---- */

static void test_parse_find(void) {
    struct sd_parsed p;
    CHECK(sd_parse(PY_FIND, sizeof(PY_FIND), &p) == 0);
    CHECK(p.entry_count == 1);
    CHECK(p.option_count == 0);
    CHECK(p.entries[0].type == SD_ENTRY_FIND_SERVICE);
    CHECK(p.entries[0].service_id == 0x1234);
    CHECK(p.entries[0].instance_id == 0x5678);
    CHECK(p.entries[0].major_version == 1);
    CHECK(p.entries[0].ttl == 0xFFFFFF);
    CHECK(p.session_id == 1);
    CHECK(p.flags == SD_FLAG_UNICAST);
}

static void test_parse_offer(void) {
    struct sd_parsed p;
    CHECK(sd_parse(PY_OFFER, sizeof(PY_OFFER), &p) == 0);
    CHECK(p.entry_count == 1);
    CHECK(p.option_count == 1);
    CHECK(p.entries[0].type == SD_ENTRY_OFFER_SERVICE);
    CHECK(p.entries[0].service_id == 0x1234);
    CHECK(p.entries[0].instance_id == 0x5678);
    CHECK(p.entries[0].ttl == 0xFFFFFF);
    CHECK(p.options[0].type == SD_OPT_IP4_ENDPOINT);
    CHECK(p.options[0].address[0] == 10 && p.options[0].address[1] == 0 &&
          p.options[0].address[2] == 2 && p.options[0].address[3] == 15);
    CHECK(p.options[0].port == 30490);
    CHECK(p.options[0].l4_proto == SD_L4_UDP);
}

static void test_parse_subscribe(void) {
    struct sd_parsed p;
    CHECK(sd_parse(PY_SUBSCRIBE, sizeof(PY_SUBSCRIBE), &p) == 0);
    CHECK(p.entry_count == 1);
    CHECK(p.entries[0].type == SD_ENTRY_SUBSCRIBE_EVENTGROUP);
    CHECK(p.entries[0].eventgroup_id == 0x0001);
    CHECK(p.entries[0].service_id == 0x1234);
    CHECK(p.options[0].port == 30490);
}

/* ---- 네거티브: 잘못된 입력 거부 ---- */

static void test_parse_rejects_invalid(void) {
    struct sd_parsed p;

    CHECK(sd_parse(NULL, 44, &p) == -1);
    CHECK(sd_parse(PY_FIND, 0, &p) == -1);
    CHECK(sd_parse(PY_FIND, 8, &p) == -1);             // 너무 짧음
    CHECK(sd_parse(PY_FIND, 23, &p) == -1);            // entries_len 필드 미확보

    // 서비스 ID가 SD(0xFFFF)가 아닌 패킷 (이벤트 메시지처럼 보이는 것)
    uint8_t bad[44];
    memcpy(bad, PY_FIND, sizeof(bad));
    bad[0] = 0x12;
    bad[1] = 0x34;
    CHECK(sd_parse(bad, sizeof(bad), &p) == -1);

    // entries_len 이 16의 배수가 아닌 손상 패킷
    uint8_t corrupt[44];
    memcpy(corrupt, PY_FIND, sizeof(corrupt));
    corrupt[23] = 0x11; // entries_len = 0x11 (17)
    CHECK(sd_parse(corrupt, sizeof(corrupt), &p) == -1);
}

int main(void) {
    test_build_find_matches_python();
    test_build_subscribe_matches_python();
    test_build_overflow_returns_minus1();
    test_parse_find();
    test_parse_offer();
    test_parse_subscribe();
    test_parse_rejects_invalid();

    if (failures == 0) {
        std::printf("ALL SOME/IP-SD HOST TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
