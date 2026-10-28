# Where the renoir port stands, and what to do next (2026-10-28, end of session 1)

Read first: `WORKFLOW.md` (Mac + VM, USB re-plug, run loop), `PORTING.md` (done/not done with log
lines), `TOUCH.md` (all touch facts), `git log renoir`.

## Done, on the real phone

UEFI (renoirPkg) boots; XNU loader stage 0; the prebuilt XNU kernel boots with `hello` as process 1;
all seven kernel cores (`small_cores_after=0`), cpufreq 1900/2208/2400 MHz; UFS reads (`ufs=2`, all
110 partitions, nothing written). The prebuilt kernel and loader work unchanged on renoir.

## Next steps, in order

1. **Kernel toolchain in the VM** (not run yet): in the VM, as root,
   `bash /root/hackios-xnu-kernel/devices/renoir/setup-kernel-vm.sh` (from the Mac:
   `devices/renoir/vmx.sh 'cd /root/hackios-xnu-kernel && git pull -q origin renoir && bash devices/renoir/setup-kernel-vm.sh'`,
   as a background job). It downloads the aarch64 iOS cctools (L1ghtmann/llvm-project), the
   iPhoneOS16.5.sdk (theos/sdks) and Apple open source, then builds. The last attempt to start it from
   the assistant was **blocked by Claude Code's auto-mode permission check** (downloads + build as root
   in the VM); the user has to allow it or start it once by hand. Expect `xnu-build.sh -k` to end with
   an error in `check_all_exports`: judge by the kernel file's date. First build the kernel
   **unchanged** and boot it with `run-open.sh` (set the kernel path, or copy it over
   `prebuilt/mach.development.bcm2837` locally) to prove the toolchain.
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
