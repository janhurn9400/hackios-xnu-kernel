# renoir GPU: what is known (2026-10-28), for `hakios.adreno.c`

Status (13:31): **the GPU works on renoir at 490 MHz** (`logs/132829-renoir-open.log`: the 3D core
measures 489998756 Hz, CoreAnimation draws through Mesa's FD660, SpringBoard never exited in 180 s).
Run with `board=renoir gpu=1` and `RAMDISK=devices/renoir/prebuilt/sysimg/root-gl.apfs` (GL=1 build).
Still some stutter: frames are drawn off-screen (OSMesa) and copied into the UEFI framebuffer by the CPU.

- Run 1: step 5 failed: the frequency counter read 0 Hz for the crystal. The GCC debug mux's input from
  the GPU clock controller is 0x13c on shima (debugcc-shima.c), 0x146 on yupik; now `gcc_debug_gpucc`.
- Run 2 (`logs/132*-renoir-open.log`): clocks measure right (ahb 150 MHz), the SMMU, the zap shader,
  the GMU firmware (versions core 0x30010008, power 0x30000009, HFI 0x20010000) starts, its message
  queues come up, it accepts the speed table, the bus table, "start" and the speed (540 MHz). Asked to
  power the 3D core (OOB GPU, host2gmu bit 30) it turns on the GX power switch (gx_gdscr 0xf8222000)
  and locks PLL0 (mode 0x80001805, L 0x38), but never sets the grant (gmu2host_intr_info stays 0,
  fw_init_result 0x100, rpmh_power_state 0) and answers nothing after ("slumber" unanswered).
  No SMMU fault.
- Run 3 (`logs/132615-renoir-open.log`, `adreno_speed=1`): at 285 MHz **the GPU starts and stays on**;
  CoreAnimation gets "OpenGL ES 3.2 Mesa 25.0.7; FD660". The cause: this phone's speed bin (fuses) is
  105, whose table (dt `qcom,gpu-pwrlevels-1`) ends at 490 MHz; 540 MHz is not allowed for it.

Sources: renoir's device tree (`prebuilt/device-tree.dtb`, decompiled in the VM with `dtc`), and
renoir's own kernel (LineageOS `android_kernel_xiaomi_sm8350`, branch `lineage-21`):
`drivers/clk/qcom/gpucc-shima.c`, `gcc-shima.c`, `drivers/gpu/msm/adreno-gpulist.h`,
`adreno_a6xx_gmu.c`. Mesa 25.0.7 `src/freedreno/common/freedreno_devices.py`.

## Same as redwood

- Firmware file names: `a660_gmu.bin`, `a660_sqe.fw`, `a660_zap.{mdt,b00..b02}` (pulled into
  `fw-gpu/` by `pull-firmware.sh`; MagicOS needs Magisk's `su` to read `/vendor/firmware`).
- Every register block at the same address: 3D core `0x3d00000`, GMU `0x3d6a000`, GMU PDC
  `0xb290000`, RSCC `0x3de0000`, GPU SMMU `0x3da0000`, GPU clock controller `0x3d90000`, the GDSCs
  `0x3d9106c` (CX) and `0x3d9100c` (GX).
- Every clock offset the driver touches: `gpucc-shima.c` = `gpucc-sc7280.c` (PLL0 0x0, PLL1 0x100,
  CXO_AON 0x1004, GX_GMU 0x1064, AHB 0x1078, CX_GMU 0x1098, CXO 0x109c, GMU RCG 0x1120, HUB_AON 0x1178,
  HUB RCG 0x117c, HUB_CX_INT 0x1204, SMMU vote 0x5000); in the GCC 0x71004, 0x7100c, 0x71018, 0x71154,
  PLL votes 0x52000 bits 15/16. The PLLs are only read: the GMU sets the 3D core's speed.
- kgsl's tables for both (`adreno_gpu_core_a642` renoir, `adreno_gpu_core_a642l` redwood): the same
  hwcg (`a660_hwcg_regs`), GBIF (`a650_gbif_regs`), protected registers (`a660_protected_regs`), GMU
  2.0, IFPC, APRIV, `pdc_in_aop`.

## Different (in `struct gpu_board`)

| | redwood (A642L) | renoir (A642) | source |
|---|---|---|---|
| chip ID | 0x06030500 | 0x06060010 | dt `qcom,chipid`, "Adreno642v1" |
| chip ID for the GMU firmware | 0x06035000 | **0x16060000** | kgsl `a6xx_gmu_fw_start`: A642 puts the patch's high nibble in bits 31:28 |
| speed / gaming bin fuse | 0x1e9 / 0x1f5 | 0x1e1 / 0x1ed | dt `gpu_speed_bin@1e1`, `gpu_gaming_bin@1ed` |
| speeds | 315/450/550 MHz | 285/443/490 MHz (levels 64/128/144); bin 79: 365 at most | dt `qcom,gpu-pwrlevel-bins`: bins 0, 158 to 747; **105** (this phone) to 490; 79 to 365 |
| GMEM | 512 KB | 1.5 MB | kgsl `gmem_size` |
| prim_fifo_threshold | 0x200000 | 0x300000 | kgsl; written as `PC_DBG_ECO_CNTL` |
| highest bank bit (UBWC) | 14 | 15 | dt `qcom,highest-bank-bit` |

## Mesa

Mesa 25.0.7 has no A642 entry; `GPUId(660)` is chip ID 0x060600ff, "any patch", so 0x06060010
runs as an **FD660** (3 CCUs, `a6xx_gen4`). If the A642 is a cut-down A660 with fewer units, the
picture may come out wrong: the first thing to look at when it draws.

## Test

`gpu=1` needs the firmware in the small image (`fw-gpu/` in the VM's checkout, then
`GL=1 REST=1 userland/build.sh`: Mesa via `tools/mk-mesa.sh`, needs meson, ninja, llvm-ar,
python3-venv). Then the run of `NEXT.md` step 3 with `board=renoir gpu=1`.

## The display (frame pacing and tearing, 2026-10-28 afternoon)

- Frame benchmark (`RC_JOBS=rc.jobs.fps`, `touch_lat=1`, `tools/frames.py`, `logs/133544-renoir-open.log`):
  40 % of frames over 16.7 ms, 6 % over 33 ms (redwood upstream: 25-30 % and 6 %). The copy into the
  panel's framebuffer takes 7-13 ms a frame here (redwood: about 4).
- `display_probe=1` with renoir's addresses (in `struct gpu_board`): the DSI interface is intf1 at
  `0xae6b800` (0xae00000 + `qcom,sde-intf-off` 0x6b800), the display clock controller as in
  `dispcc-shima.c` (power switch `0x3000`, ahb `0x2080`, mdp `0x200c`, pclk0 `0x2004`). The firmware leaves
  the panel in **command mode at 60 Hz**: the timing engine is off, the line counter runs 1 to ~2400 in
  8.6-8.9 ms per frame and rests at 1 in between. Its tear check (DPU 7: in the interface, `0x280..0x2b8`,
  `sde_hw_intf.c`) is on, synced to the panel's TE pin (gpio 82, function 1, toggling): `check_en 1`,
  `sync_config_vsync 0x180085`, `autorefresh_config 0x80000001`, start_pos/vsync_init 2400.
- The tearing that is seen is **not** in the framebuffer: frames recorded from it (`rec_fb=1 rec_ring=1
  rec_step=6`, automated swipes) are whole; the framebuffer is mapped write-combined, not cached. Copying
  right after the panel's burst (an experiment, `fb_vsync`) did not end it and hung the phone twice; it was
  taken out again. So it happens between the display controller and the panel. Open: the panel nodes in
  this device tree (Visionox R66451, 2340 lines, 120/90/60 Hz) are not renoir's panel (the firmware set
  2400 lines); the right one is named on MagicOS's kernel command line (`msm_drm.dsi_display0=`).
