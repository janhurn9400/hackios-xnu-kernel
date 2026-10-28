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
| GPU | Adreno 642L, `a660_*` firmware | **Adreno 642, `a660_*` firmware ✓** | **Identical firmware names** (`a660_gmu.bin`, `a660_sqe.fw`, `a660_zap.{mdt,b00..b02,elf}`) — `hakios.adreno.c` firmware refs need no change |
| Display | DSI panel, per-device brightness table | punch-hole AMOLED, 1080×2400, 90 Hz | Picture comes from UEFI framebuffer (GOP); brightness/refresh facts in One UI repo (see `DRIVERS.md`) |
| Touch | Goodix GT9916 (Berlin-D) / FocalTech FT3680 | **Goodix GT9897 (Berlin) ✓** | **Goodix path applies** — same Berlin family as redwood; FocalTech branch not needed. Confirm SPI engine/GPIOs/regulator from renoir DT |
| Storage | UFS | UFS | Same path, confirm controller reg from DT |
| Wi-Fi | WCN6750 | likely WCN6750-class | Not in LineageOS blob list; `pull-firmware.sh` wpss.* on-device; `hakios.wifi.c` |
| Audio DSP | ADSP (adsp.*) | ADSP ✓ | Pull `adsp.*`; battery/charging ride through it |
| Haptics | Awinic aw8697 | **Awinic-class RTP ✓** | Effects as `N_click_P_RTP.bin` → `effect_N.bin`; adapt `hakios.haptic.c` to renoir's chip |
| Modem | MPSS, IPA 4.11 | confirm IPA version | `pull-soc.sh` + `cellular_probe.py`; cellular unfinished upstream anyway |
| Cameras | QTI sensormodule | QTI sensormodule | Pull camera tuning bins on-device |

> ✓ = confirmed from public sources: LineageOS `android_device_xiaomi_renoir` (lineage-21)
> `proprietary-files.txt`. The remaining items read from renoir's device tree (`pull-soc.sh` grabs
> `/sys/firmware/fdt`) and `/vendor/firmware` on renoir's MIUI. The One UI ROM repo also documents
> renoir's panel/brightness/haptics — reuse it.

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

`tools/soc-args.py` reads renoir's `qcom,ipa`, `qcom,ipcc`, AOSS mailbox, RPMh controller and
`apps-smmu` nodes and prints the `soc_*` boot-args. The memory addresses (`soc_smem`,
`soc_cmd_db`, `soc_mpss_mem`, `soc_imem`) come from renoir's reserved-memory nodes — add by hand.
Unlike redwood (whose values equal the kernel defaults), **renoir's `soc.args` will carry real
differences** — that is the first thing to get right. See `soc.args` for the empty template.

## Running (once ported)

From the repo top, per upstream: `./xl enter`, `./xl ramdisk`, `./xl run`, or `make phone`.
