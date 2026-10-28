# renoir sound: what is known (2026-10-28, session 3) and what the port needs

Status: **not started in code.** The ADSP runs with its audio services up (`ADSP.md`, APR "THE AUDIO
SERVICES ARE UP"), so the DSP side is there. What differs from redwood is the amplifiers and the
interface that feeds them.

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

## Sources

- Xiaomi's kernel source has no renoir branch (MiCode/Xiaomi_Kernel_OpenSource: `lisa-r-oss`,
  `venus-r-oss`, `star-r-oss`, `haydn-r-oss`, `redwood-s-oss` -- `venus`/`star` are SM8350 phones
  whose audio techpack may carry the same TFA driver). LineageOS `android_kernel_xiaomi_sm8350` only
  has `sound/soc/codecs/tfa9879.c` (a different, simple chip).
- NXP's TFA98xx Linux driver (tfa98xx: container `.cnt` loaded over I2C, then the profile/vstep).

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
