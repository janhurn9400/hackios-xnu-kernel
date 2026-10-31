# renoir Wi-Fi, Bluetooth, vibration: facts read off the phone (2026-10-28, session 3)

Read off renoir in MagicOS (root): dmesg, lsmod, `/vendor/firmware*`, `/proc/bus/input/devices`,
PCI, and the saved device tree. Raw material in `fw-collect-renoir/` (git-ignored: vendor data).
**Nothing of this is in code yet.** redwood has a different Wi-Fi (WCN6750 over WPSS, `hakios.wifi.c`
+ ath11k shim `hakios.lx.c`) and an Awinic haptic chip (`hakios.haptic.c`): all three are new work.

## Wi-Fi: Qualcomm QCA6490 on PCIe

- PCI `0000:01:00.0` = **`17cb:1103`** (QCA6490), root port `0000:00:00.0` `17cb:010f`, controller
  `qcom,pcie@1c00000` (`qcom,pci-msm`: parf 0x1c00000, phy 0x1c06000, dbi 0x60000000, iatu
  0x60001000, conf 0x60100000; perst gpio 94, wake gpio 96; SMMU SID base 0x2400; L0s off).
- Android drivers: `cnss2` (`qcom,cnss-qca6490@b0000000`), `wlan` (qca_cld), MHI. dmesg:
  `cnss_pci 0000:01:00.0` loads **`amss20.bin`** from `/vendor/firmware_mnt/image`; `cnss-daemon`.
- Power: `wlan-en-gpio` 64, (`qcom,bt-en-gpio` 65); supplies vdd-wlan-aon/dig (0.95-0.952 V),
  io (1.8 V), rfa1 (1.88-1.98 V), rfa2 (1.35 V), ant switch (2.8 V) -- RPMh regulators by phandle.
- Firmware: `amss20.bin` (main, firmware_mnt), `/vendor/firmware/wlan`: `m3.bin`, `regdb.bin`,
  board data `bdwlan.e01..e96`, `bdwlan.elf`; firmware_mnt also has renoir's own `bd_k9.elf`,
  `bd_k9gl.elf`, `bd_k9jp.elf` (k9 = renoir). Which board file the running driver picks: not seen.
