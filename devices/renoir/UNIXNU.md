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

## Second look (2026-10-30): how sheng gets smooth frames, and what renoir can take

- **OS**: the only OS profile in UniXNU is `ipados-17.7.11-21H461` (board identity J120AP). No iOS/iPadOS 26 or 27
  profile, no Liquid Glass anywhere in the public repositories (UniXNU, UniXNU-sheng, GopFramebuffer, AdrenoMetal).
- **Why sheng is smooth** (`UniXNU/GopFramebuffer`, `NativeLegacyDisplay.cpp`; sheng record `docs/.vitepress/device-records/sheng.toml`):
  1. CoreAnimation composites on the GPU through Metal: `AdrenoMetal` (Metal frontend) -> metal2vulkan (AIR -> SPIR-V)
     -> Turnip (SPIR-V -> NIR -> IR3) -> `Adreno740` kernel driver. CARenderServer gets the native device
     (`add-gpu-plugin-data.py --publish`). "130 fps idle" at `sheng-vsync=120`.
  2. The surface is the panel's size and BGRA: **no scaling**. The GPU copies it into the GOP framebuffer
     (`native_legacy_gpu_offer`), and the swap is retired early so render and copy overlap (`sheng-early-retire`).
  3. **Direct scanout** (`sheng-dpu-scanout=2`): no copy at all. Each swap the DPU's two VIG pipes get the
     surface's IOVA (SSPP source address +0x14, stride +0x24 at VIG 0x4000/0x6000 of MDP 0xae01000), then CTL 0
     flush (0x15018 = 3); the flush latches within 5.4 ms with no start (command mode: the panel refreshes itself).
     The surface pages are mapped for the display through an SMMU stage-1 window (`native_ufs_dpu_map`).
- **For renoir** (HakiOS keeps 1125 x 2436 -> 1080 x 2400 CPU scaling, 10-12 ms a frame, the 90 Hz limit):
  the same direct scanout on renoir's DPU 7 (its SSPP/CTL offsets from the DT's `qcom,sde-sspp-off`,
  `qcom,sde-ctl-off`; SMMU context of the MDP), or at least the GPU doing the scale/copy (our Adreno driver blits).
  Their A740 driver does not fit renoir's A642 (A6xx); Turnip itself does.

## Newer iPadOS, A6xx GPU, camera and Bluetooth (2026-10-30, evening)

- **iPadOS 26 and 27** live on branch `feat/ipados27-gpu-parity` of UniXNU (and of AdrenoMetal), not on main:
  profiles `ipados-26.5.2-23F84` and `ipados-27.0.1-24A446` (identity iPad17,3 / J820AP, arm64e). How: their own
  XNU built from Apple's public macOS 26.5 source (xnu-12377.121.6, Darwin 26 system calls patched in, since no
  Darwin 26 source exists yet), then **Apple's own kexts taken from the IPSW's kernelcache and prelinked to it**
  (`kits/XnuPrelink`, `--fileset-reference`, roots: IOMobileGraphicsFamily, IOSurface, IOGPUFamily, apfs, AMFI,
  sandbox, IOHIDFamily ...). HakiOS instead replaces those families with its own (MiniIOSurface,
  MiniIOMobileFramebuffer, miniamfi) and stays at iOS 16.5 with a GL compositor; iOS 17+ needs Metal for
  CoreAnimation. Everything runs as an iPad there, phones too.
- **A6xx GPU**: alioth (Poco F3, SM8250, Adreno 650) has a GPU kernel driver from the "pipa" fork
  (`reference/repo/Adreno650`, not pushable/public yet) with AdrenoMetal on top: Metal device published, the
  desktop draws on one branch, SpringBoard loses its device on another. The nearest thing to renoir's A642.
- **Status honesty**: device records mark areas "implemented" while the findings say otherwise (pong: 120 Hz
  "requested, not measured", audio without proven output, Wi-Fi unconfirmed). Read findings, not status.
- **Camera and Bluetooth**: none on any UniXNU device. The Mi 10 HakiOS fork is ahead: Bluetooth (QCA6390 over
  UART into iOS's stack, `umi_bluetooth.c`, firmware htbtfw20.tlv/htnv20.bin) and a camera ladder up to a hardware
  test-pattern frame through IFE-lite (`umi_camera.c`, `umi_camera_dma.c`, CAMERA.md; real sensor frames not yet).
