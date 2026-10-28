#!/bin/bash
# Pull renoir's own device tree over adb and turn it into the SoC boot-args HakiOS needs on this
# device (upstream tools/soc-args.py). redwood matched the kernel's built-in values; renoir will
# not, so this writes renoir's real addresses into soc.args. Needs renoir in MIUI with adb (no
# root: /sys/firmware/fdt is world-readable).
set -e
cd "$(dirname "$0")/../.."
PATH=$PWD/tools/macos-bin/platform-tools:$PATH
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
adb pull /sys/firmware/fdt "$tmp/fdt" >/dev/null || { echo "pull-soc.sh: cannot read /sys/firmware/fdt over adb"; exit 1; }
tools/soc-args.py "$tmp/fdt" | tee "$(dirname "$0")/soc.args.generated"
echo "pull-soc.sh: wrote $(dirname "$0")/soc.args.generated"
echo "pull-soc.sh: now add the reserved-memory addresses by hand (soc_smem, soc_cmd_db,"
echo "pull-soc.sh: soc_mpss_mem, modem_ipa_fw_at, soc_imem) and move it into place as soc.args."
echo "pull-soc.sh: tip: tools/cellular_probe.py \"$tmp/fdt\" lists the modem/ipa/smem nodes to read them from."
