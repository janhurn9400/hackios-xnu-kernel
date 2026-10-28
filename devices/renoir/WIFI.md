# renoir Wi-Fi: QCA6490 on PCIe (started 2026-10-28, session 5)

Facts read off MagicOS are in `WIFI-BT-HAPTICS.md`. redwood's Wi-Fi (`hakios.wifi.c` steps 1-16, a WCN6750
inside the SoC, firmware on the WPSS DSP, ath11k over AHB) does not carry over: renoir's radio is a chip of
its own on the first PCIe controller and runs its firmware itself once the host hands it over through MHI.
What can be reused: ath11k on the LinuxKPI layer (`tools/mk-ath11k.sh`, `wifi/lx`, `hakios.lx.c`), the
QMI/QRTR code (`hakios.qmi.c`, `hakios.wlfw.c`), the station and iOS side (`hakios.sta.c`, `hakios.apple80211.c`),
the SMMU context code (`radio_space_*` in `hakios.wifi.c`).

## What the hardware needs (device tree)

- **Controller** `qcom,pcie@1c00000` (`qcom,pci-msm`): PARF 0x1c00000, PHY 0x1c06000, DBI 0x60000000,
  ELBI 0x60000f20, iATU 0x60001000, config 0x60100000; memory window 0x60300000 + 0x3d00000; SMMU stream
  0x2400 (+1); `msi-parent` 0x136; boot-option 1 (Linux brings the link up only when cnss asks).
- **Power domain** `gcc_pcie_0_gdsc` at 0x16b004. **Clocks** (GCC, SM8350 layout, `gcc-sm8350.c`): votes in
  0x152008 bits 0-5 (slv_axi, mstr_axi, cfg_ahb, aux, pipe, slv_q2a) and 0x152000 bits 12 (aggre_noc_pcie_0),
  22 (phy_rchng); branch status at 0x16b010-0x16b038, 0x16b080; clkref 0x18c004; aux source 0x16b058 (19.2 MHz,
  `qcom,use-19p2mhz-aux-clk`); pipe mux 0x16b054. Resets: BCR 0x16b000, PHY 0x16c01c.
- **PHY init**: the whole register sequence is in the tree (`qcom,phy-sequence`, ~95 writes), status
  0x214 bit 6, power down 0x240, `qcom,pcie-phy-ver` 0x2aef.
- **Pins**: PERST# gpio94, CLKREQ# 95, WAKE# 96; radio enable gpio64 (`wlan-en-gpio`), BT enable 65.
- **Supplies** (RPMh): vdd-wlan-aon/dig PM8350C S2 (0.95 V), io PM8350 S10 (1.8 V), rfa1 PM8350 S11
  (1.88-1.98 V), rfa2 PM8350 S12 (1.35 V), antenna switch PM8350C L11 (2.8 V); PCIe 0.9 V / 1.8 V and CX.
- **MHI** (cnss node): 30 channels, IPCR is what QRTR/QMI runs over; buffer 32 KB, timeout 10 s.
- **Firmware**: `amss20.bin` (firmware_mnt), `m3.bin`, `regdb.bin`, board data `bdwlan.*` /
  `bd_k9*.elf` (k9 = renoir).

## Plan

1. **Survey, read only** (`board=renoir pcie=1`, `pcie=2` also reads the controller if its AHB clock runs):
   is anything of PCIe 0 on after UEFI? Kernel `hakios.wifi.c` `renoir_pcie_survey()`. <- now
2. **Link up**: supplies on (RPMh, as `hakios.goodix.c`/`hakios.adsp.c` vote), gpio64 high, GDSC on,
   clocks voted, PHY sequence, PERST# released, LTSSM enable (PARF), wait for link; read config space of
   0000:01:00.0: expect 17cb:1103. Risk: the hypervisor may keep GCC/PCIe for itself (the WPSS clocks on
   redwood were).
3. **iATU + BAR**: map BAR0 of the chip in the 0x60300000 window; read MHI registers (MHIVER, BHI).
4. **MHI host, ours**: Linux's MHI core is GPL-2.0, so it cannot be taken; ath11k's own `pci.c`/`mhi.c`
   are BSD-3-Clause-Clear and can. Needed: BHI/BHIe image download (`amss20.bin`), the MHI state machine
   to mission mode, context arrays, event rings, channels IPCR (20/21) -- behind Linux's `mhi.h` API so
   that ath11k's `mhi.c` runs unchanged.
5. **SMMU** for stream 0x2400 (as redwood's `radio_space_on`), MSI (the tree's `qcom,pcie-msi`, SPIs).
6. **ath11k_pci** for QCA6490 ("hsp") on `wifi/lx`, QMI over MHI's QRTR, then the station as on redwood.
