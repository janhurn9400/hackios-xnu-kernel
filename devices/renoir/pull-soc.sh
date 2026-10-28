#!/bin/bash
# Pull renoir's own device tree over adb and turn it into the SoC boot-args HakiOS needs on this
# device (upstream tools/soc-args.py). On renoir every generated value equals redwood's kernel
# default except soc_ipa_fw (ipa_fws). Three values soc-args.py does not find on renoir because its
# nodes are named differently; they are in soc.args by hand:
#   soc_aoss=0xc300000      qcom,qmp-aop@c300000 (compatible qcom,qmp-mbox)
#   soc_cmd_db=0x80860000   reserved-memory@80860000 (compatible qcom,cmd-db)
#   soc_mpss_mem=0x8b800000 reserved-memory/mpss_wlan@8b800000
#
# /sys/firmware/fdt is not readable by the adb shell on renoir (MIUI 14 vendor): this uses root.
# Copy it to a file as root and adb pull that: `su -c cat` through adb runs in a pty that turns
# every 0x0a into 0d 0a and corrupts the blob (the file comes out longer than the header says).
set -e
cd "$(dirname "$0")/../.."
PATH=$PWD/tools/macos-bin/platform-tools:$HOME/Library/Android/sdk/platform-tools:$PATH
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
if ! adb pull /sys/firmware/fdt "$tmp/fdt" >/dev/null 2>&1; then
	adb shell "su -c 'cp /sys/firmware/fdt /data/local/tmp/fdt.dtb && chmod 644 /data/local/tmp/fdt.dtb'" \
		|| { echo "pull-soc.sh: /sys/firmware/fdt needs root and su failed"; exit 1; }
	adb pull /data/local/tmp/fdt.dtb "$tmp/fdt" >/dev/null
	adb shell rm -f /data/local/tmp/fdt.dtb
fi
python3 - "$tmp/fdt" <<'EOF' || exit 1
import struct, sys
d = open(sys.argv[1], 'rb').read()
total = struct.unpack('>I', d[4:8])[0]
if d[:4] != b'\xd0\x0d\xfe\xed' or total != len(d):
    sys.exit(f'pull-soc.sh: device tree damaged in transfer (header {total} bytes, file {len(d)})')
EOF
tools/soc-args.py "$tmp/fdt" | tee "$(dirname "$0")/soc.args.generated"
echo "pull-soc.sh: wrote $(dirname "$0")/soc.args.generated; compare with soc.args"
