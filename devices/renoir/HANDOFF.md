# Handoff for the next session

## Session 5 (2026-10-28, ~22:30-23:05): ringtones, the endless vibration, Wi-Fi started

- **Ringtones play** in Settings and can be chosen: `sysimage/select-system.py` had left `Library/Ringtones/`
  out of the system image (now kept, 17 MB). New image: `prebuilt/sysimg/system-fw-ring.apfs` (built with
  `KEEP=1 sysimage/fetch.sh; GL=1 REST=1 userland/build.sh`, has the ADSP firmware too). Use it instead of
  `system-fw.apfs`.
- **Endless vibration** while browsing ringtones (log `logs/225712-ring-vib.log`): iOS's run ended from cpu 2
  (cause unknown: the panic text is appended to the log only at the end, and the phone reset right after
  the DDR vote, before it). The motor was playing at that moment; renoir's motor is the PMIC's and plays on
  through a reset. Fixed in `loader_return.c`: the motor and the torch are stopped FIRST, the DDR vote is
  made LAST (after the panic text is in the log). Kernel 22:59 has both; running on the phone from 23:03.
  **The freeze itself** (second try, panic text read from `logdump` in MagicOS, `logs/logdump-2306.txt`):
  `kmem_alloc ... failed with 3` in `io_map` from `hv_put()`: the haptic driver mapped the arbiter's write
  channel at every write and never unmapped it. Fixed (mapped once; kernel 23:14, on the phone from 23:17).
  Still open: a reset in the middle of a vibration would
  still leave it on -- the loader could stop the motor at its start (SPMI write, `WIFI-BT-HAPTICS.md`).
- **Wi-Fi: THE PCIe LINK IS UP** (`WIFI.md` step 2): `board=renoir pcie=5 wifi_return=1` (kernel 23:21) powers
  the radio and PCIe 0, runs the PHY sequence, trains the link (Gen 3 x1) and reads `17cb:1103` (QCA6490) at
  01:00.0, then takes it down. Run: `KERNEL=devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837
  RAMDISK=devices/renoir/prebuilt/vm-kernel/hold devices/renoir/run-open.sh 60 bootcpu=7 cpus=1 board=renoir
  pcie=5 wifi_return=1 -v` (no system image needed). **`pcie=6 pcie_pause=0`**: BAR0 (2 MB) placed, MHI's
  registers read: the chip waits in its boot ROM (BHI EE 0 = PBL) for `amss20.bin`. **`pcie=7`** (2026-10-29
  00:00, ramdisk `prebuilt/sysimg/root-wifi.apfs`): **THE PBL TOOK THE SBL through BHI, the chip runs it (EE 1,
  MHI READY)**: SMMU stream 0x2400 alone, BDF-to-SID table empty (`WIFI.md` step 4 has what reset the phone).
  **`pcie=8`** (00:05): **THE WHOLE IMAGE through BHIe, THE CHIP RUNS ITS MISSION FIRMWARE (EE 2 = AMSS).**
  **`pcie=9`** (00:20-00:35): **our MHI reaches M0 and starts the IPCR channels 20/21** (commands complete
  with success); the QRTR hello sent on 20 is not taken yet, while the LOOPBACK channels 0/1 echo our data
  (`pcie_ch=0`): MHI works. **SOLVED 01:30: with MHI started in the SBL (before BHIe; now the default)
  the WLAN firmware says QRTR hello on IPCR and announces WLFW (service 0x45) at node 7 port 1.**
  **`pcie=10` (09:05): QMI with WLFW works over MHI; the firmware asks for memory (0x4e0000 type 1, 0x398000
  type 4), as on Android.** **12:50: memory given (on 4 MB), "memory ready", target capabilities, the board
  data `bd_k9gl.elf` and M3 accepted: "FIRMWARE INIT DONE"** -- the whole of cnss' bring-up up to there.
  Small image `prebuilt/sysimg/root-wifi.apfs` = hold + fw/{amss20.bin, bd_k9gl.elf, m3.bin}. **12:55
  (`pcie=11`): configuration and MODE MISSION taken: the firmware runs as for Android's Wi-Fi.**
  **13:30: ATH11K RUNS ON RENOIR AND SCANS: 5 networks heard** (`devices/renoir/wifi-run.sh scan 240 wifi_lx=2
  wifi_scan=1 wifi_quiet=1 pcie_pause=0 wifi_mac=<the phone's>`; the kernel needs `tools/mk-ath11k.sh` and
  `tools/mk-wlfw.sh` run in the VM before its build). Next: join a network, then Wi-Fi in iOS (`WIFI.md`).
