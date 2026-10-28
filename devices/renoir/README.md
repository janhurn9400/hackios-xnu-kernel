# Device: Xiaomi Mi 11 Lite 5G (`renoir`)

Everything in HakiOS that belongs to this one phone lives here. The kernel, loader and user space
above know nothing about the phone: addresses, interrupts, power rails and firmware come from its
device tree and its Android (MIUI). This directory mirrors upstream `devices/poco-x5-pro`
(`redwood`); renoir is the sibling SoC, so most of redwood applies with its numbers swapped.

| What | Where |
|---|---|
| SoC | Qualcomm Snapdragon 780G (**SM7350**); confirm the device-tree compatible string (likely `qcom,sm7350`) |
| UEFI for the phone | **already exists upstream**: Mu-Silicium `Platforms/Xiaomi/renoirPkg` (CedrosPkg SoC pkg, same as `lisa`). See `uefi/README.md` — you only replay HakiOS's bootshim/loader changes onto it. |
| Prebuilt UEFI images | `prebuilt/`: `SILICIUM_UEFI.fd`, `BootShim.bin`, `device-tree.dtb` (renoir's own), `ramdisk`, `mkbootimg.py` — **not committed** |
| The phone's firmware | **not in the repository**: `pull-firmware.sh` pulls it off renoir over `adb` into `fw-*/` at the repo top |
| Tools | upstream `tools/soc-args.py` (DT → `soc.args`), `tools/cellular_probe.py` (read-only modem inventory) |

## Known vs. must-confirm hardware map

The driver work in HakiOS lives in `xnu/files/bsd/dev/hakios_*.c` (display, touch, UFS, GPU, Wi-Fi,
audio DSP, modem, haptics, cameras). For renoir, each must be checked against renoir's components.
What is reasonably known vs. what you must read off renoir's device tree / `/vendor`:

| Subsystem | redwood (reference) | renoir | Action |
|---|---|---|---|
| GPU | Adreno 642L, chipid `0x06030500`, `a660_*` firmware | **Adreno 642, chipid `0x06060010`, `a660_*` firmware ✓** | Same GPU/GMU/SMMU addresses (`3d00000`, `3d6a000`, `3da0000`) and firmware names; check where `hakios.adreno.c` depends on the chip id / speed bin |
| Display | DSI panel, per-device brightness table | AMOLED 1080×2400, 90 Hz; this unit's panel `k9_42_02_0b_mp_dsc_cmd` | Picture comes from UEFI framebuffer (GOP); brightness/refresh facts in One UI repo (see `DRIVERS.md`) |
| Touch | Goodix GT9916 / FocalTech FT3680 on SPI (SE 13) | **Goodix GT9897 or FocalTech on SPI ✓** — node `spi@988000/xiaomi_k9_touch@0` (`xiaomi,k9-spi`, 6 MHz), reset GPIO 30, IRQ GPIO 31, `avdd`/`iovdd` supplies; **this unit: FocalTech** (`fts_ts`) | Same bus and the same two vendors as redwood: retarget `hakios.goodix.c` to SE @ `0x988000`, GPIOs 30/31, renoir's regulators and vendor detection. (`i2c@988000/focaltech@38` in the DT is a disabled Qualcomm reference leftover) |
| Storage | UFS | UFS ✓ | `ufshc@1d84000`, same three register ranges as redwood |
| Wi-Fi | WCN6750 | likely WCN6750-class | Not in LineageOS blob list; `pull-firmware.sh` wpss.* on-device; `hakios.wifi.c` |
| Audio DSP | ADSP (adsp.*) | ADSP ✓ | Pull `adsp.*`; battery/charging ride through it |
| Haptics | Awinic aw8697 | **Qualcomm PMIC `qcom-hv-haptics` ✓** | Different chip from redwood; effects as `N_click_P_RTP.bin` → `effect_N.bin`; `hakios.haptic.c` needs a new backend |
| Modem | MPSS, IPA 4.11 | same SoC addresses, IPA firmware `ipa_fws` | `soc.args`; cellular unfinished upstream anyway |
| Cameras | QTI sensormodule | QTI sensormodule | Pull camera tuning bins on-device |

> ✓ = read on a real renoir (2026-10-28: MIUI 14.0.7 TKIMIXM vendor, kernel 5.4.233, its
> `/sys/firmware/fdt` and `/proc/bus/input/devices`) or from LineageOS `android_device_xiaomi_renoir`
> (lineage-21) `proprietary-files.txt`. renoir ships two touch vendors; the Goodix firmware sits in
> `/vendor/firmware` on every unit, so the input device name says which chip a unit has.

## Firmware

```sh
devices/renoir/pull-firmware.sh      # renoir in MIUI, on adb
```

Pulls GPU (`fw-gpu/`), Wi-Fi (`fw-wifi/`), audio DSP (`fw-audio/`), haptics (`fw-haptic/`),
modem (`fw-modem/`, with `image/` for its TFTP) and cameras (`fw-camera/`). All vendor, all out of
git.

## SoC addresses (the port's core mechanical step)

```sh
devices/renoir/pull-soc.sh      # adb pull /sys/firmware/fdt, then upstream tools/soc-args.py -> soc.args
```

`soc.args` holds renoir's values, read off a real renoir. **All of them equal redwood's kernel
defaults except `soc_ipa_fw=ipa_fws`** (redwood: `yupik_ipa_fws`). `tools/soc-args.py` misses three
on renoir because the nodes are named differently; they are in `soc.args` by hand: `soc_aoss`
(`qcom,qmp-aop@c300000`, compatible `qcom,qmp-mbox`), `soc_cmd_db` (`reserved-memory@80860000`,
compatible `qcom,cmd-db`), `soc_mpss_mem` (`mpss_wlan@8b800000`).

`/sys/firmware/fdt` is not readable by the adb shell on renoir, so `pull-soc.sh` copies it as root
and pulls the copy (`su -c cat` through adb runs in a pty and turns 0x0a into 0d 0a).

## Running (once ported)

From the repo top, per upstream: `./xl enter`, `./xl ramdisk`, `./xl run`, or `make phone`.
