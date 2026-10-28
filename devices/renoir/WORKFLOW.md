# How renoir is built and tested here (Mac + Parallels VM)

Read this first when picking the port up in a new session. Progress: `PORTING.md`; hardware:
`README.md`, `DRIVERS.md`, `TOUCH.md`; history with log lines: `git log renoir`.

## Machines

- **Mac (Apple Silicon)**: this fork at `~/Desktop/ROMS/hackios-xnu-kernel`, branch `renoir`
  (`origin` = github.com/janhurn9400/hackios-xnu-kernel, upstream = oosexgang/hackios-xnu-kernel).
  adb/fastboot: `~/Library/Android/sdk/platform-tools` (not on PATH). No Docker, no Linux toolchain.
- **Parallels VM "Ubuntu 26.04 (with Rosetta)"**, aarch64, headless (no Parallels window needed).
  Start it with `prlctl start "Ubuntu 26.04 (with Rosetta)"`, run commands in it as root with
  `prlctl exec "Ubuntu 26.04 (with Rosetta)" '<cmd>'` (no SSH key works); better `devices/renoir/vmx.sh '<cmd>'`,
  which retries when Parallels does not open the session (it fails now and then even when idle).
  - only **one** exec session at a time: a second one fails with "Unable to open new session" /
    "Invalid argument"; run long builds as a background job on the Mac and do not poke the VM meanwhile.
  - the Mac's home is `/media/psf/Home` in the VM: copy results to
    `/media/psf/Home/Desktop/ROMS/hackios-xnu-kernel/devices/renoir/prebuilt/`.
  - the VM has a clone of this fork at `/root/hackios-xnu-kernel` (`git pull origin renoir`) and the
    Mu-Silicium tree at `/root/hakios-build/Mu-Silicium` (venv in it, `apply.sh` applied).

## The phone

renoir with unlocked bootloader, running the user's MagicOS port (renoir's own MIUI 14 vendor, boot,
dtbo), Magisk root. Nothing is flashed by any of this; `fastboot reboot` returns to MagicOS.
`/sys/firmware/fdt` needs root (see `pull-soc.sh`). Two `logdump` partitions: `read-log.sh`.

## Loop

1. UEFI (only after UEFI changes), in the VM: `devices/renoir/uefi/apply.sh <Mu-Silicium>`, then
   `python3 build_uefi.py -d renoir -r RELEASE` in Mu-Silicium with its venv.
2. USB loader image, in the VM:
   `xnuloader/mkpayload.py -o usb.bin loader.efi=prebuilt/loader.efi --usb` and
   `mkxnuimg.py -d renoir -p usb.bin -o xnu-usb-renoir.img`, copy to `devices/renoir/prebuilt/`.
3. On the Mac: `adb reboot bootloader`, `fastboot boot devices/renoir/prebuilt/xnu-usb-renoir.img`.
   Wait for "xnuloader" (18d1:d00d) in `fastboot devices`; it often comes by itself within ~15 s. If
   it does not, unplug and re-plug the USB cable (macOS registers it only if it enumerates after the
   loader is up).
4. `devices/renoir/run-open.sh SECS [kernel args]`: stages kernel + `hello` + args, runs, fetches
   the log into `logs/`. `KERNEL=`, `RAMDISK=` (e.g. `prebuilt/sysimg/root-gl.apfs`), `FOLLOW=N`
   (live log), `FOLLOW_KEEP=1`. The system image goes first: `fastboot oem ramdisk`, `fastboot stage
   devices/renoir/prebuilt/sysimg/system.apfs` (80 s; do not touch the cable meanwhile). `fastboot oem
   stop` ends a run early. A run that hangs the phone needs a long press on power.

## Kernel build (works)

In the VM: `bash devices/renoir/setup-kernel-vm.sh` (packages, cctools, SDK, `xnu/apply.sh`,
`xnu-deps/setup.sh`, `./xnu-build.sh -k`), started detached (`setsid nohup ... > /root/kbuild.log &`)
so no exec session stays open. Ends with errors in `check_all_exports`, `IOKit.symbolset` and the
lldbmacros copy: harmless, judge by the kernel's date. Copy the kernel to
`devices/renoir/prebuilt/vm-kernel/` and run it with `KERNEL=... devices/renoir/run-open.sh`.

## A run that hangs the phone comes back in fastboot

With `board=renoir` the kernel writes the "bootloader" reboot reason at the start of every run (PMIC
PMK8350 SDAM 0x7100 register 0x48 = 2 << 1, and IMEM 0x146aa000 + 0x65c = 0x77665500) and puts both
back at an orderly end or a panic's way back. A hang lets the watchdog restart the phone (~40 s), and
Xiaomi's ABL then starts **fastboot**, not MagicOS (tested 2026-10-28 with `crash_fastboot_test=1`,
which hangs the kernel on purpose; with `cpus=1`). The ABL clears the PMIC field after use, so a normal
`fastboot reboot` still goes to MagicOS. `crash_fastboot=0` leaves both alone. Value 1 is "recovery":
it took the phone into MIUI's recovery. A hang in the loader after the kernel came back (the reason
already cleared) still ends in MagicOS. Code: the end of `xnu/files/bsd/dev/hakios.torch.c`.

