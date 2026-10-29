# renoir touch: what is known (2026-10-28), for adapting `hakios.goodix.c`

**2026-10-29: taps were lost on the keyboard -- fixed.** The FocalTech chip says "a report waits" with a short
low pulse on its interrupt line, not a level; the driver looked at the level every 8 ms, saw it high nearly every
time, and while a finger was down only its 80 ms fallback read a report (~12 a second; a quick tap fell between
two). Statistics before: 7779 reports in 10480 looks, **2701 bad** (26 %). Now (`hakios.goodix.c`, renoir only):
the pin's own falling-edge latch (`GPIO_INTR_CFG` raw status, the interrupt itself left off; `GPIO_INTR_STATUS`
read and cleared) is looked at every 4 ms, and every look reads while a finger is down. The user: "totale
Verbesserung", typing works. `touch_latch=0` brings the old behaviour back. The bad reads by hand are still open.

Status (2026-10-28, session 2): **touch works on renoir.** `hakios.goodix.c` has a renoir board
profile (`touch_board=renoir`); run with `KERNEL=devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837
RAMDISK=devices/renoir/prebuilt/vm-kernel/hold devices/renoir/run-open.sh 90 cpus=1 touchscreen=1
touch_board=renoir -v` (`hold` = `hello.c -DHOLD`: process 1 must not exit, or the kernel panics
before the touch thread gets anywhere). Log: ldoc2 1.8 V and ldoc7 3.0 V on through RPMh, `runtime ID
56 52 valid`, finger down/move lines while swiping, about 240 reports a second. Nothing uploaded or
flashed. Only the first 8 finger positions are printed; after that one summary line a minute.

Open: **the serial engine is not used.** QUP0 SE2's clocks come on (S AHB is hardware-gated:
`0x8000000a`, bit 1 -- now accepted as running), but `GENI_IF_DISABLE_RO` = 1: the FIFO interface is
off, the engine is set up for GSI DMA only. The driver falls back to SPI on the pins by hand, which
works (data in on gpio20) with ~15 % failed reads (205 of 1223 looks). Next: GSI DMA for the engine,
or fewer failed reads by hand (timing). Axes checked by tapping the corners (held upright): x grows to the right, y downwards, no
`touch_flip` needed. The extent is **8640 x 19200** (1080 x 2400 in eighths), not redwood's 10800 x 24000:
raw top left 585/854, top right 7817/674, bottom right 7784/18343, bottom left 985/18400, middle
4192/10019 (48.5 % / 52 %). Now `focaltech_max_x/y` in the board profile.

Collected before the driver change: Everything below was
read off a real renoir (MIUI 14.0.7 vendor, rooted), its device tree, and renoir's own kernel source
(LineageOS `android_kernel_xiaomi_sm8350`, branch `lineage-21`: `drivers/clk/qcom/gcc-shima.c`,
`drivers/pinctrl/qcom/pinctrl-shima.c`, `include/dt-bindings/clock/qcom,gcc-shima.h`,
`drivers/input/touchscreen/focaltech_spi/`). renoir's SoC is **"shima"** (SM7350), redwood's "yupik".

## The chip on this unit

- **FocalTech**, Android input device `fts_ts`, driver "Focaltech V3.2 20200422", resolution
  (0,0)~(1080,2400), 10 touches, firmware version register 0xA6 = **0x13**.
