# Phase 4 — HakiOS auf das vorhandene `renoirPkg` legen

Aus redwoods echtem `uefi/` abgeleitet (BASE-Commit, `mu-silicium.patch`, `XnuLoader.[ch]`,
`apply.sh`/`export.sh`). Weil `renoirPkg` in Mu-Silicium **schon existiert**, baust du Mu-Silicium
nicht neu — du legst nur redwoods HakiOS-Änderungen auf renoirPkg. Die HakiOS-UEFI-Änderungen sind
fast alle **geräteunabhängig**; renoir-spezifisch ist im Kern **eine** Zeile.

## Was redwood an Mu-Silicium ändert (und was davon geräteunabhängig ist)

`mu-silicium.patch` fasst nur **zwei** Dateien an, beide SoC-/geräteunabhängig:

1. **`BootShim/BootShim.S`** — hinter dem UEFI-FD wird optional ein „XNUP"-Container gesucht
   (`#ifdef PAYLOAD_BASE`) und nach `PAYLOAD_BASE` kopiert. Reiner Mechanismus, kein Gerätebezug.
2. **`Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/`**
   - `BootManager.c`: in `PlatformBootManagerUnableToBoot()` wird vor allem anderen
     `XnuLoaderMain()` aufgerufen (kehrt nicht zurück) + `#include "XnuLoader.h"`.
   - `BootManagerLib.inf`: `XnuLoader.c` zu `[Sources]`, dazu `BaseLib`, `BaseMemoryLib` und die
     Protokolle `gEfiBlockIoProtocolGuid`, `gEfiPartitionInfoProtocolGuid`.

Dazu **neue Dateien** (aus redwoods `files/`), ebenfalls geräteunabhängig:
`Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/XnuLoader.c` + `XnuLoader.h`.

→ **Diese Teile kannst du 1:1 übernehmen.** Sie hängen nicht an redwood.

## Die EINE renoir-spezifische Änderung: Payload-Region reservieren

redwood reserviert die Zielregion des XNUP-Containers in seiner Memory-Map
(`files/Platforms/Xiaomi/redwoodPkg/Library/MemoryMapLib/MemoryMapLib.c`):

```c
{"XNU Payload",  0xF0000000, 0x04000000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
```

Das passt zu `XnuLoader.h` (`XNU_PAYLOAD_BASE 0xF0000000`, `XNU_PAYLOAD_MAX_SIZE 0x04000000`).

**Für renoir:** dieselbe Zeile in
`Mu-Silicium/Platforms/Xiaomi/renoirPkg/Library/MemoryMapLib/MemoryMapLib.c` eintragen. Vorher
prüfen, dass `0xF0000000`–`0xF4000000` in renoirs Map frei ist (renoirs MemoryMapLib + renoirs
`reserved-memory` im DT). Ist dort schon etwas, eine freie reservierte Region wählen und in
`XnuLoader.h`/dem Build konsistent ändern. Bei renoir sehr wahrscheinlich identisch (gleiches
SoC-Paket CedrosPkg, gleiche Adressräume wie lisa/redwood).

## `PAYLOAD_BASE` im BootShim-Build scharf schalten

`BootShim.S` kopiert den Payload nur bei definiertem `PAYLOAD_BASE`. redwood schaltet das für
seinen Build ein (nicht in `redwood.dsc/.fdf` sichtbar → kommt aus der BootShim-Build-Verdrahtung,
die `renoir.toml`/`redwood.toml` mit `requires_kernel_header = true` anstößt). **To-do beim ersten
Bauen:** verifizieren, wo redwoods Build `PAYLOAD_BASE=0xF0000000` setzt, und für renoir gleich
setzen. (Ohne das wird der XNUP-Container nicht kopiert und der Loader findet nichts.)

## Ablauf (nach dem Muster von redwoods `apply.sh`)

redwoods `apply.sh` klont Mu-Silicium **auf dem BASE-Commit** und patcht. Problem: redwoods BASE
(`e51cd877…`) ist evtl. älter als das Einchecken von `renoirPkg`. Zwei saubere Wege:

**Weg A — auf aktuellem Mu-Silicium (empfohlen, weil renoirPkg dort existiert):**
```sh
git clone https://github.com/Project-Silicium/Mu-Silicium.git
cd Mu-Silicium
git submodule update --init --recursive
# HakiOS' zwei generische Änderungen von Hand/gepatcht einbringen:
#  - BootShim/BootShim.S            (XNUP-Copy-Block)
#  - .../BootManagerLib/BootManager.c + .inf   (XnuLoaderMain + Sources/Protocols)
# redwoods mu-silicium.patch als Vorlage; bei Drift seit redwoods BASE kleine Handarbeit.
cp <redwood>/files/Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/XnuLoader.[ch] \
   Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/
# renoir-spezifisch: XNU-Payload-Region in renoirPkg/MemoryMapLib.c eintragen (siehe oben)
```

**Weg B — redwoods BASE-Commit:** nur falls renoirPkg dort schon existiert (`git -C Mu-Silicium
cat-file -e $(cat BASE):Platforms/Xiaomi/renoirPkg` prüfen). Meist ist Weg A richtig.

## Bauen und erstes Erfolgssignal

```sh
python3 Platforms/Xiaomi/renoirPkg/DeviceBuild.py --setup
python3 Platforms/Xiaomi/renoirPkg/DeviceBuild.py --update
python3 Platforms/Xiaomi/renoirPkg/DeviceBuild.py -t RELEASE
# -> Build/renoirPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd
```

Danach packt HakiOS' `mkxnuimg.py` (via `./xl enter`) das FD + den XNUP-Container in ein
`boot.img`; `fastboot boot boot.img`.

**Das erste Ziel ist NICHT „iOS bootet".** `XnuLoader.c` Stage 0 ist reine Diagnose: findet BDS
nichts zu booten, sammelt es Exception-Level, CPU-Feature-Register, Framebuffer, Memory-Map und den
Payload, schreibt alles als **Text auf die rohe GPT-Partition `logdump`** und macht Cold-Reset —
nichts auf dem Schirm (das Telefon läuft unbeaufsichtigt). Erstes Erfolgssignal:

```sh
# nach dem fastboot boot, zurück in MIUI / per Recovery:
adb shell su -c 'dd if=/dev/block/by-name/<logdump-partition>' > logdump.txt   # oder passende Partition
```
Wenn `logdump.txt` die EL/Register/Framebuffer-Angaben enthält → UEFI + Stage-0-Loader laufen auf
renoir. **renoir-To-do:** eine entbehrliche Partition als `logdump` festlegen (siehe `XnuLoader.h`:
`XNU_LOG_PARTITION L"logdump"`); redwood nutzt eine, die es gefahrlos überschreiben kann.

## Checkliste Phase 4
- [ ] Mu-Silicium (aktuell) klonen, Submodule init.
- [ ] `BootShim.S` XNUP-Block einbringen; `PAYLOAD_BASE=0xF0000000` für renoirs BootShim-Build setzen.
- [ ] `BootManager.c`/`.inf` patchen, `XnuLoader.[ch]` kopieren.
- [ ] `XNU Payload`-Zeile in `renoirPkg/MemoryMapLib.c` eintragen, Adresse gegen renoirs Map prüfen.
- [ ] `logdump`-Partition für renoir wählen.
- [ ] `renoirPkg` RELEASE bauen, `boot.img` packen, `fastboot boot`.
- [ ] `logdump` zurücklesen → Stage-0-Diagnose vorhanden = Phase 4 erreicht.
