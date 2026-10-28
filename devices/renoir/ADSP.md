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

## The crash seen (session 3, 2026-10-28 ~18:00): THE DSP STARTS; the reset comes after it

The loader now keeps the kernel's live log across a reset (`xnuloader/loader.c`, crash log at
0xF3F80000; **DDR and the whole 64 MB payload region survive the reset and Xiaomi's XBL/ABL**). The
`adsp_step=7 adsp_pause=0` run's own log, recovered on the next start:

```
adsp: 7: TRUSTZONE HAS AUTHENTICATED THE IMAGE AND LET THE AUDIO DSP OUT OF RESET; watching for 10 s
adsp: 7: +11.206 ms: the DSP's item APPEARS: SMP2P version 1 ...
adsp: 7: THE AUDIO DSP SAYS IT IS UP: "slave-kernel" says 0x6, 37.913 ms after TrustZone started it
adsp: 7: RPMh HAS DONE IT after 74 us ...            (our rails given back: the DSP has its own)
adsp: 7: the watch is over after 3038 ms; its doorbells heard: GLINK 1, SMP2P 3
adsp: 7: THE AUDIO DSP ACKNOWLEDGES THE STOP after 22.615 ms; its entry says 0x10e
adsp: 7: TrustZone returns 0, answer 0 / THE AUDIO DSP IS SHUT DOWN
adsp: 7: the always-on processor has taken it        ({image, load_state, adsp, off})
adsp: the system goes on starting                    <- the last line; the reset follows
```

So the DSP's start works (as on MagicOS): it is up after 38 ms, runs 3 s, acknowledges the stop and
TrustZone shuts it down. The "last line" session 2 saw (`about to ask RPMh ... level 384`) only was the
last one the 2-3 s live log had caught. **The reset comes after the DSP was shut down**, ~5 s after
`oem run`. This log is the loader-log channel only (`loader_log_line`); the kernel's own messages
after "goes on starting" (root mount, the small image's missing init, the panic, the return) are not in
it. `adsp_step=6` returns cleanly, so it is something step 7 leaves behind.

## Next

- What resets after the shutdown: candidates are (a) the shut-down DSP / the AOP "load_state off" and
  whatever still expects the DSP (its SMMU, the GLINK edge we opened, IPCC); (b) the loader on core 0
  (`charger_poll()` every 5 s calls Qualcomm's UEFI charger DXE, which on Qualcomm talks to the battery
  manager on the ADSP over GLINK: the reset at ~5 s fits); (c) the kernel's panic / return path.
  Cheap tests: the same run with the loader's charger polling off; with the DSP left running (no
  stop/shutdown at the end of the steps); the kernel's console mirrored into the live log.
- (b) is unlikely: the loader's charger stack is not active on renoir (no "battery: ChargerEx=" line
  in any run, the kernel reports SOC -1).
- **(c) is ruled out**: `adsp_step=7 adsp_pause=0 adsp_return=1` (back to the loader right after the
  steps, no root mount, no panic) **resets the phone as well** (session 3, 18:05; it came up in MagicOS,
  which overwrites the kept log). Left: the stop / TrustZone shutdown / AOP "off" sequence, or the way
  back to the loader with a DSP that has run.
- Watch out: when the phone comes up in MagicOS after a crash, the kept log is gone (Linux uses that
  RAM). Go to fastboot by keys while it is still resetting.
- **The kernel comes back; the reset comes after, in the loader / UEFI** (session 3, 18:04, the loader's
  own log kept across the reset): `adsp_step=7 adsp_pause=0 adsp_return=1` ->
  `kernel returned: log at f7b489c0, 29604 bytes`, `returned 29604 0`,
  `log: 32768 bytes written to logdump (0), read back the same (0)` -- the last line; the reset follows
  within about a second, while the loader is back in its USB loop and UEFI runs again. Not with
  `adsp_step=6` (the DSP never started). So something a DSP run leaves behind meets UEFI: an
  IPCC/LPASS interrupt the kernel left enabled in the GIC, a watchdog nobody pets after the run, or
  a UEFI driver touching what the DSP's shutdown changed. The run's kernel log is in `logdump` (sda30).
- **Not an interrupt, not the stop, not the shutdown** (session 3, 18:13-18:21): the loader reports no
  shared interrupt pending after the run (`gic the run is back: ... no shared interrupt pending`; the
  same as after `adsp_step=6`), and the reset comes before its next line. New boot-args in
  `hakios.adsp.c`: `adsp_no_shutdown=1` (stop request, but no TrustZone shutdown, no AOP "off") and
  `adsp_never_stop=1` (the DSP is left running everywhere, the way back included): **both reset, ~5 s
  after `oem run`**. So a running DSP takes the phone down by itself a few seconds after it is up
  (~3-4 s), whatever we do; the reset just happens to fall right after the 3 s watch.
- Likely: it waits for the apps side to open its links -- the GLINK edge and the services Linux opens
  right after "up" (sys_mon / ssctl over QRTR) -- and gives up. Steps 8-11 (`step_queues`, `step_link`,
  `step_qrtr`, `step_apr`) do that and were never tried (step 7 seemed to be the crash). Next:
  `adsp_step=11 adsp_pause=0 adsp_keep=1` (or `adsp=1`).
- Note: after a reset the phone restarts towards MagicOS; going to fastboot by keys keeps the RAM only
  if it is done before Android starts.

- Speaker amplifiers, for the sound later: two NXP TFA98xx at 0x34/0x35 (MagicOS dmesg).