- Chip ID registers: **0xA3 = 0x56, 0x9F = 0x52** (redwood's FT3680: 0x56 / 0x62). The vendor
  modules name it **FT5652**; renoir's `focaltech_spi/focaltech_config.h` builds with
  `FTS_CHIP_TYPE _FT3658U`, mapping `{0x88, 0x56, 0x52, ..., 0x56, 0xB2}` (runtime ID 0x56 0x52,
  boot-ROM ID 0x56 0xB2).
- **The chip has its own flash: no firmware upload.** renoir's driver is
  `drivers/input/touchscreen/focaltech_3658u/` (`FTS_CHIP_TYPE _FT3658U`, firmware
  `include/firmware/fw_ft3658_k9.i`, 90999 bytes; the `focaltech_spi/` directory with
  `fw_ft3658_k11.i` is another Xiaomi phone's). Its upgrade routine is `fts_ft5652_upgrade()`
  (`focaltech_flash/focaltech_upgrade_ft3658u.c`): erase pages, write flash, compare ECC. So the
  k9 file only **updates the flash** when the version differs; after a reset the chip starts its own
  firmware (0x13 here) and answers with the runtime ID 0x56 0x52. redwood instead uploads its
  firmware (`hakios.focaltech_fw.h`, from `focaltech_ts_fw_m20.bin`, 116448 bytes) into PRAM after
  every reset. **On renoir the driver must never upload or flash anything**: flashing is permanent;
  if the chip answers with the boot-ROM ID 0x56 0xB2, stop and say so.
- Some renoir units have **Goodix GT9897** instead (`goodix_gt9897t_fw_k9.bin` is in
  `/vendor/firmware` on every unit). The DT node carries parameters for both.

## SPI protocol and report format: same as redwood's FocalTech path

From renoir's `focaltech_spi/focaltech_spi.c` and `focaltech_core.[ch]`:
- framing: read = address, ctrl `0x80 | 0x20` (CRC on) = 0xA0, 2 length bytes, 3 dummy bytes;
  status in rx[3] (`& 0xA0` = error); CRC-16/Kermit (0x8408, init 0xFFFF) over the data — what
  `focaltech_read()/focaltech_write()` already do.
- report: 6 bytes per touch, point count at [2] & 0x0F, X_H 3, X_L 4, Y_H 5, Y_L 6, PRE 7, AREA 8,
  event = [3] >> 6, id = [5] >> 4, and the **15-bit coordinates**
  `x = (XH & 0x0F) << 11 | XL << 3 | (PRE & 0xE0) >> 5`, `y` likewise with `(PRE & 0x1C) >> 2` —
  identical to redwood's `focaltech_report()`. Panel extent on renoir: 8640 × 19200 (measured; see above).

## What `hakios.goodix.c` hard-codes for redwood, and renoir's value

| redwood constant | redwood | renoir | source |
|---|---|---|---|
| `TLMM_PHYS` (gpio n at +0x1000·n) | 0x0f100000 | **same** (TLMM 0x0f000000, `REG_BASE 0x100000`) | pinctrl-shima.c |
| `PIN_MISO/MOSI/CLK/CS` | 52/53/54/55 | **20/21/22/23** (`qupv3_se10_spi`) | DT |
| `PIN_FUNCTION_SPI` | 1 ("qup15") | **1** ("qup10" is function 1 of gpio20-23) | pinctrl-shima.c |
| `PIN_INT` | 81 | **31** | DT `goodix,irq-gpio` / `focaltech,irq-gpio` |
| `PIN_RESET` | 105 | **30** | DT `*,reset-gpio` |
| `PIN_AVDD` (gpio switch) | 107 | **none: avdd is PM8350C LDO7, RPMh "ldoc7", 3.0 V** | DT `avdd-supply` |
| io supply | RPMh "ldoc2", 1.8 V | **same** ("ldoc2", PM8350C L2, 1.8 V) | DT `iovdd-supply` |
| `PIN_FOCALTECH_ID/PIN_GOODIX_ID` | 68 / 96 | **not used on renoir**: tell the chips apart by asking (FocalTech chip ID over SPI) | — |
| `SE_PHYS` | 0x00a94000 (QUP1 SE5) | **0x00988000** (QUP **0** SE **2**) | DT `spi@988000` |
| `GCC_VOTE` | gcc+0x52008 | **same** | gcc-shima.c |
| vote bits core2x/core/m_ahb/s_ahb/se | 18/19/20/21/27 | **9/8/6/7/12** | gcc-shima.c |
| `GCC_QUP1_M_AHB_CBCR`/`S_AHB_CBCR` | +0x18004/+0x18008 | **+0x17004/+0x17008** | gcc-shima.c |
| `GCC_QUP1_SE5_CBCR` | +0x185fc | **+0x1726c** | gcc-shima.c |
| `GCC_QUP1_SE5_CMD` (cmd_rcgr; CFG +4, DFSR +0x14, PERF_CFG +0x1c, PERF_M +0x5c, PERF_N +0x9c) | +0x18600 | **+0x17270** | gcc-shima.c |
| `CMD_DB_PHYS`, `RSC_PHYS` | 0x80860000, 0x18220000 | **same** | soc.args |
| SPI clock in Android's DT | — | `spi-max-frequency` of the touch node 6 MHz | DT |

## Plan for the driver change

1. Put the board constants into a struct (redwood, renoir) chosen by a boot-arg
   (`touch_board=renoir`), redwood the default, so redwood is untouched.
2. renoir power-on: ldoc2 1.8 V and **ldoc7 3.0 V** through the RPMh helper already there (instead
   of the avdd gpio), reset gpio 30, int gpio 31.
3. Vendor: ask FocalTech's chip ID over SPI first; accept 0x56/0x52 (runtime, after waiting up to
   1 s past reset). If the chip answers with the boot ID 0x56/0xB2, stop with a clear message — never
   upload or flash. Goodix path for GT9897 units afterwards.
4. Build the kernel (`xnu-deps/setup.sh`, `xnu-build.sh`; see `WORKFLOW.md`) and test with
   `run-open.sh 30 cpus=1 touchscreen=1 touch_board=renoir -v`.
