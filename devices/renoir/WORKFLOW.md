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
   **Then unplug and re-plug the USB cable**: macOS registers the loader ("xnuloader", 18d1:d00d)
   only if it enumerates after the loader is up; otherwise `fastboot devices` stays empty.
4. `devices/renoir/run-open.sh SECS [kernel args]`: stages kernel + `hello` + args, runs, fetches
   the log into `logs/`. Kernel: `prebuilt/mach.development.bcm2837` (until one is built here).

## Kernel build (not done yet)

`xnu/apply.sh`, `xnu-deps/setup.sh` (theos with its Linux cctools and `iPhoneOS16.5.sdk`, a
MacOSX13.3.sdk mirror, Apple open source), `./xnu-build.sh -j8 -k`. Upstream builds on Linux
x86-64; the VM is aarch64 with Rosetta for x86-64 binaries. Untested here.
