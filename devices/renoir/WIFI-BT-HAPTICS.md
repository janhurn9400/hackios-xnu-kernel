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
