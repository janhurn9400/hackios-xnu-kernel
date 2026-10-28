# Porting HakiOS to renoir — the real checklist

Ordered by "can't boot without it" → "nice to have". Each step says what to do and how you know
it worked. This is a research bring-up: expect to live in boot logs.

## 0. Prerequisites
- [ ] renoir with an **unlocked bootloader**, on MIUI, `adb` + `fastboot` working.
- [ ] A Linux box (or the Docker image upstream ships) to build with.
- [ ] Your own **iOS 16.5 IPSW** (HakiOS fetches/builds the user space from it; no Apple files in git).
- [ ] Clone upstream HakiOS; drop this `devices/renoir/` into its `devices/`.

## 1. UEFI (the biggest piece — mostly already done)
- [x] Existing renoir UEFI found: Mu-Silicium **`renoirPkg`** exists + maintained (same CedrosPkg
      SoC package as redwood's base). See `devices/renoir/uefi/README.md`.
- [x] Build `renoirPkg` **stock** and `fastboot boot` it → confirm UEFI comes up on renoir.
      Done 2026-10-28: built in an aarch64 Ubuntu VM (`uefi/build-renoirpkg.sh`), `fastboot boot
      Mu-renoir.img` on a renoir with unlocked bootloader → Silicium logo, then "Failed to find an
      operating system". Display via the UEFI framebuffer works.
- [x] Apply HakiOS to renoirPkg: `uefi/apply.sh` (redwood's patch + XnuLoader + the `0xF0000000`
      payload region), then `mkxnuimg.py -d renoir`. Done 2026-10-28: stage 0 runs on renoir and
      writes its log (7762 bytes): EL1, boot CPU Cortex-A55 (`MIDR 0x412FD050`), CNTFRQ 19.2 MHz,
      framebuffer `0xE1000000` 1080×2400 stride 1080 BGRA, payload region reserved in the UEFI map.
      **renoir has two `logdump` partitions** (sda30 on LUN 0, sde55 on LUN 4); the loader writes
      sda30, Android's by-name link points at sde55. Read the log with `read-log.sh`.
      `uefi/debug-onscreen.py` makes the loader print a summary on screen (for when the log is empty).

## 2. SoC addresses
- [ ] `devices/renoir/pull-soc.sh` → `soc.args.generated`.
- [ ] Hand-add reserved-memory addresses (`soc_smem`, `soc_cmd_db`, `soc_mpss_mem`,
      `modem_ipa_fw_at`, `soc_imem`); use `tools/cellular_probe.py` to find the nodes.
- [ ] Move into place as `devices/renoir/soc.args`.

## 3. Firmware
- [ ] `devices/renoir/pull-firmware.sh` → `fw-gpu/ fw-wifi/ fw-audio/ fw-haptic/ fw-modem/ fw-camera/`.
- [ ] Note the actual file names (renoir's a6xx GPU blob name especially) and pin them.

## 4. First boot: display + CPU
- [x] No separate panel driver needed — the **UEFI framebuffer (GOP)** gives the picture; XNU just
      consumes it. Driver details + renoir display facts:
      **[`devices/renoir/DRIVERS.md`](DRIVERS.md)**.
- [x] **XNU boots on renoir with one core** (2026-10-28, `run-open.sh`: prebuilt kernel + `hello`,
      `cpus=1 -v`). The loader takes renoir for redwood by its framebuffer address (`0xE1000000`),
      which is right: GIC (`0x17A00000`/`0x17A60000`), PSCI over SMC and the eight cores (MPIDR
      `0x000`-`0x700`, 0-3 small) are the same. Kernel log: `Darwin Kernel Version 22.5.0`,
      `initialize_screen: b=E1000000, w=00000438, h=00000960`, `gic: v3`, `bsd_init`, then
      `Hello from user space on Darwin! I am process 1`; returned after 20 s as asked.
- [ ] All 8 cores (`cpus=8 bootcpu=7 cpufreq=1`).

## 5. Input + storage
- [ ] `hakios_touch` for renoir's touch controller (confirm the part from DT first).
- [ ] UFS up → the iOS user space mounts and you reach the lock/home screen.

## 6. GPU (should be close to redwood)
- [ ] Adreno 642 via Mesa. redwood is Adreno 642L — same family, so this is adaptation, not bring-up.
- [ ] Success = home screen renders, apps draw.

## 7. The rest (expect partial, like upstream)
- [ ] Wi-Fi (`hakios_wifi` + `fw-wifi/mac` + network creds).
- [ ] Audio (speakers/mic via ADSP).
- [ ] Cameras.
- [ ] Haptics.
- [ ] Modem / Bluetooth / charging / Metal / Secure Enclave — **unfinished upstream too**; see the
      "can we fix these ourselves?" note in `WHY-NOW.md`.

## Milestone definition of "done for a demo"
Lock screen → home screen → stock apps → GPU render → Wi-Fi → sound. Matches redwood's working set.
Daily-driver features (cellular, BT, charging, Metal, Apple sign-in) are explicitly out of scope.
