/**
 * SOME/IP-SD 호스트 통합 시뮬레이터 (MCU 소비자 역할, PC 시뮬레이션)
 *
 * 실제 MCU 프로토콜 코드(someip_sd.cpp)를 호스트에서 컴파일해,
 * 실제 MPU Python 스택(SdServer + client)과 UDP 멀티캐스트로 통신하며
 * SD 흐름(Find -> Offer -> Subscribe)과 이벤트(Notify/Heartbeat) 수신을 검증한다.
 *
 * 검증 시나리오 (scripts/pc_sim_test.py 가 MPU 스택을 띄우고 실행):
 *   1. 멀티캐스트 224.224.224.245:30490 join + FindService 전송
 *   2. MPU SdServer의 OfferService 수신 -> sd_parse()로 검증 -> 프로바이더 확인
 *   3. SubscribeEventgroup 전송
 *   4. MPU client의 Notify(0x8001)/Heartbeat(0x8002) 수신 -> 헤더/페이로드 검증
 *   5. 성공 시 0, 실패 시 1 반환 (스텝마다 PASS/FAIL 로그)
 *
 * 빌드 (호스트, Zephyr 무의존):
 *   g++ -std=c++17 -Wall -Wextra -Werror -IMCU/src \
 *       MCU/tests/sd_host_sim.cpp MCU/src/someip_sd.cpp -o sd_host_sim
 */
#include "someip_sd.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define CLOSESOCK closesocket
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK close
#define INVALID_SOCKET (-1)
#endif

#define SIM_PORT 30490
#define SERVICE_ID 0x1234
#define INSTANCE_ID 0x5678
#define EVENT_NOTIFY 0x8001
#define EVENT_HEARTBEAT 0x8002
#define SIM_EVENTGROUP_ID 0x0001
#define SIM_MULTICAST_IP "224.224.224.245"

