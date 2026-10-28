# Where the renoir port stands, and what to do next (2026-10-28, end of session 1)

Read first: `WORKFLOW.md` (Mac + VM, USB re-plug, run loop), `PORTING.md` (done/not done with log
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
2. **Touch driver** (`xnu/files/bsd/dev/hakios.goodix.c`, edit in `xnu-src/` then `xnu/export.sh`
   per `xnu/README.md`): a renoir board profile chosen by boot-arg `touch_board=renoir`, redwood stays
   the default. All constants are in the table in `TOUCH.md` (SE 0x988000 = QUP0 SE2, pins 20-23
   function 1, reset 30, int 31, avdd = RPMh `ldoc7` 3.0 V instead of gpio 107, iovdd `ldoc2` as on
   redwood, GCC vote bits 9/8/6/7/12 and offsets 0x17004/0x17008/0x1726c/0x17270). Chip on this unit:
   FocalTech, runtime ID 0x56/0x52, **firmware in its own flash: never upload or flash**; accept
   0x56/0x52, stop on boot-ROM ID 0x56/0xB2. SPI framing and the 15-bit report are already right.
   Test: `run-open.sh 30 cpus=1 touchscreen=1 touch_board=renoir -v`, then touch the screen.
3. Then: IPSW + system image (`sysimage/fetch.sh`, `REST=1 userland/build.sh`, ~7.4 GB) for the lock
   screen; GPU (`gpu=1`, chip id 0x06060010 vs redwood's 0x06030500); haptics (renoir has
   `qcom-hv-haptics`, not Awinic).

## State of the machines at the end

- Phone: in MagicOS (rooted). VM: running, Mu-Silicium tree built with HakiOS + the on-screen debug
  of `uefi/debug-onscreen.py` (harmless: only shows when no `loader.efi` is in the payload).
- `devices/renoir/prebuilt/` (untracked): `xnu-usb-renoir.img` (the USB loader image to
  `fastboot boot`), UEFI images, `logdump.orig.img` (backup of sde55).
