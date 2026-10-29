# renoir sound: what is known (2026-10-28, session 3) and what the port needs

Status (2026-10-28, 20:52): **iOS's sound plays** (`sound=4`: the charging chime and the keyboard clicks,
heard; haptics felt alongside, no hiss), with the amplifiers kept on once started (renoir's default
`sound_keep_on=1`). Ringtone previews in Settings stay silent and the ringtone cannot be changed: an iOS
side matter (the tone library), not the driver. The ADSP runs with its audio services up (`ADSP.md`).

## redwood (what `hakios.sound.c` / `hakios.speaker.c` do)

Two **Awinic AW88261** at I2C 0x34/0x35 (serial engine 3, GPIO 12/13), set up by register writes
from Android's `aw882xx_acf.bin`, fed by the first TDM interface (PRI_TDM_RX_0, AFE port 0x9000,
group 0x9100; pins 97-100, function mi2s0), 2 slots of 32 bits at 48 kHz. `sound=1..4`.

## renoir (from its device tree, `/sys/firmware/fdt` of MagicOS)

- **Two NXP TFA98xx** ("smart amps" with their own CoolFlux DSP), compatible `tfa,tfa98xx`, on
  **`i2c@a84000`** (QUP1, `qcom,i2c-geni`):
  - `tfa98xx_1@34`: `reset-gpio` 15, `irq-gpio` 47, `spk-sw-gpio` 24 (a speaker switch)
  - `tfa98xx_2@35`: `irq-gpio` 84, no reset pin
- Sound card `lahaina-shimaidp-snd-card` (`qcom,lahaina-asoc-snd`), `qcom,mi2s-audio-intf = 1`,
  `qcom,msm-mi2s-master` all 1, `qcom,tdm-max-slots = 8`. The TFA codec is not in `asoc-codec-names`
  (`msm-stub-codec.1 bolero_codec wcd938x_codec swr-haptics wsa-codec1 wsa-codec2 ...`): Xiaomi's
  machine driver adds it in a DAI link of its own -- which MI2S (and so which AFE port and pins) is
  still to read off renoir.
- MagicOS dmesg (session 2) has the two amplifiers at 0x34/0x35.

## Read off renoir in MagicOS (session 3, 18:55; files in `fw-audio-renoir/`, not committed)

- **Amplifiers: NXP TFA9873** (container `9873N1B`), module `tfa98xx_dlkm.ko`, I2C bus 1 = `a84000`.
  Firmware `/vendor/firmware/tfa98xx.cnt`, **3828 bytes** (sha256 `5be2bd90...e071d09e`), container
  format `PM3_01`, customer "Mi", 2 devices, 7 profiles: `stereo`, `handset`, `left`, `right`, each
  with a `.standby` variant, plus `calibrate`; the patch is `TFA9873N1B0_dummy.patch` (no DSP
  patch: register settings and a few `SetAlgoParams` / `SetCoeffs` messages), profiles at 96 kHz.
  dmesg: `tfa98xx 1-0034` and `1-0035`, `container_loaded ... 9873N1B, 2 ndev, 7 nprof`.
  The HAL's mixer control "TFA Profile" (default `Powerdown`).