- **The loader now sets the restart reason "bootloader" for every `board=renoir` run** (before the kernel
  starts; `xnuloader/loader.c` `renoir_fastboot_on_reset()`; image `prebuilt/xnu-usb-renoir-crashlog.img`
  09:00): a run that dies early also lands in fastboot. The user wants that always: never MagicOS.
  Test runs: `devices/renoir/wifi-run.sh NAME 110 pcie=9 pcie_pause=0` (stage `oem run`, wait, `fastboot reboot-bootloader`,
  crashlog loader, `oem log`). Runs now end with `fastboot reboot-bootloader` from the loader (the reboot reason stays set by
  default: every restart after a run lands in fastboot), then the crashlog loader and `oem log`. Do not use the default `pcie_pause` with `pcie=6` (a long pause hung a config
  write).
- **Ringtones work without freezing** (user, 23:20) with kernel 23:14.

## Session 4 (2026-10-28, ~19:00-20:55): vibration, crash -> fastboot, and SOUND in iOS

Read first: this, then `SOUND.md` (all of the sound, with the run command and how to get the log after a
DSP run), `WORKFLOW.md`, `ADSP.md`. Branch `renoir`, everything committed, **nothing pushed**.

What works on the phone now (all with `board=renoir`):
- iOS 16.5 to SpringBoard, touch, GPU 490 MHz (`gpu=1`), battery in the status bar (`adsp=1`).
- **Vibration in iOS** (`haptic=5`, PM8350B haptics; `WIFI-BT-HAPTICS.md`), also together with sound.
- **A hang lands in fastboot**, not MagicOS (reboot reason "bootloader" set at every run; `WORKFLOW.md`).
- **Sound in iOS** (`sound=4`): charging chime and keyboard clicks from the two NXP TFA9873s on
  TERT_MI2S at 96 kHz (`hakios.speaker.c` renoir section, `hakios.sound.c` RENOIR_*). Test tone `sound=3`.

The full run used last (20:50, worked):
```
fastboot oem ramdisk; fastboot stage devices/renoir/prebuilt/sysimg/system-fw.apfs     # 80 s, has fw/adsp.*
python3 xnuloader/mkpayload.py -o run-payload.bin kernel=devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837 \
  ramdisk=devices/renoir/prebuilt/sysimg/root-gl.apfs --cmdline "loader_return_after=300 epoch=$(date +%s) sb_delay=8 \
  bootcpu=7 cpus=8 cpufreq=1 touchscreen=1 board=renoir gpu=1 haptic=5 adsp=1 sound=4 sound_below=144"
fastboot stage run-payload.bin; fastboot oem run
```
USB is gone ~3 s after the DSP starts (known, `ADSP.md`). After testing: the user goes to fastboot by keys
(power long + volume down); then `fastboot boot devices/renoir/prebuilt/xnu-usb-renoir-crashlog.img`,
`fastboot oem log; fastboot get_staged FILE` -> the loader's per-second notes of the run (grep `sound: st`).
After every reset the system image has to be staged again.

Kernel in `prebuilt/vm-kernel/` is from 20:54: renoir keeps the amplifiers on by default (`sound_keep_on=1`);
that exact build was not run on the phone yet (the 20:48 one with `sound_keep_on=1` given was).

Open, in order of what the user cares about:
1. **Ringtone previews** in Settings -> Sounds & Haptics are silent and the ringtone cannot be changed (iOS
   side: tone library / `tonelibraryd`?); compare with redwood upstream.
2. **Why stopping the TFA9873s after 3 s of silence silences iOS** (iOS then writes nothing more to
   `/dev/hakios-audio`, haptics stop too). Then stop them on silence again, for the battery.
3. Volume: TFA9873 has no volume register used yet; `sound_below` is applied to the samples (6 dB steps).
   Candidates TDMSPKG 0x5f[9:6], AMPGAIN 0x52. Which speaker plays (earpiece heard; bottom not confirmed).
4. Older items: tearing, Wi-Fi/BT (QCA6490), USB after the DSP, status bar clock under the punch hole,
   Metal (upstream's `oosexgang/hakios-metal` has blits, no shaders yet; UniXNU's Turnip route: `UNIXNU.md`).

