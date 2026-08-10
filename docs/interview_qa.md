# 면접 Q&A - 독일 SDV 팀장 대비

## Q1: Warum UNO Q und nicht S32G? (왜 UNO Q고 S32G가 아니냐)
**A:**
> Gute Frage. UNO Q ist für mich ein kostengünstiges Heterogeneous-Evaluierungsboard mit exakt derselben Architektur wie S32G: QRB2210 (Linux/Docker) + STM32U585 (Zephyr) + CAN-FD. Das ist die Zone-Architektur von MB.OS. Die Arduino-Abstraktion habe ich komplett entfernt - nur HAL/LL, Zephyr statt loop(), SOME/IP (vsomeip-kompatibles Wire-Format) statt Bridge. Statische Analyse läuft in der CI (cppcheck, warning/performance/portability). Mit S32G hätte ich dasselbe gemacht, nur 5x teurer und 3 Monate später. Für den Nachweis von SOME/IP/Docker ist das Board-Logo irrelevant.

## Q2: Yocto Erfahrung? (Yocto 경험 있냐)
**A:**
> Ja, und zwar bis zum laufenden System. Phase 1 habe ich mit Debian gestartet, um SOME/IP schnell zu beweisen. Phase 2 ist ein vollständiger Yocto Kirkstone (4.0 LTS) Build mit Custom Layer `meta-sdv`: poky + meta-oe + meta-virtualization + meta-sdv. Ich baue das Image `sdv-hpc-image` für qemuarm64 (Cortex-A57, als Ersatz für den QRB2210 Cortex-A53).
>
> **Was wirklich gelaufen ist (verifiziert):**
> - Build erfolgreich: 7.125 Tasks, alle grün, Image `sdv-hpc-image-...rootfs.ext4` (751 MB) + `Image`-Kernel (5.15.201)
> - Inhalt des Images: **docker-ce 20.10.25** + **vsomeip 3.4.10** (COVESA) + **sdv-mpu 1.0** (eigene App) + openssh/systemd
> - **QEMU-Boot bestätigt**: SSH-Port geöffnet, `dockerd` läuft (hello-world Container erfolgreich ausgeführt), `vsomeipd` (Routing-Manager) aktiv mit Logs "vSomeIP 3.4.10"
> - Kernel-Config für Docker angepasst: `CONFIG_VETH=y`, `NETFILTER_XT_MATCH_ADDRTYPE=m` (per Kernel-Config-Fragment `docker.cfg`)
>
> Build-Infrastruktur: Docker-Container als Build-Host (Ubuntu 22.04), sstate-cache/downloads in Linux-Native-Volumes (NTFS-Problem umgangen), `BB_NUMBER_THREADS=2`, `PARALLEL_MAKE:pn-gcc-cross-aarch64="-j 1"` (Make-Race in gcc-cross vermieden).

**보너스:** `yocto/meta-sdv/recipes-support/vsomeip/vsomeip_3.4.10.bb` 파일 구조를 화이트보드에 그릴 수 있어야 함.
- SRC_URI = GitHub Archive tarball + sha256 (github.com hat `git://` eingestellt, Kirkstone-Fetcher transformierte die URL falsch → auf Archive-Tarball mit festem SRCREV umgestellt)
- `do_install:append`: routingmanagerd-Binary als `/usr/bin/vsomeipd` installieren (vsomeip 3.4.10 baut den Daemon unter diesem Namen), vsomeip.json + systemd-Unit
- `vsomeip.json`: `routingmanagerd` als Routing-App (id 0xffff, routing=true) konfiguriert — sonst "not configured as routing - abort"

## Q3: QNX vs Linux? (QNX와 리눅스 차이)
**A:**
> QNX ist Microkernel (ASIL-D, deterministisch, 512MB+), Linux ist monolithisch. Für HPC/ADAS nutzen wir QNX oder PREEMPT_RT Linux. Zephyr ist für mich der Microkernel-Ansatz im Kleinen - deshalb habe ich Zephyr gewählt, um QNX-Konzepte (Memory Protection, Watchdog, Safe State) zu üben. QNX SDP 8.0 habe ich evaluiert.

## Q4: Wie haben Sie getestet? (어떻게 테스트했냐 - ASPICE)
**A:**
> V-Modell: 1) SOME/IP-Format (vsomeip-kompatibel, Service 0x1234/Events 0x8001/0x8002) und CAN-Nachricht (ID 0x18FF01F4, CRC8) als Anforderungsdefinition 2) Zephyr/C++ + Python (raw-socket) Implementierung 3) Host-Simulation (UDP 30490) mit pytest getestet.
>
> **CI-Pipeline läuft und ist grün** (GitHub Actions, 4 Jobs): pytest (Python-Unit-Tests), cppcheck (statische Analyse), Zephyr-Build (`west build -b arduino_uno_q`), Docker-Build. Dazu Yocto-Build + QEMU-Boot-Verifikation (dockerd + vsomeipd + Container-Start) als Systemtest.
>
> **Wichtig - ehrlich:** MISRA-C 2012 habe ich bewusst **nicht** angewendet: MISRA-C ist für C-Code definiert, mein MCU-Code ist C++ (Zephyr). cppcheck mit `--addon=misra` meldet auf C++ viele Fehlalarme (Regel 15.5 single-exit, 14.4 if-Form etc.), die nichts mit Code-Qualität zu tun haben. Stattdessen nutze ich cppcheck allgemein (warning/performance/portability). MISRA-Zahlen behaupte ich nicht. Die Verifikation auf dem realen Board (Safe State über UART-Tunnel) steht noch aus - das kommuniziere ich transparent.

