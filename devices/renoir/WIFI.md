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
   is anything of PCIe 0 on after UEFI? Kernel `hakios.wifi.c` `renoir_pcie_survey()`.
   **Done (22:55, `logs/*-renoir-open.log`): everything is off, and the GCC reads fine from our kernel.**
   GDSC 0x0022f001 (off, sw collapse set); BCRs 0; votes 0x152000 = 0x00e0057f, 0x152008 = 0x083c8000 (no
   PCIe bit); every PCIe 0 branch reads CLK_OFF (0x8000xxxx) except phy_rchng 0x16b038 = 0 (perhaps not
   that register on shima) and clkref 0x18c004 = 1 (on); pipe mux 2, aux RCG 0x80000000 (root off).
   Pins: PERST# 94 input pulled down, CLKREQ# 95 and WAKE# 96 inputs reading 1, Wi-Fi enable 64 and BT
   enable 65 inputs pulled down (the radio is off). So step 2 starts from cold.
2. **Link up**: `board=renoir pcie=3|4|5` (`renoir_pcie_link()` in `hakios.wifi.c`; each level does the
   ones before; everything is taken down again at the end, the supplies stay voted).
   **DONE, 2026-10-28 23:22 (`logs/232251-renoir-open.log`): THE LINK IS UP, Gen 3 x1, and 01:00.0 reads
   `17cb:1103`, class 0x028000, BAR0 0x4 (64-bit memory).** Nothing of it is kept from us by the hypervisor.
   - 3: RPMh votes (DRV 2, slot set 0) smpc2 0.95 V, smpb10 1.8 V, smpb11 1.88 V, smpb12 1.35 V, ldoc11 2.8 V,
     ldoc10 0.88 V, ldob6 1.2 V: all answered. gpio64 high, PERST# low, CLKREQ# function 1. GDSC on
     (0xf822f000). Aux RCG cfg 1 (XO, 19.2 MHz). Votes 0x152008 |= 0x3f, 0x152000 |= bits 11, 12, 18, 19:
     every branch runs.
   - 4: BCR pulses 0x16b000 and 0x16c01c; PARF then reads (sys_ctrl 0x2000361c, phy_ctrl 0x900); phy_ctrl
     bit 0 cleared, PARF_DEVICE_TYPE 4, the 73 writes of `qcom,phy-sequence`: PHY ready at once (0x214 = 0x86);
     pipe mux 0x16b054 = 0 (from the PHY).
   - 5: PARF_DBI_BASE_ADDR 0, PARF_SYS_CTRL 0x3c56, Q2A flush 0, SLV_ADDR_SPACE_SIZE 0x4000000; DBI id
     0x010f17cb; root port buses 0/1/1 + memory/master; PERST# high after 100 ms; LTSSM (PARF 0x1b0 bit 8):
     link status 0x3013 within the first poll, LTSSM 0x111. iATU region 0 (unrolled, 0x60001000) CFG0 to
     bus 1 at 0x60100000.
   - Not done yet: CX level vote (the boot chain's was enough), the supplies are not voted off again (shared:
     a vote of ours replaces the boot chain's for this processor).
3. **iATU + BAR**: `pcie=6`. **DONE, 23:29 (`logs/232853-renoir-open.log`)**: BAR0 is 2 MB (64-bit, reads
   0xffe00004 after all ones), placed at 0x60400000; root port window 0x63f06030; iATU region 1 memory 1:1
   over 0x60300000 + 0x3d00000; command 0x6. **The chip waits in its boot ROM for the firmware:** MHI regs:
   MHIREGLEN 0x100, MHIVER 0xdeadbeef, MHICFG 0x00800080 (128 event rings, 128 channels), CHDBOFF 0x300,
   ERDBOFF 0x700, BHIOFF 0x100, BHIEOFF 0x224, MHICTRL 0, MHISTATUS 0 (state RESET), error 0x8cb4d3a7;
   BHI version 2.1, **EE 0 = PBL**, status 0, serial 0xfa5e5f9d, hardware id 1.
   Caution: with `pcie_pause=2200` (a 2.2 s sleep between the id read and the first config write) the
   write hung the kernel for good (no abort; the phone had to be reset). With `pcie_pause=0` and the link
   watched for 2 s in 500 ms steps (each a config read) it never happened: perhaps the link going idle
   (L1/clock request) with nothing to wake it. Keep the bus busy or look at ASPM/L1 before long pauses.
4. **MHI host, ours** (next): first BHI alone -- the SBL part of `amss20.bin` into DMA memory, BHI_IMGADDR/
   SIZE/TXDB, and EE going 0 -> 1 (SBL) -- before any rings. Then: Linux's MHI core is GPL-2.0, so it cannot be taken; ath11k's own `pci.c`/`mhi.c`
   are BSD-3-Clause-Clear and can. Needed: BHI/BHIe image download (`amss20.bin`), the MHI state machine
   to mission mode, context arrays, event rings, channels IPCR (20/21) -- behind Linux's `mhi.h` API so
   that ath11k's `mhi.c` runs unchanged.
5. **SMMU** for stream 0x2400 (as redwood's `radio_space_on`), MSI (the tree's `qcom,pcie-msi`, SPIs).
6. **ath11k_pci** for QCA6490 ("hsp") on `wifi/lx`, QMI over MHI's QRTR, then the station as on redwood.