Upstream published new docs (2026-10-28): https://hackios-web.vercel.app/en, kernel branch `hakios` of
`oosexgang/hakios-xnu-kernel` (`docs/driver-review/sound.md` lists known sound-code weaknesses S-01..S-16;
S-15, a period whose submission fails is never resubmitted, is a candidate for silences later).

## Session 3 (2026-10-28, ~17:45-18:40): the ADSP is solved, USB after it is not

- **iOS shows the battery** (home screen, status bar) with `adsp=1` and `system-fw.apfs` (ADSP.md).
- **The ADSP works, steps 1-11**: it starts (up after 38 ms), GLINK channels `apr_audio_svc` and
  `PMIC_RTR_ADSP_APPS` open, QRTR hello and 12 services, a battery manager notice, APR "audio services
  are up" (`ADSP.md`, `adsp_step=11 adsp_keep=1`).
- **The "reset" was never the DSP**: once started, the DSP takes the USB-C port over; the loader's USB
  receive then failed forever and its retry loop never fed the hypervisor watchdog, which bit after
  30 s. Fixed in the loader (`813328a`): the phone now stays in the loader after a DSP run.
- **USB does not come back** after a DSP run (the Mac loses the loader 3-5 s after the DSP starts).
  Leave the loader then by a long press on power. Next steps: `ADSP.md` "Next for the ADSP".
- **New loader** (`devices/renoir/prebuilt/xnu-usb-renoir-crashlog.img`, built from `xnuloader/` in the
  VM: `cd xnuloader && ./build.sh`, needs `lzfse`): keeps across a reset the kernel's live log (two slots
  at 0xF3F80000), its own log (ring at 0xF3FC1000) and what `logdump` held at its start, and hands them
  out with `fastboot oem log; fastboot get_staged FILE`. DDR and the whole payload region survive a
  reset and Xiaomi's boot loaders, but not Android starting: go to fastboot by keys right after a reset,
  or rely on the `logdump` notes (every second, 1200 bytes of kernel log), which survive MagicOS.
- New kernel boot-args (`hakios.adsp.c`): `adsp_no_shutdown`, `adsp_no_aop_off`, `adsp_never_stop`.
  The kernel in `prebuilt/vm-kernel/` has them (18:20); the 17:34 one is kept as `.1734`.
- The VM's clone is updated from the Mac's repo through the share (no push needed):
  `git pull --ff-only /media/psf/Home/Desktop/ROMS/hackios-xnu-kernel renoir`.
- Magisk had no `su` in the last MagicOS start (`read-log.sh` cannot read `logdump` then).
- Still nothing pushed to GitHub.

# Session 2's handoff (written 2026-10-28, ~17:45)

Read this first, then `NEXT.md`, `WORKFLOW.md`, `ADSP.md`, `GPU.md`, `TOUCH.md`. Branch `renoir`,
everything committed, nothing pushed. The user wants the ADSP solved next ("gib alles").

## What works on the phone

