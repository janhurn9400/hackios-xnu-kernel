# Handoff for the next session (written 2026-10-28, ~17:45, end of session 2)

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