- **Interface: TERT_MI2S_RX** (`/vendor/etc/audio/sku_shima/mixer_paths_shimaidp.xml`: every playback
  path routes MultiMedia* to it; `mixer_paths_overlay_static.xml`: `TERT_MI2S_RX Channels Two`,
  `Format S24_LE`, `SampleRate KHZ_96`). DT `qcom,msm-dai-q6-mi2s-tert`: dev-id 2, rx-lines 0x2 (data1),
  tx-lines 0x1 (data0, the amplifiers' feedback); `qcom,msm-mi2s-master` 1 (the SoC drives the clocks).
  Qualcomm's numbers (q6afe): AFE port TERTIARY_MI2S_RX `0x1004` (TX `0x1005`), bit clock
  `LPASS_CLK_ID_TER_MI2S_IBIT 0x104`; 96 kHz x 2 x 32-bit slots = 6.144 MHz.
- **Pins** (pinctrl-shima.c function numbers): SCK gpio120 `mi2s2_sck` (1), WS gpio122 `mi2s2_ws` (1),
  data0 gpio121 `mi2s2_data0` (1, in), **data1 gpio124 `mi2s2_data1` (function 3**, out to the amps); all
  8 mA, no pull. The I2C bus: QUP wrapper 1 SE1 (`GCC_QUPV3_WRAP1_S1_CLK`), gpio36/37 function `qup1`.
  TFA pins: reset gpio15, irq gpio47 (0x34) / gpio84 (0x35), speaker switch gpio24.
- The generic `mixer_paths.xml` / `mixer_paths_qrd.xml` (WSA codec) are Qualcomm's templates, not used.

## Sources

- Xiaomi's kernel source has no renoir branch (MiCode/Xiaomi_Kernel_OpenSource: `lisa-r-oss`,
  `venus-r-oss`, `star-r-oss`, `haydn-r-oss`, `redwood-s-oss` -- `venus`/`star` are SM8350 phones
  whose audio techpack may carry the same TFA driver). LineageOS `android_kernel_xiaomi_sm8350` only
  has `sound/soc/codecs/tfa9879.c` (a different, simple chip).
- NXP's TFA98xx Linux driver (tfa98xx: container `.cnt` loaded over I2C, then the profile/vstep).

## First probe (session 4, 2026-10-28 19:51): both amplifiers answer

`board=renoir speaker_probe=2` (`hakios.speaker.c`: I2C by hand on gpio36/37, read only): **0x35 and
0x34 each answer with revision 0x0b73, a TFA9873**; 0x35: system control 0 `0x4201`, control 1 `0x8000`,
status 0 `0x0017`, status 1 `0`, status 3 `0x010f` (powered down, as the firmware leaves them). The bus
is at rest (both lines high). The TERT_MI2S pins 120/121/122/124 are GPIOs (function 0, inputs) until
the sound path sets them to their MI2S functions; reset gpio15 reads 0 (not in reset), speaker switch
gpio24 an input reading 0.

## The container decoded (`devices/renoir/tools/tfa-container.py fw-audio-renoir/tfa98xx.cnt`)

Self-describing: descriptors are a 24-bit offset with the type in the top byte; bitfield items are a
16-bit value and a 16-bit field id `register << 8 | position << 4 | (length - 1)`, so no NXP tables are
needed. **0x34 is "RCV"** (the earpiece speaker, top), **0x35 is "SPK"** (the main speaker, bottom):
renoir's stereo is earpiece + speaker. Per device: 30 bitfields (registers 0x02, 0x04, 0x20-0x27,
0x54, 0x5f, 0x68, 0x6f-0x76, 0x85, 0xf9) and a small register patch (`PA1_00`, no DSP code); the two
differ only in 0x26/0x27 (which slot/channel each takes). Profile `stereo`: 0x02[3:0] = 9 (96 kHz),
0x00[4] = 1, 0x64[15:14] = 0, 0x62[15:14] = 0; `*.standby` / `powerdown.standby`: 0x00[0] = 1
(power down); `calibrate.cal` with four files (MG3_01 messages). Still needed: NXP's TFA2 start order
(power-up, clocks/PLL wait, patch, profile, unmute) from its Linux driver.

## The start sequence: Goodix's (formerly NXP's) driver

Source: **github.com/Goodix-vas/tfa98xx** (current, 2026-09; `src/tfa_init.c`, `src/tfa_dsp.c`,
`src/tfa_container.c`, `inc/tfa9873_tfafieldnames_B0.h` for the field names). For our revision
**0x0b73**, `tfa9873_specific()` (tfa_init.c ~751) is the chip's init: unlock (0x0f = 0x5a6b, then
0xa0 = reg 0xfb ^ 0x005a, key 2), then `0x02=0x0628 0x61=0x0183 0x63=0x005a 0x6f=0x0082 0x70=0xa3eb
0x71=0x107e 0x73=0x0187 0x83=0x071c 0x85=0x0380 0xd5=0x004d`, the error interrupts, FSSYNCEN = 0.
`tfa_dev_start()` (tfa_dsp.c ~3435): `tfaRunSpeakerBoost` (cold start: power up, wait for the clocks
-- needs the MI2S bit clock running --, the init above, the container's device bitfields and patch,
the profile), then oscillator on, `tfa_dev_set_state(OPERATING | MUTE)`, the profile and filters,
unmute. `tfa_dev_stop()`: mute, powerdown. Still to read in detail: `tfaRunSpeakerBoost`,
`tfaRunStartup`, `tfa_dev_set_state` for TFA2 (which fields: PWDN, MANSCONF, MANCOLD, AMPE, ...).

