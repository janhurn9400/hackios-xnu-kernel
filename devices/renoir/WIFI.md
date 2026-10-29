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
4. **BHI: the SBL. DONE, 2026-10-29 00:00 (`pcie=7 pcie_pause=0`, ramdisk `prebuilt/sysimg/root-wifi.apfs` =
   `sbin/launchd` (hold) + `fw/amss20.bin`, made with `tools/apfs/mkapfs.py --plain fw`).** The first 512 KB of
   `amss20.bin` in one physical piece, shown to the chip through the SMMU at 0xa0000000 (match register 3,
   context 3, our page tables: `hakios_wifi_radio_map()`), BHI IMGADDR/IMGSIZE, session 0x1234 to IMGTXDB:
   after 20 ms BHI_STATUS 0x80001234 (success), **EE 1 (SBL), MHISTATUS 0x101 (READY)**.
   What it took (each of these reset the phone at the doorbell, within microseconds):
   - a physical address (PCIe 0's streams fault: nobody matches them);
   - stream 0x2401 (the QCA6490's in dt iommu-map) with PARF's BDF-to-SID table filled as Linux does
     (CRC-8 0x15 -> 0x100 << 16 | 1 << 8): the chip's requests still did not come as 0x2401;
   - stream 0x2400 with mask 1 was not taken by the hypervisor (SMR read back unchanged).
   **Working: stream 0x2400, mask 0, table empty** (all devices then offset 0). The hypervisor grants SMR/S2CR
   for 0x2400 and 0x2401 alone. Reboot reason "bootloader" is now set for `pcie>=3` (resets land in fastboot).
5. **BHIe: the whole image. DONE, 2026-10-29 00:05 (`pcie=8 pcie_pause=0`, `logs/001039-bhie-crash.log`).**
   All of `amss20.bin` (5197824 bytes) in 10 pieces of 512 KB, each in one physical piece and shown to the
   chip (0xa0080000 on), the vector table (10 x {address, size}, 160 bytes) at 0xa0580000; BHIe (BHIEOFF
   0x224) TXVECADDR 0x2c/0x30, TXVECSIZE 0x34, sequence 0x55 to TXVECDB 0x3c: **after 540 ms TXVECSTATUS
   0x80000055 (done), 100 ms later EE 2 = AMSS: THE QCA6490 RUNS ITS MISSION FIRMWARE.** MHISTATUS stays
   0x101 (READY), error 0x9cb4d3a7. No MHI context nor M0 was needed for BHIe. (The run takes ~90 s with
   the slow SMMU writes; `run-open.sh 150` gives up before `getvar result` says returned: fetch with
   `fastboot oem log; fastboot get_staged`.)
6. **MHI host, ours: M0 and the IPCR channels DONE, 2026-10-29 00:20 (`pcie=9`, `renoir_mhi()`; log read off
   `logdump` in MagicOS, `logs/logdump-mhi.txt`).** One 64 KB piece shown to the chip at 0xa0584000: channel
   contexts (MHICFG says 128 channels, 128 event rings; we set NER 2), 2 event rings, command ring, rings of
   channels 20/21, buffers. CCABAP/ECABAP/CRCBAP, control/data windows = the SMMU window, event doorbells, MHICTRL
   M0: **event 0x20 (state M0) and 0x40 (EE 2), MHISTATUS 0x201; START for 20 and 21 completed with code 1.**
   The firmware sent nothing on 21 by itself in 3 s: QRTR wants the host's hello first.
   **Open (00:35): the hello on channel 20 is not taken.** The TRE (0xa0590000, 0x34 bytes, type 2, IEOT) at
   0xa058a000 with the channel's doorbell (CHDBOFF 0x300 + 8 x 20) rung, again after 0.5 and 1.5 s: no transfer
   event on event ring 1 (its context read pointer never moves), nothing on 21. Both channel contexts read
   config 0x2 (running; the chip cleared our doorbell-burst bits 0x200) and their read pointer field holds
   0x100000001 (channel 20) / 0x100000002 (channel 21) -- the chip's own use of it? (Linux never reads it.)
   Event rings with MSI vectors 1/2 instead of 0/1 changed nothing.
   **00:40-00:55, found:** (a) **MHI's data path works**: with `pcie_ch=0` the LOOPBACK pair 0/1 echoes our
   52 bytes (transfer events on event ring 1 for both, the bytes back in the buffer of channel 1). So rings,
   doorbells, TREs, buffers and the SMMU are right; it is the WLAN side's IPCR client that never takes
   anything. (b) MSI on (capability at 0x50, 32 vectors, address = a page of ours): the chip writes vector 1
   there (event ring 0), nothing changes for IPCR. (c) Three event rings as dt's `mhi_events` (0 control
   MSI 1, 1 data MSI 2, 2 bandwidth MSI 0, `pcie_rings=`): nothing. (d) Device wake (doorbell 127 = 1):
   nothing. (e) Watched 30 s (`pcie_watch=30000`), doorbells rung again every 5 s: nothing; MHI stays
   0x201, no SMMU fault. The channel contexts' read field holds (type, event ring) -- the chip's own use.
   **Android for comparison** (`fw-collect-renoir/dmesg-full.txt` 14.29-15.13 s): WLAN_EN, link (Gen 2 x1
   there, Gen 3 x1 here), MHI INIT/POWER_ON, amss20.bin, "MISSION_MODE" at 15.075, "Disable ADSP power
   collapse", **WLFW server (node 7 port 1) at 15.13: 55 ms after mission mode.**
   **SOLVED 01:30 (`logs/013003-m0sbl.log`): MHI has to be started in the SBL, before BHIe** (as Linux's
   MHI does: READY -> M0 in the SBL, then the full image). Then in AMSS: EE event 2, channels 20/21
   started, our hello taken (transfer event on 20), and on 21 **the chip's QRTR HELLO from node 7**, then
   **NEW_SERVER: service 0x45 (WLFW), version 1 instance 1, node 7, port 1** -- what Android logs. Default
   now (`pcie_m0sbl=0` for the old order). The MSI page got vector 2 (event ring 1) too.
   Ideas that had been left: what the chip wants in the channel
   context before START (Linux's MHI init for "enabled" channels; pollcfg; the read pointer it overwrote),
   the doorbell value's form, whether channel 20 is IPCR on this firmware (ath11k's QCA6390 table: 20/21),
   MSI being needed for the data path (no interrupts are set up: the chip may wait for MSI enable in its
   configuration space before it moves data).
   **QMI WITH WLFW WORKS, 2026-10-29 09:05 (`pcie=10`, `logs/090327-qmi.log`).** QRTR over MHI (`renoir_mhi_send()`,
   the pump thread `renoir_mhi_pump()` into `qrtr_input()`), the QMI code on top as on redwood;
   `hakios_wlfw_renoir()` in `hakios.wlfw.c`: the service at node 7 port 1, indication register (with
   request_mem and fw_mem_ready) success in 3 ms, host capabilities (m3 supported) success in 8 ms, and
   **the firmware asks for memory: 0x4e0000 bytes of type 1 and 0x398000 of type 4** -- Android's numbers.
   (The header with ath11k's message tables: `tools/mk-ath11k.sh fetch; tools/mk-wlfw.sh` in the VM, after a
   kernel build; done.) Odd: our first two datagrams (the hellos) got no transfer event within 1 s although
   the chip acted on them; everything after came back in milliseconds.
   **FIRMWARE INIT DONE, 2026-10-29 12:50 (`pcie=10`, `logs/124931-mem2.log`):** the two pieces in one physical
   piece each (`lx_os_radio_memory()`), shown on 4 MB-aligned addresses 0xa0800000 / 0xa1000000 (unaligned ones,
   0xa05a4000 / 0xa0a84000, sent the chip into MHI SYS_ERR at once; dt cnss_wlan_region alignment 0x400000) ->
   respond memory success in 2 ms, **"memory ready"**; target capabilities: chip id 0, family 0xb, board id not
   given, soc id 0x400c0200, firmware 0x110a004f "2021-05-20 10:30", build WLAN.HSP.1.1.c2-00079-...402775.4
   (Android's exactly); **`bd_k9gl.elf` (from the phone's firmware_mnt, in the small image) accepted in 10 pieces**;
   `m3.bin` (266684 bytes) shown at 0xa1398000 and its place sent; **"firmware init done" 4.8 s after the start.**
   **ATH11K RUNS ON RENOIR AND SCANS, 2026-10-29 13:30 (`wifi_lx=2 wifi_scan=1 pcie_pause=0 wifi_mac=...`,
   `logs/132955-scan.log`).** `lx_bus.c` has a renoir bus (`renoir_ops`: BAR0 with pcic.c's moving window at
   0x310c, power_up = the kernel's chain to QRTR over MHI, `hakios_wifi_renoir_up()`, kept running); ath11k's own
   qmi.c does the whole exchange (memory on 4 MB, board.bin = bd_k9gl.elf, m3.bin, init done, wlan cfg with its
   shadow registers, mission), then HTC (Control, HTT Data, WMI), "service ready", "unified ready" 7.9 s after its
   start: one radio, 2.4 and 5 GHz, 2x2, HT/VHT/HE, firmware country "CN" (13 + 13 channels). **The scan: 26
   channels in 4 s, 5 access points heard at 2.4 GHz (-61 to -82 dBm).** What it took: QMI's transport and the
   SMMU context set before ath11k starts; SMMU tables for 128 MB (66 tables); the DMA pool 64 MB with 32 MB of
   rings (pieces of 1 MB and more on 4 MB); PERST# left released for the kept radio; the MHI pump stops on a chip
   that reads all ones. The radio has no address of its own (the firmware says zeros; Android gets it from the
   modem's DMS, "use-nv-mac"): give it with boot-arg `wifi_mac=` (the phone's, from MagicOS; not in these files).
   **JOINED, 13:40 (`wifi_join=1 wifi_hold=20`, small image with `fw/network` -- the user's, made by the user,
   `fw-wifi/network` is git-ignored; `logs/133743-join.log`):** the wanted network heard at 2.4 GHz (channel 11,
   -60 dBm) and 5 GHz (36), WPA2-PSK/CCMP; authentication and association accepted (station 4), the 4-way
   handshake done, keys to the driver: "THE NETWORK IS JOINED". The country became "AT" from the access point.
   Then the access point disassociated us (reason 33, QoS/bandwidth) within the 20 s, with nothing sent (no
   iOS, no DHCP in this run). Next: `wifi=1` in a full iOS run (hakios.apple80211.c, en0, DHCP); look at reason
   33 (block ack / QoS setup, or idle) if it comes back with traffic.
   **WI-FI IN iOS WORKS, 2026-10-29 14:15:** `wifi=1` (renoir: `renoir_whole_thread()`; small image
   `prebuilt/sysimg/root-gl-wifi.apfs` = root-gl's tree + fw/{amss20.bin, bd_k9gl.elf, m3.bin, network}, local only,
   network is the user's) with `wifi_mac=` -- the radio up 14.9 s after the kernel began on it, the FRITZ!Box
   joined at 17.0 s, iOS shows Wi-Fi, **Safari loads pages** (user). What it took after the scan: renoir's radio
   memory cached and shown to the radio cacheable (`RADIO_PTE_CACHED`, PCIe 0 is dma-coherent; no uncached second
   mapping), the firmware's 9 MB as pieces of their own (`lx_os_radio_memory_device()`), the pool 64 MB (32 MB of
   rings) on renoir only, the big mappings (pins, BAR0, RPMh, command DB) outside the kernel's 8 MB io_map room
   (`IO_MAP_SIZE`: full, iOS's start panicked in io_map for the speaker's pins, three times), and RPMh under
   `hakios_rsc_lock` (with wifi=1 there was no sound and no vibration; the ADSP's driver uses the same slots).
   Without adsp/sound, Wi-Fi + vibration work together (14:20).
   **MISSION MODE, 2026-10-29 12:55 (`pcie=11`, `logs/125435-mission.log`):** logging on (3 ms), the configuration
   from ath11k's "wcn6855 hw2.1" tables (9 pipes, 14 services: the same `qca6390` tables as the WCN6750's; no shadow
   registers, which ath11k does send for this chip) taken after 1148 ms, **mode MISSION taken in 17 ms.** No "firmware
   ready" indication after it in 3 s -- Android shows none there either (cnss calls "init done" FW_READY); it gets a
   "pin connect result" 20 ms after the mode. (`tools/mk-wlfw.sh` now emits `WLFW_HSP_*` for this chip.)
   Was next before: the configuration and mode MISSION (ath11k_qmi_firmware_start: wlan_cfg with the copy engine and
   service tables for the QCA6390 family, mode 0), Android's "Setup S3E TCS Addr: 42204 Data: 1" before M3 (the
   power amplifier's supply through the `tcs_cmd` region: dt `qcom,tcs_offset_int_pow_amp_vreg` 0x4b0), then
   ath11k_pci. Done before that: **give the firmware its memory** (two pieces in one physical piece each, shown through the SMMU,
   QMI_WLANFW_RESPOND_MEM), then FW_MEM_READY, target capabilities, BDF (`bdwlan.*` / `regdb`), M3
   (`m3.bin`), mode. Earlier plan: **QMI with WLFW over this** (`hakios.wlfw.c` speaks it for redwood over GLINK/QRTR: give it an
   MHI transport, `hakios_qmi_set_transport()`), then ath11k_pci. Originally planned as: the context arrays (channels, event rings, command ring) in shown memory, their
   addresses in MHI's registers (CCABAP 0x58, ECABAP 0x60, CRCBAP 0x68, CRDB, and the data/control limits),
   MHICTRL to M0, then the channels IPCR 20/21 for QRTR -- polling event rings at first (no MSI). Then: Linux's MHI core is GPL-2.0, so it cannot be taken; ath11k's own `pci.c`/`mhi.c`
   are BSD-3-Clause-Clear and can. Needed: BHI/BHIe image download (`amss20.bin`), the MHI state machine
   to mission mode, context arrays, event rings, channels IPCR (20/21) -- behind Linux's `mhi.h` API so
   that ath11k's `mhi.c` runs unchanged.
7. **MSI** (the tree's `qcom,pcie-msi`, SPIs); the SMMU part is done (step 4).
8. **ath11k_pci** for QCA6490 ("hsp") on `wifi/lx`, QMI over MHI's QRTR, then the station as on redwood.
