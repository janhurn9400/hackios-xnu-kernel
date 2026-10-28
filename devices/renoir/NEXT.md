# Where the renoir port stands, and what to do next

## Session 4 (2026-10-28 evening), in short

Vibration in iOS (`haptic=5`), hangs land in fastboot, and **sound in iOS** (`sound=4`, two TFA9873s on
TERT_MI2S, 96 kHz). The run command and the open items are at the top of `HANDOFF.md`; all of the sound
in `SOUND.md`. Next: ringtone previews (silent), why stopping the amplifiers silences iOS, a volume for the
TFA9873s; then tearing, Wi-Fi/BT, USB after the DSP.

## Session 2 (2026-10-28), in short

Kernel built in the VM and booted; touch (FocalTech FT5652, `board=renoir`); iOS 16.5 to SpringBoard
with touch; **GPU at 490 MHz** (Adreno 642, Mesa FD660, `gpu=1`, image `root-gl.apfs`); display measured
(tearing is between display controller and panel, not in the framebuffer: `GPU.md`); firmware pulled
(`fw-*`, Wi-Fi is a QCA6490 on PCIe); **ADSP steps 1-6 work, its start resets the phone** (`ADSP.md`).
A full iOS run today:

```
fastboot oem ramdisk; fastboot stage devices/renoir/prebuilt/sysimg/system.apfs
FOLLOW=10 KERNEL=devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837 \
RAMDISK=devices/renoir/prebuilt/sysimg/root-gl.apfs devices/renoir/run-open.sh 600 \
  sb_delay=8 bootcpu=7 cpus=8 cpufreq=1 touchscreen=1 board=renoir gpu=1
```

Next, in order: save the crashed run's kernel log in the loader (to see the ADSP's last line); the
ADSP's start (battery, sound); the status bar clock away from the punch hole (`inject_sb.c`, the
non-notch layout); a crash landing in fastboot instead of MagicOS (the restart reason); tearing (renoir's
real panel node, `msm_drm.dsi_display0=` on MagicOS's command line); Wi-Fi/Bluetooth (QCA6490: PCIe, MHI,
ath11k; UART HCI) as a project of its own.

## Session 1 (end of 2026-10-28 morning)

Read first: `HANDOFF.md`, `WORKFLOW.md` (Mac + VM, USB re-plug, run loop), `PORTING.md` (done/not done with log
lines), `TOUCH.md` (all touch facts), `git log renoir`.

## Done, on the real phone

UEFI (renoirPkg) boots; XNU loader stage 0; the prebuilt XNU kernel boots with `hello` as process 1;
all seven kernel cores (`small_cores_after=0`), cpufreq 1900/2208/2400 MHz; UFS reads (`ufs=2`, all
110 partitions, nothing written). The prebuilt kernel and loader work unchanged on renoir.

## Next steps, in order

1. ~~Kernel toolchain in the VM~~ **done** (2026-10-28, session 2): `setup-kernel-vm.sh` (now with
   `libxml2-dev`, native `gawk` as `awk` -- mawk breaks `init_sysent.c` -- and `xnu-deps/setup.sh`
   always run) builds `mach.development.bcm2837` in the VM (`-j2`; ends with the expected
   `check_all_exports` / `IOKit.symbolset` / lldbmacros errors). The unchanged kernel built there
   boots on renoir: `KERNEL=devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837
   devices/renoir/run-open.sh 20 cpus=1 -v` -> `Darwin Kernel Version 22.5.0: Wed Oct 28 12:14:55
   CET 2026`, `I am process 1`. The iOS 16.5 disk images (`rootfs.dmg`, `cryptex-os.dmg`,
   `cryptex-app.dmg`) are already downloaded into the Mac's `sysimage/` (for step 3).
2. ~~Touch driver~~ **works** (session 2, see `TOUCH.md` for the run and what is open: SPI by hand, the
   engine has its FIFO off). Was: (`xnu/files/bsd/dev/hakios.goodix.c`, edit in `xnu-src/` then `xnu/export.sh`
   per `xnu/README.md`): a renoir board profile chosen by boot-arg `touch_board=renoir`, redwood stays
   the default. All constants are in the table in `TOUCH.md` (SE 0x988000 = QUP0 SE2, pins 20-23
   function 1, reset 30, int 31, avdd = RPMh `ldoc7` 3.0 V instead of gpio 107, iovdd `ldoc2` as on
   redwood, GCC vote bits 9/8/6/7/12 and offsets 0x17004/0x17008/0x1726c/0x17270). Chip on this unit:
   FocalTech, runtime ID 0x56/0x52, **firmware in its own flash: never upload or flash**; accept
   0x56/0x52, stop on boot-ROM ID 0x56/0xB2. SPI framing and the 15-bit report are already right.
   Test: `run-open.sh 30 cpus=1 touchscreen=1 touch_board=renoir -v`, then touch the screen.
3. **System image built, iOS boots to SpringBoard** (session 2): the three iOS 16.5 images downloaded on the Mac
   (`sysimage/*.dmg`, 7.4 GB, linked into the VM's `sysimage/`), `KEEP=1 sysimage/fetch.sh` and
   `REST=1 userland/build.sh` in the VM: `system.apfs` 2.5 GB + `root.apfs`, copied to
   `devices/renoir/prebuilt/sysimg/` (sha256 checked). No `ldid` (a few test programs unsigned), `fw/`
   empty (`pull-firmware.sh` not run).
   **SpringBoard runs on renoir and touch works in iOS** (12:51, `logs/125102-renoir-open.log`): no
   `xl` (no MacPorts gstat/gtimeout here), by hand: `fastboot oem ramdisk`, `fastboot stage
   devices/renoir/prebuilt/sysimg/system.apfs` (2.4 GB, 80 s), then
   `FOLLOW=15 KERNEL=devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837
   RAMDISK=devices/renoir/prebuilt/sysimg/root.apfs devices/renoir/run-open.sh 180 -v sb_delay=8
   bootcpu=7 cpus=8 cpufreq=1 touchscreen=1 touch_board=renoir`. In 180 s SpringBoard and backboardd
   never exited; only searchd (SIGABRT) and some intents extensions. Slow: software rendering (no GPU).
   `-v` writes the kernel console over the picture: leave it out for a clean screen.
   The first image was built under `prlctl exec`'s umask 077: everything 0700/0600, SpringBoard as
   mobile could not open "/" (`failed to open root directory: /: 13`) and launchd restarted it every 5 s.
   `vmx.sh` now sets umask 022; after such a build: `chmod -R go+rX sysimage/{rootfs,cryptex,cryptex-app,20F66*}
   userland` and `SYSTEM=1 REST=1 userland/build.sh` again.
   Was: IPSW + system image (`sysimage/fetch.sh`, `REST=1 userland/build.sh`, ~7.4 GB) for the lock
   screen; GPU (`gpu=1`, chip id 0x06060010 vs redwood's 0x06030500); haptics (renoir has
   `qcom-hv-haptics`, not Awinic).

## State of the machines at the end

- Phone: in MagicOS (rooted). VM: running, Mu-Silicium tree built with HakiOS + the on-screen debug
  of `uefi/debug-onscreen.py` (harmless: only shows when no `loader.efi` is in the payload).
- `devices/renoir/prebuilt/` (untracked): `xnu-usb-renoir.img` (the USB loader image to
  `fastboot boot`), UEFI images, `logdump.orig.img` (backup of sde55).