static int failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            std::printf("PASS %s\n", msg);                                     \
        } else {                                                               \
            std::printf("FAIL %s\n", msg);                                     \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static uint16_t sim_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

int main(void) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == static_cast<int>(INVALID_SOCKET)) {
        std::printf("FAIL socket create\n");
        return 1;
    }

    // bind 0.0.0.0:30490 (SdServer/MPU client 가 같은 포트로 전송)
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SIM_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::printf("FAIL bind %d\n", SIM_PORT);
        return 1;
    }

    // 멀티캐스트 그룹 join (OfferService 수신)
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    inet_pton(AF_INET, SIM_MULTICAST_IP, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   reinterpret_cast<const char *>(&mreq), sizeof(mreq)) < 0) {
        std::printf("FAIL multicast join %s\n", SIM_MULTICAST_IP);
        return 1;
    }

    // 수신 타임아웃 5s (시나리오당 최대 대기)
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char *>(&tv), sizeof(tv));

    // 1. FindService 전송
    {
        uint8_t buf[64];
        int n = sd_build_find_service(buf, sizeof(buf), SERVICE_ID, INSTANCE_ID, 1);
        struct sockaddr_in mcast;
        memset(&mcast, 0, sizeof(mcast));
        mcast.sin_family = AF_INET;
        mcast.sin_port = htons(SIM_PORT);
        inet_pton(AF_INET, SIM_MULTICAST_IP, &mcast.sin_addr);
        int ret = sendto(sock, reinterpret_cast<const char *>(buf), (size_t)n, 0,
                         reinterpret_cast<struct sockaddr *>(&mcast), sizeof(mcast));
        CHECK(ret == n, "FindService multicast 전송");
    }

    // 2. OfferService 수신 (자기 FindService 에코는 스킵) + 3. SubscribeEventgroup 전송
    {
        uint8_t buf[256];
        struct sockaddr_in src;
        socklen_t src_len;
        bool offer_ok = false;
        bool opt_ok = false;
        int n;

        for (int i = 0; i < 8 && !offer_ok; i++) {
            src_len = sizeof(src);
            n = recvfrom(sock, reinterpret_cast<char *>(buf), sizeof(buf), 0,
                         reinterpret_cast<struct sockaddr *>(&src), &src_len);
            if (n < 16) {
                continue; // 타임아웃 (SdServer 1s 주기 광고 대기)
            }

            struct sd_parsed parsed;
            if (sd_parse(buf, (size_t)n, &parsed) < 0) {
                continue;
            }

            for (uint8_t e = 0; e < parsed.entry_count; e++) {
                const struct sd_entry *ent = &parsed.entries[e];
                // 자기 FindService 에코(0x00)는 스킵, OfferService(0x01)만 매칭
                if (ent->type == SD_ENTRY_OFFER_SERVICE &&
                    ent->service_id == SERVICE_ID &&
                    ent->instance_id == INSTANCE_ID) {
                    offer_ok = true;
                    for (uint8_t o = 0; o < parsed.option_count; o++) {
                        const struct sd_option_ip4 *op = &parsed.options[o];
                        if (op->type == SD_OPT_IP4_ENDPOINT &&
                            op->l4_proto == SD_L4_UDP) {
                            std::printf("  provider endpoint %u.%u.%u.%u:%u\n",
                                        op->address[0], op->address[1],
                                        op->address[2], op->address[3], op->port);
                            opt_ok = true;
                        }
                    }
                    break;
                }
            }
        }
        CHECK(offer_ok, "OfferService 0x1234/0x5678 엔트리 확인");
        CHECK(opt_ok, "OfferService IPv4 Endpoint 옵션 확인");
        if (!offer_ok) {
            return 1;
        }

        // 3. SubscribeEventgroup 전송 (프로바이더 유니캐스트, 요청자=로컬 127.0.0.1)
        uint8_t sub[64];
        const uint8_t self_ip[4] = {127, 0, 0, 1};
        int sn = sd_build_subscribe_eventgroup(sub, sizeof(sub), SERVICE_ID,
                                               INSTANCE_ID, SIM_EVENTGROUP_ID,
                                               self_ip, SIM_PORT, 2);
        struct sockaddr_in prov;
        memset(&prov, 0, sizeof(prov));
        prov.sin_family = AF_INET;
        prov.sin_port = htons(SIM_PORT); /* MPU client 는 30490으로 전송 */
        prov.sin_addr.s_addr = htonl(0x7F000001); /* 127.0.0.1 (PC 시뮬레이션) */
        int sret = sendto(sock, reinterpret_cast<const char *>(sub), (size_t)sn, 0,
                          reinterpret_cast<struct sockaddr *>(&prov), sizeof(prov));
        CHECK(sret == sn, "SubscribeEventgroup 전송");
    }

    // 4. Notify/Heartbeat 이벤트 수신 (MPU client 가 주기 전송)
    {
        uint8_t buf[256];
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        bool got_notify = false;
        bool got_heartbeat = false;
        for (int i = 0; i < 6 && !(got_notify && got_heartbeat); i++) {
            int n = recvfrom(sock, reinterpret_cast<char *>(buf), sizeof(buf), 0,
                             reinterpret_cast<struct sockaddr *>(&src), &src_len);
            if (n < (int)sizeof(uint16_t) * 2) {
                continue; // 타임아웃이면 다음 루프
            }
            uint16_t svc = sim_u16(buf);
            uint16_t method = sim_u16(buf + 2);
            if (svc != SERVICE_ID) {
                continue;
            }
            if (method == EVENT_NOTIFY) {
                got_notify = true;
                std::printf("  Notify(0x8001) payload=%u 수신\n", buf[16]);
            } else if (method == EVENT_HEARTBEAT) {
                got_heartbeat = true;
                std::printf("  Heartbeat(0x8002) payload=%u 수신\n", buf[16]);
            }
        }
        CHECK(got_notify, "Notify(0x8001) 이벤트 수신");
        CHECK(got_heartbeat, "Heartbeat(0x8002) 이벤트 수신");
    }

    CLOSESOCK(sock);

    if (failures == 0) {
        std::printf("ALL SD HOST SIM TESTS PASSED (PC 시뮬레이션 통합 검증 완료)\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