## What the port needs, in order

1. From renoir in MagicOS (adb; `su` if Magisk works): `/vendor/etc/mixer_paths*.xml` (the speaker
   path: which `*_MI2S_RX` / AFE port), the TFA container (`/vendor/firmware/*.cnt`, e.g.
   `tfa98xx.cnt`), `lsmod` / `/vendor/lib/modules/*tfa*` (the module's name), dmesg lines of the
   TFA driver (chip revision, e.g. TFA9874 0x0a74 / TFA9873 0x0a73).
2. A probe first (no sound): read the TFA's revision register over I2C on `i2c@a84000` from our
   kernel, like `speaker_probe=2` does for redwood.
3. `hakios.tfa98xx.c`: reset (gpio 15), power-up, load the container's patch/config/vstep for the
   speaker profile, unmute -- after NXP's driver; the guard (`hakios.guard.c`) stays in front.
4. `hakios.sound.c`: a renoir board profile (`board=renoir`): the MI2S port instead of TDM 0, its
   pins and clock; the rest (SMMU stream, ADM/ASM, `sound=1..4`) as for redwood.
5. Mind USB: once the DSP runs, USB goes away (`ADSP.md`), so test with the logs kept by the loader.

## The first tone (session 4, 2026-10-28 20:17)

Code: `hakios.speaker.c` (renoir section, "renoir's amplifiers, NXP TFA9873") and `hakios.sound.c`
(`RENOIR_PORT` and friends). With `board=renoir`:

- **Amplifiers** (no file needed: the container's settings are in the code): `prepare` = I2C reset,
  keys unlocked, revision 0x0b73's settings, FSSYNCEN 0, AUDFS/FRACTDEL restored, the container's 30
  device bitfields (RCV/SPK slots) and profile `stereo`, keys locked -- powered down; `start` =
  MANSCONF 1, PWDN 0, wait for CLKS (0x10 bit 6), then AMPE 1; `stop` = AMPE 0, PWDN 1. Every bitfield
  is read back; a mismatch or a fault flag powers both down. The TFA9873 has no volume of its own:
  `sound_below` takes the samples down in 6 dB steps (default 240 -> -50 dBFS; 144 -> -38 dBFS).
- **Port**: AFE 0x1004, `AFE_PARAM_ID_I2S_CONFIG` (24 bits, data line 1, stereo, word select ours,
  96 kHz), clock 0x104 at 6.144 MHz attribute 1; pins 120/122/121 function 1, 124 function 3, 8 mA.
  ADM path and ASM stream at 96 kHz (the 1 kHz table, each sample twice).
- **SMMU**: renoir's audio stream is **0x2001** (dt `qcom,msm-audio-ion`), not redwood's 0x1801; with
  0x1801 the match register would not take the write (the hypervisor refuses foreign streams).

Runs (iOS from `system-fw.apfs` + `root-gl.apfs`, `bootcpu=7 cpus=8 ... board=renoir adsp=1`):
- `sound=1`: LEVEL DONE -- both TFA9873s report clocks stable on the bit clock, no fault (amplifiers off).
- `sound=3 sound_below=144`: **heard** (the user, at the earpiece); LEVEL DONE, no SMMU fault, no
  amplifier fault. The log lines survive only in the loader's per-second notes (`oem log` after the
  next start; USB is gone during a DSP run), so parts are missing.

## iOS's sound (sound=4), session 4, 20:24-20:52

Run: `fastboot oem ramdisk; fastboot stage prebuilt/sysimg/system-fw.apfs`, then kernel + `root-gl.apfs`,
`loader_return_after=300 sb_delay=8 bootcpu=7 cpus=8 cpufreq=1 touchscreen=1 board=renoir gpu=1 haptic=5 adsp=1
sound=4 sound_below=144`. USB goes when the DSP starts; leave by keys (power + volume down: fastboot), then boot
`xnu-usb-renoir-crashlog.img` and `fastboot oem log` / `get_staged`: the loader's per-second notes.

- iOS writes 48 kHz; the feeder takes 480 of its frames per 10 ms period, runs the guard at 48 kHz, takes
  `sound_below` off in 6 dB steps (`tone_shift`), then doubles to 96 kHz (`upsample_96k()`, the first of each
  pair halfway from the frame before).
- **A status line** every 0.5 s (`sound: st on .. starts .. gave-up .. ios <frames> fed <frames> peak ..
  trouble: ..`): the only thing that reliably survives in the notes among iOS's own lines.
- What went wrong on the way, in order:
  1. A fault read during play powered both down (`gave-up 1`); `last -` showed no status had been read as a
     fault: **I2C transfers get lost now and then** with iOS running (6 retries in one run). Fix: every
     transfer is tried three times (`register_read/write`), a status not read counts as a fault only 5 looks
     in a row, a fault flag only if it is still there on a second look 2 ms later (the alarms are sticky).
  2. Then no more fault, but after the chime **iOS wrote nothing more** (`ios` frozen at 150528 frames) and its
     haptics stopped too, when the amplifiers were stopped after 3 s of silence and started again. With
     `haptic=5 adsp=1` and no sound, haptics work; with `sound_keep_on=1` the keyboard clicks play and haptics
     work. Why the stop/start silences iOS is not understood (the kernel's write path never waits) --
     **renoir keeps the amplifiers on** (`sound_keep_on=0` brings the stop back, for a look).
- Upstream runs `sound=4` with `haptic=5` on redwood (`docs/ru/notes.md`): this was renoir-specific.

Next: why stopping the TFA9873s silences iOS (then stop them on silence again, for the battery), a level for the TFA9873s
(TDMSPKG 0x5f[9:6] / AMPGAIN 0x52 are candidates), and the amplifiers' temperature/battery monitor.

## Sound and haptics stop once Safari starts (2026-10-29, with Wi-Fi)

Seen with the audio plug-in's new trace (properties set, starts/stops per device, I/O counts every 5 s;
`logs/trace-live*.log`, run without the DSP so that USB stays up): before Safari every haptic is a start of the
Actuator (client 71) and the Speaker (client 95), the waveform, and a stop of both -- all clean. Once Safari starts,
**iOS starts no device of ours any more**: no haptics, no system sounds; nothing fails below (plug-in, kernel,
drivers get no request at all). The first time WebKit's GPU process played 5 s on the Speaker before that; the
second time it played nothing and haptics stopped all the same. Closing Safari brings them back only sometimes, after
a while. No audio daemon exits or is killed. Not the clocks either: one clock for all devices in one clock domain
(audio_plugin.c, also a fix in its own right) changed nothing. Upstream has nothing on it (no commit, no review).
It is a decision inside mediaserverd (a session WebKit makes muting system haptics and sounds?); next: mediaserverd's
own log (over SSH now that Wi-Fi works, `log stream`), or WebKit's audio session category.

## Sound and haptics stop once Safari starts: it was memory (2026-10-29, evening)

`mediaserverd`'s log (`oslog` from Procursus, over SSH): `HALB_IOBufferManager_Server::AllocateBuffer: failed to
lock the buffer ... errno 35`, `IOWorkLoop: failed to start the hardware`, `DoStartIO failed (1852797029 = 'nope')`:
the kernel refused to wire the 64 KB I/O buffer of the Actuator/Speaker device. `vm_stat`: 3.66 GB wired, 20 MB
free of ~4.3 GB (the 2.5 GB system image in RAM). Not our plug-in, not the drivers. **Fixed by booting from the
flash** (`FLASH.md`): 1.6 GB wired, haptics stay with Safari (user, 21:38). Sound not tried there yet (that run had
no `adsp=1`).