Kernel built in the VM (`devices/renoir/setup-kernel-vm.sh`), touch (`board=renoir`), iOS 16.5 to
SpringBoard with touch, GPU at 490 MHz (Adreno 642, Mesa FD660). See NEXT.md for the full iOS run
command. ADSP: steps 1-6 work, step 7 (the DSP's start) resets the phone (`ADSP.md`).

## The task now: see why the ADSP start resets the phone

Every crash is too fast for the live log (`FOLLOW`). Plan: **make the loader save the crashed run's
kernel log** and hand it over on the next start.

- The loader is `xnuloader/loader.c` (freestanding AArch64 UEFI app, `xnuloader/build.sh`, built in the
  VM). It loads the kernel at a fixed physical address (`kernel loaded at pa f7004000..f8280000` in the
  run log) and, when a run returns, prints `kernel returned: log at f7b489c0, N bytes` (loader.c ~3381):
  find how it locates the kernel's log (`klog`) and do the same at loader start, BEFORE anything writes
  that memory: if a previous run's log is there (magic / plausible text), copy it into the staged buffer
  (`out_buf`, `fastboot get_staged`) or write it to `logdump`. DDR usually survives a watchdog reset;
  UEFI (Mu-Silicium renoirPkg) may or may not touch that range -- check its memory map (`oem memmap`).
- Then rebuild the USB loader image (`WORKFLOW.md` loop step 2: `xnuloader/mkpayload.py -o usb.bin
  loader.efi=... --usb`, `mkxnuimg.py -d renoir -p usb.bin -o xnu-usb-renoir.img`), copy to
  `devices/renoir/prebuilt/xnu-usb-renoir.img`.
- Reproduce the crash: `adsp_step=7 adsp_pause=0` (see below), the phone resets; boot the loader again,
  fetch the saved log. The last lines say what the DSP's start runs into.

Candidates already ruled out (boot-args, all in `hakios.adsp.c`, off by default): rails level
(`adsp_level=64`), no rails (`adsp_rails=0`), doorbells not enabled (`adsp_ipcc=0`), pauses, proxy
clock (`adsp_xo=1`), crypto engine bandwidth (`adsp_bw=1`). Ideas not yet tried: the DSP's own crash
reason (SMEM item 423 "last words", if SMEM survives the reset); the AOP "load_state on" message
earlier (Linux sends it first, before anything); the order (Linux votes proxies BEFORE PAS init image).

## Test commands (the ADSP tests need no system image upload)

Small image with the ADSP firmware: `devices/renoir/prebuilt/sysimg/root-adsp.apfs` (built with
`ADSP=1 REST=1 userland/build.sh` in the VM). Kernel: `devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837`
(current, 17:34, has every adsp_* option).

```
KERNEL=devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837 \
RAMDISK=devices/renoir/prebuilt/sysimg/root-adsp.apfs \
devices/renoir/run-open.sh 60 bootcpu=7 cpus=1 board=renoir adsp_step=N adsp_pause=0 -v
```

`adsp_step=6` returns cleanly (the kernel then panics at once because the small image has no init:
the run is over in seconds, normal). `adsp_step=7` resets the phone.

## Practical things learnt the hard way

- **VM**: `prlctl list -a`; if "stopped", `prlctl start "Ubuntu 26.04 (with Rosetta)"`. Use
  `devices/renoir/vmx.sh '<cmd>'` (retries, sets umask 022 -- prlctl's 077 broke the system image once).
  Long jobs: `(setsid nohup bash -c "..." > /root/x.log 2>&1 < /dev/null &)` inside the vmx command, then
  poll. Kernel: `cp xnu/files/... xnu-src/xnu-8796.121.2/...` in the VM, `./xnu-build.sh -j2 -k`, the
  error in check_all_exports is normal; copy the kernel to `devices/renoir/prebuilt/vm-kernel/`.
- **Phone states**: in MagicOS adb only sees it after **unplug/replug**; the user prefers to go to
  fastboot by keys (power off, then power + volume down) when asked. From fastboot:
  `fastboot boot devices/renoir/prebuilt/xnu-usb-renoir.img`, then wait for `xnuloader` in
  `fastboot devices` (usually comes by itself within ~15 s). After a hang the user needs a long press on
  power. Tell the user explicitly when to go to fastboot.
- **Live log only with `bootcpu=7`**: the loader runs on core 0; with the kernel there, the loader cannot
  answer during the run. `FOLLOW=N` + a run's end can leave only the loader's log as the final log
  (the last `oem log` replaces the staged kernel log): run without FOLLOW to get the full final log.
- **System image upload** (2.4 GB, `fastboot oem ramdisk; fastboot stage system.apfs`) failed twice with
  `usb_write failed e00002ed`; waiting ~10 s after the loader appears helped. The image stays resident
  across runs until the phone resets.
- A chain that waits for a state that never comes (adb vs fastboot) looks like "nothing happens": kill it
  (`pkill -f run-open.sh`) and start one that waits for what the user did.
- Untracked, local only: `devices/renoir/prebuilt/` (kernels, `sysimg/*.apfs`, decompiled
  `device-tree.dts`), `fw-*/` (the phone's firmware), `sysimage/*.dmg`, `logs/`.

## Other open items (in NEXT.md order)

Status bar clock under the punch hole (top left): swap UIKit's status bar layout to a non-notch one in
`userland/src/inject_sb.c`, apps need the library too. A crash landing in fastboot instead of MagicOS:
write the "bootloader" restart reason (IMEM/PMIC) at the start of each run. Tearing: renoir's real
panel node (MagicOS `msm_drm.dsi_display0=`). Wi-Fi/BT: QCA6490, a project of its own.
