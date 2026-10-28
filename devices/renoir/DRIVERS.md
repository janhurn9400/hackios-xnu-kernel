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

### 1. Touch — `hakios.goodix.c` (❗ größte Unbekannte)
redwood fährt **Goodix GT9916 ODER FocalTech FT3680** auf dem **SPI-Serial-Engine 13** der SoC,
macht Power/Clocks/Pins/SPI komplett selbst (kein Framework im Kernel), Boot-Arg `touchscreen=1`,
Polling 125×/s (`touch_poll_us=N`). Panel-Discriminator bei redwood: `GPIO96=1 & GPIO68=0` →
FocalTech, sonst Goodix.

**renoir:** Touch-Controller ist im One-UI-Repo **nicht** dokumentiert → **aus renoirs DT lesen**
(`cellular_probe.py`-Ansatz / den Touch-Node im `/sys/firmware/fdt` suchen: Goodix/FocalTech/
Novatek?). Danach in einer renoir-Variante des Treibers: Serial-Engine-Nummer, GPIOs (Reset/IRQ/
Supply), Regulator (bei redwood LDO2 des PM8350C über RPMh) an renoirs Werte anpassen. redwood
nennt als Register-Quellen die Vendor-Linux-Treiber (`spi-geni-qcom`, `qcom-geni-se`, `rpmh-rsc`,
`cmd-db`, `gcc-sc7280`, `pinctrl-sc7280`, `goodix_berlin_*`, `focaltech_core`) + das DT — für renoir
die SM7350-Pendants.

### 2. Storage — `hakios.ufs.c` / `hakios.ufshc.c`
UFS-Host-Controller; Registerbasis aus renoirs DT. Weg wie redwood, nur Adresse tauschen. Ohne UFS
mountet der iOS-Userspace nicht → kein Lock/Home-Screen.

### 3. GPU-Beschleunigung — `hakios.adreno.c` (nah an redwood)
**Adreno 642 (renoir) ≈ Adreno 642L (redwood)** — gleiche a6xx-Familie, selbes SoC-Paket. Das ist
**Anpassung, kein Bring-up von null**. Der Treiber geht in ~18 nachvollziehbaren Schritten hoch
(`adreno=N` hält nach N an, liest vor dem Schreiben, bricht ab statt blind weiterzumachen —
gebaut fürs Arbeiten unter Qualcomms Hypervisor). renoir-spezifisch: GPU-/GMU-/SMMU-Adressen und
die Firmware-Namen (redwood: `fw/a660_{gmu.bin,sqe.fw,zap.mdt}` — renoirs echte Namen via
`pull-firmware.sh`-Glob). TrustZone-Signaturprüfung des „zap shader" wie bei redwood.

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
| `hakios.goodix.c` | Touch-Chip, SPI-Engine, GPIOs, Regulator | **renoirs DT** (noch zu ermitteln) |
| `hakios.ufshc.c` | UFS-Controller-Reg | renoirs DT |
| `hakios.adreno.c` | GPU/GMU/SMMU-Adressen, Firmware-Namen | renoirs DT + `pull-firmware.sh` |
| Helligkeit/Refresh | max 2047, 90 Hz, HWC-Float | One-UI-Repo (siehe Tabelle oben) |
| `hakios.wifi.c` | WLAN-Chip, Firmware | `pull-firmware.sh` (`fw-wifi/`) |
