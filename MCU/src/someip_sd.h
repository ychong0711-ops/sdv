/**
 * SOME/IP-SD 프로토콜 (AUTOSAR PRS SOME/IP-SD, vsomeip 호환) - 순수 C 모듈
 *
 * Zephyr에 의존하지 않으므로 호스트에서 clang++/g++로 직접 컴파일 가능
 * (MCU/tests/test_someip_sd_host.cpp 가 wire format 을 검증).
 *
 * 와이어 포맷 (모두 빅엔디안):
 *   SOME/IP 헤더 16B: service 0xFFFF | method 0x8100 | length u32 |
 *                     client u16 | session u16 | 01 01 02 00
 *   SD 헤더 8B:       flags u8 | reserved 3B | entries_len u32
 *   entries (16B 단위) | options_len u32 | options
 *   length = 20 + len(entries) + len(options)
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define SD_SERVICE_ID 0xFFFF
#define SD_METHOD_ID 0x8100
#define SD_CLIENT_ID 0x0000
#define SD_PROTOCOL_VERSION 0x01
#define SD_INTERFACE_VERSION 0x01
#define SD_MESSAGE_TYPE 0x02
#define SD_RETURN_CODE 0x00
#define SD_FLAG_UNICAST 0x40

#define SD_ENTRY_FIND_SERVICE 0x00
#define SD_ENTRY_OFFER_SERVICE 0x01
#define SD_ENTRY_SUBSCRIBE_EVENTGROUP 0x06
#define SD_ENTRY_SUBSCRIBE_EVENTGROUP_ACK 0x07

#define SD_OPT_IP4_ENDPOINT 0x04
#define SD_L4_UDP 0x11
#define SD_TTL_DEFAULT 0xFFFFFF

#define SD_ENTRY_SIZE 16u
#define SD_IP4_OPTION_SIZE 12u
#define SD_MAX_ENTRIES 4u
#define SD_MAX_OPTIONS 4u

struct sd_entry {
    uint8_t type;
    uint16_t service_id;
    uint16_t instance_id;
    uint8_t major_version;
    uint32_t ttl;             /* 24-bit TTL */
    uint16_t eventgroup_id;   /* eventgroup 엔트리 전용 (서비스 엔트리는 0) */
};

struct sd_option_ip4 {
    uint8_t type;
    uint8_t address[4];
    uint8_t l4_proto;
    uint16_t port;
};

struct sd_parsed {
    uint8_t flags;
    uint16_t session_id;
    uint8_t entry_count;
    struct sd_entry entries[SD_MAX_ENTRIES];
    uint8_t option_count;
    struct sd_option_ip4 options[SD_MAX_OPTIONS];
};

/**
 * FindService 패킷 빌드: ENTRY_FIND_SERVICE 1개, 옵션 없음 (멀티캐스트 전송용).
 * @return 패킷 길이(44) 또는 -1 (버퍼 부족)
 */
int sd_build_find_service(uint8_t *out, size_t cap,
                          uint16_t service_id, uint16_t instance_id,
                          uint16_t session_id);

/**
 * SubscribeEventgroup 패킷 빌드: ENTRY_SUBSCRIBE_EVENTGROUP 1개 + IPv4 Endpoint 옵션 1개.
 * @return 패킷 길이(56) 또는 -1 (버퍼 부족)
 */
int sd_build_subscribe_eventgroup(uint8_t *out, size_t cap,
                                  uint16_t service_id, uint16_t instance_id,
                                  uint16_t eventgroup_id,
                                  const uint8_t ip[4], uint16_t port,
                                  uint16_t session_id);

/**
 * SD 패킷 파서. entries/options 배열에 담아 반환.
 * @return 0 성공 / -1 형식 오류 (헤더 불일치, 길이 오류 등)
 */
int sd_parse(const uint8_t *buf, size_t len, struct sd_parsed *out);
