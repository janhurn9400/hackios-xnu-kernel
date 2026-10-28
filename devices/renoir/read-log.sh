#!/bin/bash
# Read the XNU loader's text log back from renoir in Android (rooted, on adb).
#
# renoir has two GPT partitions named "logdump": sda30 (LUN 0) and sde55 (LUN 4). Android's
# /dev/block/by-name/logdump points at sde55, but the loader writes to the first one UEFI lists,
# sda30, so by-name reads only zeros. This finds the one that carries the log by its PARTNAME and
# its "== XNULOADER" header.
#
# usage: devices/renoir/read-log.sh [OUT.txt]
set -e
PATH=$HOME/Library/Android/sdk/platform-tools:$PATH
out=${1:-xnuloader-renoir.log}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
devs=$(adb shell "su -c 'grep -l PARTNAME=logdump /sys/class/block/*/uevent'" | tr -d '\r' | sed 's#/sys/class/block/##; s#/uevent##')
for d in $devs; do
	adb shell "su -c 'dd if=/dev/block/$d of=/data/local/tmp/xnulog.img bs=256k count=1 2>/dev/null; chmod 644 /data/local/tmp/xnulog.img'"
	adb pull /data/local/tmp/xnulog.img "$tmp/$d.img" >/dev/null
	adb shell rm -f /data/local/tmp/xnulog.img
	if head -c 32 "$tmp/$d.img" | grep -q '== XNULOADER'; then
		tr -d '\000' < "$tmp/$d.img" > "$out"
		echo "read-log.sh: $d -> $out ($(wc -c < "$out" | tr -d ' ') bytes)"
		exit 0
	fi
done
echo "read-log.sh: no loader log in any logdump partition ($devs)"; exit 1
