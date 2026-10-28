#!/bin/bash
# Run the open kernel on renoir from a Mac: the prebuilt XNU with the raw-syscall `hello` RAM disk
# (no IPSW, nothing of Apple's beyond the kernel), over the loader's USB channel. The log lands in
# logs/<time>-renoir-open.log.
#
# The loader must be up in USB mode first:
#   fastboot boot devices/renoir/prebuilt/xnu-usb-renoir.img
# (made in the VM: xnuloader/mkpayload.py -o usb.bin loader.efi=prebuilt/loader.efi --usb, then
# mkxnuimg.py -d renoir -p usb.bin). macOS leaves the loader's USB device unregistered when it
# enumerates while the loader is still starting: if `fastboot devices` lists nothing, unplug and
# re-plug the cable. xl does the same as this on Linux, but needs flock and timeout.
#
# usage: devices/renoir/run-open.sh [SECS] [kernel args]     (default: 20 s, "cpus=1 -v")
#   KERNEL=path   another kernel than prebuilt/mach.development.bcm2837 (e.g. one built in the VM)
set -e
cd "$(dirname "$0")/../.."
PATH=$HOME/Library/Android/sdk/platform-tools:$PATH
t() { perl -e 'alarm shift; exec @ARGV' "$@"; }
secs=${1:-20}; shift || true
args=${*:-cpus=1 -v}
fastboot devices | grep -q xnuloader || { echo "run-open.sh: no xnuloader on USB (boot xnu-usb-renoir.img, re-plug the cable)"; exit 1; }
mkdir -p logs
log=logs/$(date +%H%M%S)-renoir-open.log
python3 xnuloader/mkpayload.py -o run-payload.bin kernel="${KERNEL:-prebuilt/mach.development.bcm2837}" \
	ramdisk=prebuilt/userland/hello --cmdline "loader_return_after=$secs epoch=$(date +%s) $args" >/dev/null
t 60 fastboot stage run-payload.bin 2>&1 | grep -q OKAY || { echo "run-open.sh: upload failed"; exit 1; }
t 10 fastboot oem run >/dev/null 2>&1
for _ in $(seq 1 $(( secs / 3 + 40 ))); do
	result=$(t 10 fastboot getvar result 2>&1 | grep '^result:' || true)
	case "$result" in ""|*running*) sleep 3 ;; *) break ;; esac
done
echo "run-open.sh: $result"
# the loader stages loader + kernel log when the run is over; "oem log" would replace it with its own
t 30 fastboot get_staged "$log.raw" >/dev/null 2>&1
tr -d '\000' < "$log.raw" > "$log" && rm -f "$log.raw"
echo "run-open.sh: $log ($(wc -c < "$log" | tr -d ' ') bytes)"
grep -aE "Darwin Kernel Version|Hello from user space|panic" "$log" || true