- Linux's way: `ath11k_pci` supports QCA6490 (WCN6855 family, "hsp"): MHI over PCIe, QMI, the same
  firmware names. A port means a PCIe host (or the UEFI's?), MHI, then ath11k on `hakios.lx.c`.

## Bluetooth: the QCA6490's BT ("hastings") on a UART

- `persist.vendor.qcom.bluetooth.soc` = **hastings**; DT `bt_qca6490` (`qcom,qca6490`): reset gpio
  65, `bt-sw-ctrl` gpio 91, `wl-reset` gpio 64, the same supplies as Wi-Fi.
- UART: **`qcom,qup_uart@99c000`** (`qcom,msm-geni-serial-hs`, alias `hsuart0` = `/dev/ttyHS0`),
  pins CTS 68, RTS 69, TX 70, RX 71 (QUP0 SE7, "se15" in the pinctrl names).
- Firmware in `/vendor/bt_firmware/image`: **`hpbtfw20.tlv`** (patch), `hpbtfw20.ver`, NVM
  **`hpnv20.bXX`** (one per board id). Modules `btpower`, `bt_fm_slim`.
- Linux's way: `hci_qca` (QCA6490 = `QCA_QCA6490`), H4 over the UART, download of the .tlv and the NVM.
  iOS's Bluetooth stack talks to Apple's own chips; this is a large project.

## Vibration: Qualcomm PMIC haptics (not Awinic)

- Input device `qcom-hv-haptics`, DT **`qcom,spmi@c440000/qcom,pm8350b@3/qcom,hv-haptics@f000`**:
  SPMI slave 3 (PM8350B), registers **0xf000** (config), **0xf100** (pattern), **0xf200** (power);
  interrupt `fifo-empty`; `qcom,vmax-mv` 2450, LRA period 4808 us (~208 Hz), f0 min/max/default
  0xc3/0xd2/0xcb, brake mode 1 / pattern, `hpwr` 3600 mV, SDAM calibration (`hap_cl_brake`,
  `hap_cfg_sdam`), PBS client; six effects (`effect_0..5`: click, double click, tick, thud, pop,
  heavy click -- the same RTP effects as MagicOS's vibrator HAL). Also `swr_haptics@f0170220` (the
  audio path's SoundWire haptics, `swr_haptics_dlkm`).
- Linux's way: `drivers/input/misc/qcom-hv-haptics.c` (downstream): SPMI register writes. A port needs
  SPMI access from the kernel (PMIC arbiter at `c440000`) -- likely the simplest of the three.

## Order suggested

Vibration first (SPMI writes, one register block), then sound (`SOUND.md`), then Wi-Fi (PCIe + MHI +
ath11k), Bluetooth last.

## Vibration works (session 4, 2026-10-28 evening)

- `haptic=4 haptic_ms=500 haptic_amp=0xff` / `0x30`: strong and weak pulses felt and told apart, with
  2450 mV, without the high-voltage boost (PBS). Use `RAMDISK=devices/renoir/prebuilt/vm-kernel/hold`:
  with `hello` the panic at its exit cut a 500 ms pulse short before the registers were put back.
- **`haptic=5` with `board=renoir` drives iOS's haptics** (taps and vibrations felt in iOS, confirmed by
  the user): upstream's service (`/dev/hakios-haptic`, what iOS plays on its Taptic Engine) now calls
  four motor actions (`motor_start/stop/gain/healthy`) that go to redwood's Awinic chip or to renoir's
  PMIC: a tap is 20 ms of direct play, a vibration direct play until iOS stops, the strength follows
  iOS's waveform on the amplitude register (0..0xff). At the run's end the motor is stopped and the
  block put back as found; a PMIC fault stops it for the run.
- Full run: `... board=renoir gpu=1 touchscreen=1 haptic=5` with `system.apfs` and `root-gl.apfs`.

## Bluetooth works in iOS (2026-10-31, session 8)

Ported from the Xiaomi Mi 10 fork (JopangJovan/hackios-xnu-kernel-umi: `umi_bluetooth.c`, `hakios.bluetooth.cpp`,
`inject_bluetooth.c`, `umi_bluetool.c`) to the QCA6490: kernel part at the end of `bsd/dev/hakios.wifi.c` (next to the
radio's PCIe code, using its RPMh supplies), the registry service in `iokit/Kernel/hakios.wlan.cpp`, userland
`src/inject_bluetooth.c` (bluetoothd, iOS 16.5 20F66 addresses, checked by UUID) and `src/bluetool.c` (BlueTool's
stand-in), `BLUETOOTH=1` in `userland/build.sh` / `mkjobs.py`.
- Hardware: UART QUP0 SE7 `0x99c000` (pins 68-71, function 1), its clock SE7 CBCR `0x1785c`, RCG `0x17860`, vote bit 17
  of `0x52008`; reset/BT_EN gpio 65; the radio's five supplies (smpc2, smpb10/11/12, ldoc11) as Wi-Fi votes them.
- Firmware from MagicOS's `/vendor/bt_firmware/image` (root): `hpbtfw20.tlv`, `hpnv20.bin` (Android's HAL: board ID 0,
  so the default NVM), in `fw-bluetooth/` (git-ignored), put into `fw/` of the small image by `build.sh`.
- `bt_probe=4`: `the ROM answers: HCI 0x9, manufacturer 29, LMP subversion 0x17f3` (product 0x13, ROM 0x200, SoC
  0x400c0200). `bt_probe=5`: patch 813 segments, NVM 26, `the firmware runs: HCI 0xb` (5.2), LE passive scan 33 reports in
  3 s. `bt=1`: the H4 socket `com.hakios.bluetooth`; bluetoothd (`BT_HCI_TRANSPORT=H4:POSIX:com.hakios.bluetooth,115200`)
  initialises it, `Update Dynamic Store with state On value 1`, LE scan and inquiry; **devices show up in Settings ->
  Bluetooth** (the user). Only Apple's vendor command 0xfd48 is refused.
- BlueTool's job must not keep its `com.apple.BlueTool` label: launchd panicked (`unexpected exec of non-platform binary:
  /redwood/bin/bluetool: com.apple.BlueTool`); it is `com.hakios.bluetool` now (as in the Mi 10 fork).
- The firmware download takes ~1 min at 115200 baud; bluetoothd exits until the endpoint is up and launchd restarts it.
- Run: the everyday run plus `bt=1`, kernel `vm-kernel/mach.development.bcm2837.bt`, images `*-bt.apfs`.

## Bluetooth audio: AirPods play (2026-10-31, session 8)

AirPods paired and their settings (ANC/transparency) worked at once, but no sound. Two things were missing:
- **The line**: 115200 baud carries about 90 kbit/s; A2DP needs 250-350. The kernel now does what Linux's `hci_qca.c`
  does: EDL `0xfc48` with `QCA_BAUDRATE_3000000` (0x0e) to the ROM, then SE7's source to 48 MHz (GPLL0 even 300 MHz,
  M/N 4/25, serial clock undivided), HCI Read Local Version at the new speed (back to 115200 if it does not answer),
  and the NVM's tag 17 says 3 Mbit/s so the patched firmware stays there. `bt: the UART at 3 Mbit/s: the controller
  answers`; the firmware loads in a few seconds. `bt_baud=0` keeps 115200. bluetoothd's own `115200` (its env and the
  socket's termios) is only what it is told: it does not reach the wire.
- **The audio server's end**: bluetoothd said `[Server.Audio] Error, unable to sendMsg due to no available xpc client
  connections`: Apple's `BTAudioHALPlugin.driver` (iOS volume, `System/Library/Audio/Plug-Ins/HAL`) was not in the image
  (`select-system.py` leaves that directory out, and `/redwood/hal` replaces it with our device alone). `build.sh` with
  `BLUETOOTH=1` now puts it beside `HakiosAudio.driver`, from `../fw-bluetooth/BTAudioHALPlugin.driver` (git-ignored;
  `7z x sysimage/rootfs.dmg 'System/Library/Audio/Plug-Ins/HAL/BTAudioHALPlugin.driver/*' -r`). `mkjobs.py` with
  `BLUETOOTH=1` also lets bluetoothd's helpers in (`com.apple.BTServer.*`, `bluetoothuserd`).

Then: the AirPods in Control Center's audio menu, `sound_test route` -> `BluetoothA2DPOutput "JantschigPods"`,
`sound_test tone 10 440 660` heard in both ears; bluetoothd: `A2DP Link Adaptive Bitrate for AAC-LC started with initial
bitrate of 241`, latency 161 ms (140 delay report + 21). `Failed to set diversity mode` (Apple's vendor settings) is
harmless. bluetoothd's own log in the kernel log: `OSLOG="com.apple.backboardd com.apple.bluetoothd com.apple.mediaserverd"`.

Images `sysimg/flashboot-renoir-bt3.apfs` + `root-flash-wifi-ssh-bt3.apfs`, kernel `vm-kernel/mach.development.bcm2837.bt3m`.

**Open**:
- **Bluetooth stops 2.4 GHz Wi-Fi**: on the iPhone hotspot at 2.4 GHz (channel 6) traffic stopped ~20 s after joining
  with `bt=1` (201 frames in the first 22 s, 9 in the next 300) and ran without it; on 5 GHz (hotspot with "Maximize
  Compatibility" off, channel 44) both work. The QCA6490's WLAN/BT coexistence (shared antenna) is not set up between
  the two firmwares yet.
- **bluetoothd's stop path crashes**: SIGSEGV at pc 0, lr bluetoothd+0xf397c: its shutdown calls the HCI driver's hook
  at +0x60 of the transport table (`0x1000e656c`), which is null for the H4:POSIX transport; the new bluetoothd then did
  not connect to the socket again (Bluetooth stays off until a reboot). Seen once, 10:32 in a run without audio.
- Safari in these images gets the page but does not draw it (seen with and without Bluetooth): separate.
