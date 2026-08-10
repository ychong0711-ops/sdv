/**
 * SOME/IP-SD 프로토콜 구현 (AUTOSAR PRS SOME/IP-SD, vsomeip 호환) - 순수 C
 *
 * Zephyr 의존 없음 -> 호스트(PC)에서 직접 컴파일/단위테스트 가능.
 * wire format 은 mpu/someip/sd.py (Python, pytest 16건 검증)와 바이트 단위 일치.
 *
 * 빅엔디안 수동 조립: Cortex-M33에서 unaligned read 를 피하기 위해
 * struct 재해석 대신 바이트 단위로 쓰고 읽는다.
 */
#include "someip_sd.h"

#include <string.h>

/* ---- 내부: 빅엔디안 쓰기/읽기 ---- */

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* ---- 내부: 서브 필드 빌더 ---- */

/* SOME/IP 헤더 16B (service 0xFFFF / method 0x8100 / SD 메시지 타입) */
static void put_someip_header(uint8_t *p, uint32_t length, uint16_t session_id) {
    put_u16(p + 0, SD_SERVICE_ID);
    put_u16(p + 2, SD_METHOD_ID);
    put_u32(p + 4, length); /* 20 + entries + options */
    put_u16(p + 8, SD_CLIENT_ID);
    put_u16(p + 10, session_id);
    p[12] = SD_PROTOCOL_VERSION;
    p[13] = SD_INTERFACE_VERSION;
    p[14] = SD_MESSAGE_TYPE;
    p[15] = SD_RETURN_CODE;
}

/* Service/Eventgroup 엔트리 16B.
 * service_entry: minor_version(4B) / eventgroup_entry: reserved(2B)+eventgroup_id(2B) */
static void put_entry(uint8_t *p, uint8_t type,
                      uint16_t service_id, uint16_t instance_id,
                      uint8_t major_version, uint32_t ttl,
                      uint16_t eventgroup_id) {
    p[0] = type;   /* index1=0, index2=0, #opts=0 (Python 기본값과 동일) */
    p[1] = 0;
    p[2] = 0;
    p[3] = 0;
    put_u16(p + 4, service_id);
    put_u16(p + 6, instance_id);
    p[8] = major_version;
    p[9] = (uint8_t)(ttl >> 16);
    p[10] = (uint8_t)(ttl >> 8);
    p[11] = (uint8_t)ttl;
    if (type == SD_ENTRY_SUBSCRIBE_EVENTGROUP ||
        type == SD_ENTRY_SUBSCRIBE_EVENTGROUP_ACK) {
        put_u16(p + 12, 0); /* reserved */
        put_u16(p + 14, eventgroup_id);
    } else {
        put_u32(p + 12, 0); /* minor_version */
    }
}

/* IPv4 Endpoint 옵션 12B (length=9, type=0x04, discardable=0) */
static void put_ip4_option(uint8_t *p, const uint8_t ip[4],
                           uint16_t port, uint8_t l4_proto) {
    put_u16(p + 0, 9);
    p[2] = SD_OPT_IP4_ENDPOINT;
    p[3] = 0; /* discardable */
    memcpy(p + 4, ip, 4);
    p[8] = 0; /* reserved */
    p[9] = l4_proto;
    put_u16(p + 10, port);
}

/* ---- 공개 빌더 ---- */

int sd_build_find_service(uint8_t *out, size_t cap,
                          uint16_t service_id, uint16_t instance_id,
                          uint16_t session_id) {
    const size_t pkt_len = 16u + 8u + SD_ENTRY_SIZE + 4u; /* 44 */
    if (cap < pkt_len || out == NULL) {
        return -1;
    }

    put_someip_header(out, 20u + SD_ENTRY_SIZE, session_id);
    out[16] = SD_FLAG_UNICAST; /* flags */
    out[17] = 0;               /* reserved 3B */
    out[18] = 0;
    out[19] = 0;
    put_u32(out + 20, SD_ENTRY_SIZE); /* entries_len = 16 */
    put_entry(out + 24, SD_ENTRY_FIND_SERVICE, service_id, instance_id,
              1, SD_TTL_DEFAULT, 0);
    put_u32(out + 40, 0); /* options_len = 0 */

    return (int)pkt_len;
}

