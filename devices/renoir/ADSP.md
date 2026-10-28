# renoir audio DSP (ADSP): where it stands (2026-10-28)

The ADSP brings the battery manager (state of charge, charging) and the audio services. Driver:
`xnu/files/bsd/dev/hakios.adsp.c`, upstream's for redwood, run step by step with `adsp_step=N`.

## Same as redwood

From renoir's device tree (`qcom,lpass@3700000`, `qcom,smp2p-adsp`, `qcom,glink/adsp`,
`qcom,pmic_glink`): TrustZone image (pas-id) 1, SMP2P items 443 (the DSP's) / 429 (ours), IPCC client 3
(signal 0 GLINK, 2 SMP2P), rails `lcx.lvl` / `lmx.lvl` voted at 384 while it boots
(`qcom,vdd_cx-uV-uA = <0x180 0>`), the SMEM lock at 0x1f43000 (syscon@1f40000, lock 3), SMEM at
0x80900000, AOSS mailbox at 0xc300000, apps RSC DRV 2 with TCS offset 0xd00, the battery manager on
`PMIC_RTR_ADSP_APPS`.

## Different

- **Reserved memory 48 MB** (`adsp@86700000`, `reg <... 0x3000000>`), not 64: `board=renoir` sets it.
  renoir's own `adsp.mdt` fills exactly 0x3000000 bytes.
- The device tree also asks for a proxy clock `xo` (RPMh `bi_tcxo`, ARC `xo.lvl`): `adsp_xo=1` votes it
  (did not help, see below).

## On the phone

Firmware from `fw-audio/` (pulled with Magisk's `su`), in a small image built with `ADSP=1`
(`prebuilt/sysimg/root-adsp.apfs`, 24.5 MB: no 2.4 GB system image upload needed for these tests):

```
KERNEL=devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837 \
RAMDISK=devices/renoir/prebuilt/sysimg/root-adsp.apfs \
devices/renoir/run-open.sh 90 bootcpu=7 cpus=1 board=renoir adsp_step=N adsp_pause=4000 -v
```

`bootcpu=7` matters: with the kernel on core 0 the loader cannot answer during the run (no live log).

- Steps 1-6 work: SMEM partition found, the DSP has not run since power-on; the image is whole (36
  segments, 21900753 bytes); the AOSS mailbox opens; **TrustZone accepts the signature** (PAS init 0,
  mem setup 0x3000000 at 0x86700000); the image is written and read back (0 differences); our SMP2P item
  429 is made. `adsp_step=6` returns cleanly.
- **Step 7 resets the phone**, within a second when there are no pauses. The last lines seen:
  `about to ask RPMh, through TCS 0, for lcx.lvl (0x30030) = 6 and lmx.lvl (0x30040) = 6 (level 384)`.
  The same with `adsp_level=64`, with `adsp_rails=0` (no RPMh request), with `adsp_ipcc=0` (doorbells not
  enabled), with long pauses or none, with `adsp_xo=1`. The live log (every 2-3 s) is too coarse to
  show the very last line. Most likely the DSP is let out of reset and dies at once, and an unhandled
  subsystem crash resets the SoC.
- Under MagicOS the same DSP starts fine (the sound card is up at 9.2 s); the boot lines of the start
  itself had already scrolled out of the kernel's buffer.

## Compared with renoir's own start code (LineageOS subsys-pil-tz.c, peripheral-loader.c)

Linux's order: AOP "on", proxy votes (rails, clock `xo`), PAS init image, mem setup, (hypervisor
assignment only with `qcom,vmid`: renoir's ADSP has none), segments, then `scm_pas_enable_bw()` -- a vote
for the crypto engine's way to DDR (dt `pil_scm_pas`, MASTER_CRYPTO to SLAVE_EBI1; on shima the BCMs
CE0, SN8, SN0, SH0, MC0, ACV) -- regulators and clocks again, and `auth_and_reset`. `adsp_bw=1` makes
that vote before the start: **it did not help either** (run 17:36, the same reset).

Ruled out so far: the rails' level (384, 64), no rails at all, the IPCC doorbells, pauses, the `xo`
proxy clock, the crypto engine's bandwidth.

## Next

- See the crash: the kernel's log lies at a fixed address in DDR (`log at f7b4....`) and probably
  survives the watchdog's reset; the loader (`xnuloader/loader.c`) could save the previous run's log
  when it starts again. That would show the last line of every crash, not only this one.

- Speaker amplifiers, for the sound later: two NXP TFA98xx at 0x34/0x35 (MagicOS dmesg).
