#!/bin/bash
# One Wi-Fi test run (kernel prebuilt/vm-kernel, ramdisk prebuilt/sysimg/root-wifi.apfs): stage, run, wait,
# "fastboot reboot-bootloader" (the reboot reason stays set), the crashlog loader, "oem log" into logs/.
# usage: devices/renoir/wifi-run.sh NAME WAIT_SECONDS BOOT-ARGS...  (e.g. wifi-run.sh mhi 110 pcie=9 pcie_pause=0)
cd /Users/jantragbauer/Desktop/ROMS/hackios-xnu-kernel
F=~/Library/Android/sdk/platform-tools/fastboot; t() { perl -e 'alarm shift; exec @ARGV' "$@"; }
name=$1; wait=$2; shift 2
t 3 $F devices | grep -q xnuloader || { t 20 $F boot devices/renoir/prebuilt/xnu-usb-renoir-crashlog.img >/dev/null 2>&1; for i in $(seq 1 30); do sleep 2; t 3 $F devices 2>/dev/null | grep -q xnuloader && break; done; sleep 4; }
python3 xnuloader/mkpayload.py -o run-payload.bin kernel=devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837 ramdisk=devices/renoir/prebuilt/sysimg/root-wifi.apfs --cmdline "loader_return_after=200 epoch=$(date +%s) bootcpu=7 cpus=1 board=renoir $* wifi_return=1 -v" >/dev/null
t 60 $F stage run-payload.bin >/dev/null 2>&1; t 10 $F oem run >/dev/null 2>&1
sleep $wait
t 10 $F reboot-bootloader >/dev/null 2>&1
for i in $(seq 1 60); do sleep 2; t 3 $F devices 2>/dev/null | grep -q 71c9b089 && break; done
t 20 $F boot devices/renoir/prebuilt/xnu-usb-renoir-crashlog.img >/dev/null 2>&1
for i in $(seq 1 30); do sleep 2; t 3 $F devices 2>/dev/null | grep -q xnuloader && break; done; sleep 3
L=logs/$(date +%H%M%S)-$name.log
t 30 $F oem log >/dev/null 2>&1; t 60 $F get_staged $L >/dev/null 2>&1
echo $L
