# 면접 Q&A - 독일 SDV 팀장 대비

## Q1: Warum UNO Q und nicht S32G? (왜 UNO Q고 S32G가 아니냐)
**A:**
> Gute Frage. UNO Q ist für mich ein kostengünstiges Heterogeneous-Evaluierungsboard mit exakt derselben Architektur wie S32G: QRB2210 (Linux/Docker) + STM32U585 (Zephyr) + CAN-FD. Das ist die Zone-Architektur von MB.OS. Die Arduino-Abstraktion habe ich komplett entfernt - nur HAL/LL, Zephyr statt loop(), SOME/IP (vsomeip-kompatibles Wire-Format) statt Bridge. Der MISRA-C-Check ist vorbereitet, die Verifikation läuft noch. Mit S32G hätte ich dasselbe gemacht, nur 5x teurer und 3 Monate später. Für den Nachweis von SOME/IP/Docker ist das Board-Logo irrelevant.

## Q2: Yocto Erfahrung? (Yocto 경험 있냐)
**A:**
> Phase 1 habe ich mit Debian gestartet, um SOME/IP schnell zu beweisen. Phase 2 portiere ich gerade auf Yocto Kirkstone (Branch yocto). Layer-Struktur: poky + meta-oe + meta-sdv (vsomeip_3.4.10.bb, sdv-hpc-image.bb, sdv-mpu.bb). Die Rezepte sind geschrieben, und die SRCREV für vsomeip ist gegen das offizielle Tag 3.4.10 (Commit 02c199d, 2023-11-29) verifiziert. Der erste erfolgreiche Build steht noch aus - das ist der nächste Schritt.

**보너스:** `yocto/meta-sdv/recipes-support/vsomeip/vsomeip_3.4.10.bb` 파일 구조를 화이트보드에 그릴 수 있어야 함.

## Q3: QNX vs Linux? (QNX와 리눅스 차이)
**A:**
> QNX ist Microkernel (ASIL-D, deterministisch, 512MB+), Linux ist monolithisch. Für HPC/ADAS nutzen wir QNX oder PREEMPT_RT Linux. Zephyr ist für mich der Microkernel-Ansatz im Kleinen - deshalb habe ich Zephyr gewählt, um QNX-Konzepte (Memory Protection, Watchdog, Safe State) zu üben. QNX SDP 8.0 habe ich evaluiert.

## Q4: Wie haben Sie getestet? (어떻게 테스트했냐 - ASPICE)
**A:**
> V-Modell: 1) SOME/IP 설정(vsomeip.json 참조) und DBC für CAN-Nachrichten als Anforderungsdefinition 2) Zephyr/C++ + Python (raw-socket) Implementierung 3) Host-Simulation (UDP 30490) getestet. Die CI-Pipeline (pytest, cppcheck MISRA, Docker-Build, Zephyr-Build) ist eingerichtet, die Ergebnisse werden nach dem ersten Push aktualisiert. MISRA-/Coverage-Zahlen veröffentliche ich erst nach der CI-Verifikation; die Verifikation auf dem realen Board (Safe State, UART-Tunnel) steht noch aus.

## Q5: Was ist SOME/IP-SD? (SOME/IP-SD가 뭐냐)
**A:**
> Service Discovery (OfferService, FindService, SubscribeEventgroup) ist der Mechanismus, mit dem SOME/IP-Dienste gefunden werden. In meinem Projekt ist SD noch nicht implementiert - es gibt nur Informations-Logs, das ist Phase 2. Aktuell läuft die Kommunikation direkt über SOME/IP-Notify (0x8001) und Heartbeat (0x8002) im vsomeip-kompatiblen Wire-Format.

## Q6: Arduino ist doch Spielzeug! (아두이노는 장난감 아니냐)
**A:**
> (웃으며) Ja, wenn man delay() und String benutzt. Ich nutze nur die Hardware als STM32-Eval-Board. Schauen Sie meinen Code: kein Arduino-Code, nur Zephyr Threads, SOME/IP (vsomeip-kompatibles Wire-Format), CAN-FD mit E2E CRC. Arduino CLI ist nur ein alternatives Build-System - primär baue ich mit west (Zephyr).

## Q7: Gehaltsvorstellung? (희망 연봉)
**A:**
> Für Junior SDV Embedded (Stuttgart/München) liegt mein Ziel bei €65,000-€72,000 Brutto. Mit Blue Card.

## Q8: Deutschkenntnisse? (독일어)
**A:**
> Aktuell B1, Ziel B2 bis Einreise. Arbeitssprache ist Englisch, Dokumentation auf Deutsch lerne ich schnell. (독일은 영어로 일하고 독일어는 배우겠다는 자세가 중요)
