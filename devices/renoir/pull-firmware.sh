#!/bin/bash
# Pull renoir's own firmware out of its MIUI into the fw-*/ directories at the top of the HakiOS
# repository (never committed: it is the vendor's). renoir is on adb, booted into MIUI.
#
# Unlike the redwood (upstream) script, this GLOBS for renoir's actual firmware names instead of
# hardcoding redwood's (a660_*, wpss.*, etc.), because renoir's exact file names must be read off
# the phone, not guessed. After the first run, pin the names you actually got so builds are
# reproducible (compare against redwood's list in the comments below).
set -e
cd "$(dirname "$0")/../.."

adb get-state >/dev/null

pullglob() { # DIR  'adb shell ls' glob...   (missing matches are told, not fatal)
	local dir=$1; shift
	mkdir -p "$dir"
	local files
	files=$(adb shell "ls $* 2>/dev/null" | tr -d '\r')
	[ -n "$files" ] || { echo "pull-firmware.sh: $dir: nothing matched: $*"; return 0; }
	for f in $files; do adb pull "$f" "$dir/" >/dev/null 2>&1 || echo "pull-firmware.sh: $f: pull failed"; done
	echo "pull-firmware.sh: $dir: $(ls -1 "$dir" | wc -l | tr -d ' ') files"
}

# GPU — renoir is Adreno 642. redwood used a660_{gmu.bin,sqe.fw,zap.*}; renoir's a6xx name may
# differ (a650/a660-class). Glob both the gmu/sqe and the zap (signed) blobs.
pullglob fw-gpu '/vendor/firmware/a6*_gmu.bin' '/vendor/firmware/a6*_sqe.fw' '/vendor/firmware/a6*_zap.*'

# Wi-Fi — redwood: wpss.{mdt,b02..b07}. renoir likely WCN6750-class; glob the wpss image set.
pullglob fw-wifi '/vendor/firmware_mnt/image/wpss.*'

# Audio DSP — battery and charging ride through the ADSP. redwood: adsp.{mdt,b02..b36}.
pullglob fw-audio '/vendor/firmware_mnt/image/adsp.*'

# Haptics — redwood: Awinic aw8697_haptic.bin. renoir's motor driver may differ; glob haptic bins.
pullglob fw-haptic '/vendor/firmware/*haptic*.bin'

# Modem — redwood: modem.{mdt,b00..b28}. Cellular is unfinished upstream, but pull it anyway.
pullglob fw-modem '/vendor/firmware_mnt/image/modem.*'
mkdir -p fw-modem/image && adb pull /vendor/firmware_mnt/image fw-modem/ >/dev/null 2>&1 \
	&& echo "pull-firmware.sh: fw-modem/image: the modem's own files (TFTP)"

# Cameras — HakiOS uses renamed QTI sensor/tuning bins (hakios_*). On a stock phone these are the
# normal com.qti.sensormodule.* / com.qti.tuned.* bins; HakiOS renames them during bring-up.
pullglob fw-camera '/vendor/lib64/camera/com.qti.sensormodule.*.bin' '/vendor/lib64/camera/com.qti.tuned.*.bin'

echo "pull-firmware.sh: done."
echo "pull-firmware.sh: Wi-Fi also wants fw-wifi/mac (the radio's address) and fw-wifi/network (SSID + passphrase)."
echo "pull-firmware.sh: NOTE the actual file names you got above; pin them for reproducible builds."
