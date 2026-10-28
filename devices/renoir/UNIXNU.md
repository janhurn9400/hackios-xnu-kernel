# UniXNU as a second source of facts (2026-10-28)

[UniXNU](https://github.com/UniXNU/UniXNU) (unixnu.com) runs iOS/iPadOS 17 on Qualcomm phones and
tablets through a common UEFI layer (DarwinBoot) that loads per-device drivers as kexts, much like
OpenCore. Created 2026-10-19, very active. **We stay with HakiOS for renoir** (its upstream, redwood
SM7325, is renoir's sibling: same register blocks, Adreno 6xx, PMICs; and here kernel, touch, iOS,
GPU, vibration and the ADSP already work). UniXNU has no SM7350 support; its working drivers are for
SM8550 (sheng, Xiaomi Pad 6S Pro); SM8350 devices are at "boot image built". Use it for facts.

## Useful for renoir

- **Reboot reason** (for "a crash lands in fastboot, not MagicOS"): the SM8350 port (haydn,
  `xiaomi-haydn-dev/UniXNU-haydn` PORTING.md) uses IMEM `restart_reason` at +0x65c. renoir's DT has
  the same: `qcom,msm-imem@146aa000`, `restart_reason@65c` (Linux writes 0x77665500 there for
  "reboot bootloader"), plus a PMIC nvmem cell `qcom,reboot-reason` (`restart@48`) that newer ABLs read.
- haydn (SM8350) facts that match renoir's family: CNTP_* traps under Qualcomm's hypervisor (only the
  virtual timer works), GICD 0x17a00000 / GICR 0x17a60000, SPMI 0xc440000, FocalTech touch on GENI SPI,
  touch extent **8640 x 19200** (the same as we measured on renoir). haydn's display is DSC command mode
  at 120 Hz; there is also an iOS 16 route on haydn ("SWEET", outside UniXNU) that reached launchd.
- **Display**: UniXNU's display drivers also copy into the GOP framebuffer, with no vblank interrupt and a
  timer for vsync (`sheng-vsync`, `unixnu-nativemobiledisplay`): the same state as HakiOS. Nothing to take
  for the tearing yet. sheng has DPU observation probes (`package/ShengDpuObservation.c`,
  `DisplayTimingProbe.cpp`) worth reading when the tearing is looked at again.
- **Metal**: `AdrenoMetal` is metal2vulkan on **Turnip**, Mesa's Vulkan driver, under iOS. Turnip supports
  Adreno 6xx (A642/A660 included), and HakiOS already builds Mesa (freedreno) against its kernel GPU
  interface: Metal on renoir along this route looks possible, unlike their Adreno740 kernel driver (A7xx).
- **ADSP / sound / battery**: sheng has its own GLINK, QRTR, SMP2P, SMEM, QMP, RPMh, battery manager and
  audio code (`UniXNU-sheng/package/Qcom*.c`, `ShengBattmgr.c`, `ShengAudioReach.c`, `ShengCs35l43.c`,
  `ShengGpr.c`). sheng's audio is AudioReach/GPR (newer); renoir's is APR/Q6 like redwood's, so HakiOS's
  sound path fits better. Their host tests (`package/tests/*_host.c`) are a model for testing protocol code.
- **Wi-Fi**: `Ath12kWifi` (sheng: PCIe, MSI, DMA, MHI boot in `package/MhiBoot.c`, `WifiPcieTables.h`) is
  the closest thing to what renoir's QCA6490 (ath11k family, PCIe + MHI) needs.
- **USB**: `NativeDwc3` (nabu, SM8150) is a DWC3 controller driver: relevant for USB after the ADSP takes
  the port (no hardware result documented yet).