## Q5: Was ist SOME/IP-SD? (SOME/IP-SD가 뭐냐)
**A:**
> Service Discovery (OfferService, FindService, SubscribeEventgroup) ist der Mechanismus, mit dem SOME/IP-Dienste dynamisch gefunden werden - das Herzstück für service-orientierte Architektur im Fahrzeug. In meinem Projekt ist SD **implementiert**: Ich habe ein eigenes SD-Modul im vsomeip-kompatiblen Wire-Format geschrieben (`mpu/someip/sd.py`): SD-Header (0xFFFF/0x8100, Message-Type 0x02, Unicast-Flag 0x40), Service-Entries (FindService 0x00 / OfferService 0x01, StopOffer = TTL 0), SubscribeEventgroup (0x06) und IPv4-Endpoint-Option (0x04) - per Unit-Tests gegen die AUTOSAR-PRS-/vsomeip-Konvention verifiziert (Länge 20+16N+Σ(3+opt), TTL 0xFFFFFF, Session-Increment). Der `SdServer` sendet periodisch OfferService über Multicast (224.224.224.245:30490), `SdClient` macht FindService. Nebenläufig läuft weiter die direkte SOME/IP-Notify (0x8001)/Heartbeat (0x8002), und der vsomeip-Routing-Manager (`vsomeipd`) ist im Yocto-Image aktiv.

## Q6: Arduino ist doch Spielzeug! (아두이노는 장난감 아니냐)
**A:**
> (웃으며) Ja, wenn man delay() und String benutzt. Ich nutze nur die Hardware als STM32-Eval-Board. Schauen Sie meinen Code: kein Arduino-Code, nur Zephyr Threads, SOME/IP (vsomeip-kompatibles Wire-Format), CAN-FD mit E2E CRC. Arduino CLI ist nur ein alternatives Build-System - primär baue ich mit west (Zephyr).

## Q7: Gehaltsvorstellung? (희망 연봉)
**A:**
> Für Junior SDV Embedded (Stuttgart/München) liegt mein Ziel bei €65,000-€72,000 Brutto. Mit Blue Card.

## Q8: Deutschkenntnisse? (독일어)
**A:**
> Aktuell B1, Ziel B2 bis Einreise. Arbeitssprache ist Englisch, Dokumentation auf Deutsch lerne ich schnell. (독일은 영어로 일하고 독일어는 배우겠다는 자세가 중요)

## Q9: Was war Ihr schwierigstes Problem? (가장 어려웠던 문제는?)
**A:**
> Mehrere harte Probleme, drei typische:
>
> 1. **gcc-cross Make-Race (Yocto):** undefined reference `simplify_replace_rtx` beim gcc-cross-aarch64 - nur bei parallelem Build reproduzierbar. Lösung: `PARALLEL_MAKE:pn-gcc-cross-aarch64 = "-j 1"` + `cleansstate`. Wichtige Lektion: Bei Toolchain-Builds erst parallel, dann serialisiert isolieren.
>
> 2. **Docker im Kernel:** `dockerd` startete, aber Container-Start scheiterte (veth-Paar nicht möglich) und davor NAT-Fehler (iptables `addrtype` fehlte). Lösung über Kernel-Config-Fragmente: `CONFIG_VETH=y`, `CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=m` in `docker.cfg`, dann Kernel + Image neu gebaut. Das zeigt: Docker auf Embedded-Linux heißt Kernel-Config-Verständnis, nicht nur Container-APIs.
>
> 3. **vsomeip-Routing-Manager:** Daemon startete und brach mit "routingmanagerd has not been configured as routing - abort" ab. Ursache: vsomeip.json hatte die Routing-App falsch konfiguriert (Name/ID). Fix: `routingmanagerd` (id 0xffff) mit `"routing": true` registrieren. Zusätzlich baut vsomeip 3.4.10 den Daemon als `routingmanagerd` - musste im Rezept als `/usr/bin/vsomeipd` installiert werden, weil die systemd-Unit das erwartet.
>
> **Meta-Lektion:** Ich protokolliere jeden Fehler in `yocto/TROUBLESHOOTING.md` - beim nächsten Build ist derselbe Fehler in 2 Minuten gelöst, nicht in 2 Stunden.

## Q10: Wie arbeiten Sie mit Docker im Yocto-Build? (Yocto 빌드에 Docker를 어떻게 활용했나?)
**A:**
> Der Build läuft in einem Docker-Container (Ubuntu 22.04) als Build-Host - reproduzierbar auf jedem Rechner. Wichtige Details:
> - **Volumes:** sstate-cache, downloads, tmp als Linux-Native-Volumes statt NTFS-Bind-Mounts (BitBake/pzstd braucht case-sensitive FS und funktionierte auf NTFS nicht zuverlässig)
> - **Memory:** Host-WSL2 von 8GB auf 12GB erhöht, da Rust-native/LLVM-Build 1h56m mit hohem RAM lief; danach `BB_NUMBER_THREADS=2` um OOM zu vermeiden
> - **Logging:** Build-Protokoll per `tee` in ein Volume - wenn der Container stirbt (OOM oder Fehler), ist der Fehler nicht verloren
> - **Ergebnis:** kompletter 7.125-Task-Build + QEMU-Systemtest aus einem `docker run`-Befehl