int sd_build_subscribe_eventgroup(uint8_t *out, size_t cap,
                                  uint16_t service_id, uint16_t instance_id,
                                  uint16_t eventgroup_id,
                                  const uint8_t ip[4], uint16_t port,
                                  uint16_t session_id) {
    const size_t pkt_len = 16u + 8u + SD_ENTRY_SIZE + 4u + SD_IP4_OPTION_SIZE; /* 56 */
    if (cap < pkt_len || out == NULL || ip == NULL) {
        return -1;
    }

    put_someip_header(out, 20u + SD_ENTRY_SIZE + SD_IP4_OPTION_SIZE, session_id);
    out[16] = SD_FLAG_UNICAST;
    out[17] = 0;
    out[18] = 0;
    out[19] = 0;
    put_u32(out + 20, SD_ENTRY_SIZE);
    put_entry(out + 24, SD_ENTRY_SUBSCRIBE_EVENTGROUP, service_id, instance_id,
              1, SD_TTL_DEFAULT, eventgroup_id);
    put_u32(out + 40, SD_IP4_OPTION_SIZE); /* options_len = 12 */
    put_ip4_option(out + 44, ip, port, SD_L4_UDP);

    return (int)pkt_len;
}

/* ---- 파서 ---- */

int sd_parse(const uint8_t *buf, size_t len, struct sd_parsed *out) {
    const size_t hdr_size = 16u + 8u; /* SOME/IP + SD 헤더 */
    uint32_t entries_len, options_len, off, opt_off;
    uint8_t i;

    if (buf == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    if (len < hdr_size + 4u) {
        return -1;
    }
    /* SOME/IP 헤더 검증: SD 식별자 (0xFFFF / 0x8100) */
    if (get_u16(buf + 0) != SD_SERVICE_ID || get_u16(buf + 2) != SD_METHOD_ID) {
        return -1;
    }

    entries_len = get_u32(buf + 20);
    if (entries_len % SD_ENTRY_SIZE != 0 || entries_len / SD_ENTRY_SIZE > SD_MAX_ENTRIES) {
        return -1;
    }
    if (hdr_size + entries_len + 4u > len) {
        return -1; /* options_len 필드까지 필요 */
    }
    options_len = get_u32(buf + hdr_size + entries_len);
    if (options_len > len - (hdr_size + entries_len + 4u)) {
        return -1;
    }

    out->flags = buf[16];
    out->session_id = get_u16(buf + 10);
    out->entry_count = (uint8_t)(entries_len / SD_ENTRY_SIZE);

    /* entries 파싱 */
    for (off = 0; off < entries_len; off += SD_ENTRY_SIZE) {
        const uint8_t *e = buf + hdr_size + off;
        struct sd_entry *d = &out->entries[off / SD_ENTRY_SIZE];

        d->type = e[0];
        d->service_id = get_u16(e + 4);
        d->instance_id = get_u16(e + 6);
        d->major_version = e[8];
        d->ttl = ((uint32_t)e[9] << 16) | ((uint32_t)e[10] << 8) | e[11];
        if (d->type == SD_ENTRY_SUBSCRIBE_EVENTGROUP ||
            d->type == SD_ENTRY_SUBSCRIBE_EVENTGROUP_ACK) {
            d->eventgroup_id = get_u16(e + 14);
        }
    }

    /* options 파싱 (연속 배치, 블록 크기 = 3 + length) */
    opt_off = 0;
    for (i = 0; i < SD_MAX_OPTIONS && opt_off + 3u <= options_len; i++) {
        const uint8_t *o = buf + hdr_size + entries_len + 4u + opt_off;
        uint32_t block = 3u + get_u16(o + 0);
        if (block > options_len - opt_off) {
            break;
        }
        if (o[2] == SD_OPT_IP4_ENDPOINT && block >= SD_IP4_OPTION_SIZE) {
            struct sd_option_ip4 *d = &out->options[i];
            d->type = o[2];
            memcpy(d->address, o + 4, 4);
            d->l4_proto = o[9];
            d->port = get_u16(o + 10);
            out->option_count = i + 1;
        }
        opt_off += block;
    }

    return 0;
}
