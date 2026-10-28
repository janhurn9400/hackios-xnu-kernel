# Phase 5+ — Display & Treiber auf renoir

Aus redwoods echten `xnu/files/bsd/dev/hakios.*.c`-Treibern abgeleitet. Grundprinzip von HakiOS:
**jeder Treiber ist per Boot-Arg einzeln scharf zu schalten und bringt sich Schritt für Schritt
hoch** (z. B. `adreno=N` führt nur Schritte 1..N aus und hält an). So portiert man gegen Logs,
nicht blind. Jede Adresse in redwoods Treibern „gehört diesem Telefon" — für renoir aus renoirs DT
ersetzen.

## Wie „das Bild" entsteht (wichtig: größtenteils gratis)

Es gibt **keinen** separaten Panel-Treiber im Kernel:
- Das **Panel bringt das UEFI hoch.** `renoirPkg` (für Windows-on-ARM gebaut) liefert einen
  linearen **Framebuffer über GOP** — genau das zeigt WoA auf renoir schon an. `renoir.dsc` hat
  `USE_CUSTOM_DISPLAY_DRIVER = 0`, nutzt also den Framebuffer, den der Bootloader hinterlässt.
- `hakios.adreno.c` ist **GPU + beschleunigte Ausgabe** (Adreno 642L bei redwood): „step by step up
  to a picture that it fills and the panel shows", Boot-Arg `gpu=1`, sonst Software-Rendering.

→ **Konsequenz:** Das erste sichtbare Bild / der Framebuffer kommt vom UEFI, nicht von renoir-XNU-
Code. Der renoir-spezifische Kernel-Aufwand liegt bei **Touch, Storage, GPU-Beschleunigung**, nicht
beim bloßen „etwas anzeigen". Das macht den Homescreen-Meilenstein deutlich realistischer.

## renoir-Display-Fakten (aus dem One-UI-Repo + bekannten Specs)

| Wert | renoir | Quelle |
|---|---|---|
| Panel | OLED, 6.55" | Gerätespec |
| Auflösung | 1080 × 2400 (FHD+) | Gerätespec |
| Refresh | 60 / 90 Hz | `docs/PLAN.md` |
| Dichte | 440 dpi | `docs/PLAN.md` |
| Backlight | 11-bit, **max 2047**; `panel0-backlight`, lights-HAL schreibt `value*8` | `overlays/.../arrays.xml`, `PLAN.md` |
| Helligkeit-Pfad | `canSetBrightnessViaHwc` (Float über SurfaceFlinger/HWC) | `configs/displayconfig/display_port_130.xml` |
| Nits | ~2.0 … 500 (LineageOS 4.5 … 1000 inkl. HBM) | `display_port_130.xml` |
| Cutout | Punch-hole oben, x ≈ 67–131 px (24–48 dp @ 440) | `overlays/SystemUI.../dimens.xml` |

Diese Werte brauchst du **nicht für den ersten Boot** (Framebuffer kommt vom UEFI), sondern später
für Helligkeit/Refresh-Feintuning auf der HakiOS-Seite. Gut, dass sie schon dokumentiert sind.

## renoir-spezifische XNU-Treiber — Reihenfolge & was zu tun ist

### 1. Touch — `hakios.goodix.c` (✓ Chip bekannt: Goodix GT9897)
redwood fährt **Goodix GT9916 ODER FocalTech FT3680** auf dem **SPI-Serial-Engine 13** der SoC,
macht Power/Clocks/Pins/SPI komplett selbst (kein Framework im Kernel), Boot-Arg `touchscreen=1`,
Polling 125×/s (`touch_poll_us=N`). Panel-Discriminator bei redwood: `GPIO96=1 & GPIO68=0` →
FocalTech, sonst Goodix.

**renoir: Goodix GT9897** (bestätigt aus LineageOS-Blobs: `goodix_gt9897t_fw_k9.bin` /
`goodix_gt9897t_cfg_k9.bin`) — also **Goodix Berlin**, dieselbe Familie wie redwoods GT9916. Das
heißt: **redwoods Goodix-Pfad greift, der FocalTech-Zweig entfällt.** Zu ermitteln bleibt nur das
Drumherum aus renoirs DT: Serial-Engine-Nummer, GPIOs (Reset/IRQ/Supply), Regulator (bei redwood
LDO2 des PM8350C über RPMh). Register-Quellen wie bei redwood (`spi-geni-qcom`, `qcom-geni-se`,
`rpmh-rsc`, `cmd-db`, `gcc-*`, `pinctrl-*`, `goodix_berlin_*`) + renoirs DT.

### 2. Storage — `hakios.ufs.c` / `hakios.ufshc.c`
UFS-Host-Controller; Registerbasis aus renoirs DT. Weg wie redwood, nur Adresse tauschen. Ohne UFS
mountet der iOS-Userspace nicht → kein Lock/Home-Screen.

### 3. GPU-Beschleunigung — `hakios.adreno.c` (nah an redwood)
**Adreno 642 (renoir) ≈ Adreno 642L (redwood)** — gleiche a6xx-Familie, selbes SoC-Paket. Das ist
**Anpassung, kein Bring-up von null**. Der Treiber geht in ~18 nachvollziehbaren Schritten hoch
(`adreno=N` hält nach N an, liest vor dem Schreiben, bricht ab statt blind weiterzumachen —
gebaut fürs Arbeiten unter Qualcomms Hypervisor). renoir-spezifisch sind nur die
GPU-/GMU-/SMMU-Adressen aus renoirs DT — die **Firmware-Namen sind identisch** zu redwood
(bestätigt: `a660_gmu.bin`, `a660_sqe.fw`, `a660_zap.{mdt,b00..b02,elf}`), im Treiber also nichts
zu ändern. TrustZone-Signaturprüfung des „zap shader" wie bei redwood.

### 4. Danach (Tier A, wie redwood)
WLAN (`hakios.wifi.c` + Linux-`ath11k`-Shim `hakios.lx.c`, Firmware `fw-wifi/` + `mac`/`network`),
Audio/Mic (`hakios.adsp.c`/`hakios.sound.c`/`hakios.mic.c`), Kameras, Haptik.

## Reihenfolge der Meilensteine

1. **Framebuffer steht** (UEFI/renoirPkg) → XNU bootet, Panik-/Boot-Log sichtbar, 8 Kerne.
2. **Touch** (`touchscreen=1`) → man kann tippen.
3. **UFS** → iOS-Userspace mountet → Lock-/Home-Screen.
4. **GPU** (`gpu=1`) → beschleunigtes Zeichnen, Apps flüssig.
5. WLAN/Audio/Kamera → Demo „rund".

## Welche renoir-Quelle füttert welchen Treiber

| Treiber | renoir-Input | Woher |
|---|---|---|
| (Framebuffer) | Auflösung/Stride | UEFI GOP (automatisch) |
| `hakios.goodix.c` | Chip = Goodix GT9897 ✓; SPI-Engine/GPIOs/Regulator | renoirs DT (nur noch Drumherum) |
| `hakios.ufshc.c` | UFS-Controller-Reg | renoirs DT |
| `hakios.adreno.c` | GPU/GMU/SMMU-Adressen (FW-Namen = a660, identisch ✓) | renoirs DT |
| Helligkeit/Refresh | max 2047, 90 Hz, HWC-Float | One-UI-Repo (siehe Tabelle oben) |
| `hakios.wifi.c` | WLAN-Chip, Firmware | `pull-firmware.sh` (`fw-wifi/`) |
