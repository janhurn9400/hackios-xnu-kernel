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
- [ ] Build `renoirPkg` **stock** and `fastboot boot` it → confirm UEFI comes up on renoir.
- [ ] Apply HakiOS to renoirPkg: **step-by-step guide in
      [`devices/renoir/uefi/APPLYING-HAKIOS.md`](uefi/APPLYING-HAKIOS.md)**
      (the one renoir-specific edit = reserve `0xF0000000` XNU-Payload region; first success signal
      = `logdump` partition readback, not a booting iOS).

## 2. SoC addresses
- [ ] `devices/renoir/pull-soc.sh` → `soc.args.generated`.
- [ ] Hand-add reserved-memory addresses (`soc_smem`, `soc_cmd_db`, `soc_mpss_mem`,
      `modem_ipa_fw_at`, `soc_imem`); use `tools/cellular_probe.py` to find the nodes.
- [ ] Move into place as `devices/renoir/soc.args`.

## 3. Firmware
- [ ] `devices/renoir/pull-firmware.sh` → `fw-gpu/ fw-wifi/ fw-audio/ fw-haptic/ fw-modem/ fw-camera/`.
- [ ] Note the actual file names (renoir's a6xx GPU blob name especially) and pin them.

## 4. First boot: display + CPU
- [ ] No separate panel driver needed — the **UEFI framebuffer (GOP)** gives the picture; XNU just
      consumes it. Driver details + renoir display facts:
      **[`devices/renoir/DRIVERS.md`](DRIVERS.md)**.
- [ ] Success = XNU panics/logs on screen, all 8 cores up. **This is the "it boots" milestone.**

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
