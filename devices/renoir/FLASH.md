# renoir: iOS with its root on the flash (2026-10-29)

Status: **iOS runs from the flash** (`root_apfs=ufs_darwin`), as upstream's redwood does. The resident image is a
67 MB boot volume instead of the 2.5 GB system image, so iOS has the memory it was missing.

## Why

With the system image in RAM the kernel had about 4.3 GB (the loader hands it the largest free block,
0xF4000000..0x200000000; renoir has 6 GB in three banks: 0x80000000 0.9 GB, 0xC0000000 1 GB, 0x100000000 4 GB).
`vm_stat` in iOS: **3.66 GB wired, 20 MB free**. `mediaserverd` could then not lock the 64 KB I/O buffer of an
audio device (`HALB_IOBufferManager ... failed to lock the buffer ... errno 35`, then `DoStartIO failed`
`'nope'`): sound and haptics stopped together, at once when Safari/WebKit took its share. From the flash:
**1.6 GB wired**, ~2.7 GB for applications and cache; haptics stay with Safari loading pages (user, 21:38).

## The partition `darwin`

20 GiB cut off the end of `userdata`, as upstream did (`docs/ru/notes.md`, "Раздел порта — darwin"), from
MagicOS (Android 16, Magisk root) in one session:

| | before | after |
|---|---|---|
| `userdata` (sda34) | LBA 2785280..30617594, 106.17 GiB | 2785280..25374714, 86.17 GiB (the f2fs exactly) |
| `darwin` (sda35, entry 34) | -- | **LBA 25374720..30617594, 20.000 GiB**, type APFS container, unique GUID e101c5e9-5052-4d8a-b246-ebf4723978bb |

renoir's `userdata` has metadata encryption (`dm-default-key`, plus FBE): the raw partition is ciphertext. The
f2fs superblock and segment info were read from the decrypted device instead, `/dev/block/dm-44`, whose size is
sda34's (222658520 sectors, 1:1). Steps:

1. Saved (read only) into `fw-backup/renoir-userdata/` (git-ignored): `gpt-head.bin` (LUN 0's first 8 blocks),
   `gpt-tail.bin` (its last 8, `skip=30617592`), `userdata-meta-before.bin` (dm-44 block 0),
   `segment_info-before.txt` (`/proc/fs/f2fs/dm-44/segment_info`). **To undo: write `gpt-head.bin` and
   `gpt-tail.bin` back** (then Android grows its f2fs again at the next boot).
2. `tools/userdata-carve.py fw-backup/renoir-userdata 20 darwin` (dry run on the Mac): the old table checks out,
   the f2fs holds data up to segment 29323 of 54130.
3. `tools/android/f2fs-resize` (built in the VM, `tools/android/build.sh`'s command) `/data 22588928`: done.
4. `sb-after.bin`, the tool again -> `gpt-head.new` / `gpt-tail.new`; written with its three `dd` commands and
   read back (sha256 `1566ef7b...` / `e95b737a...`). (Android's toybox `dd` has no `iflag=direct`: read back
   with `echo 3 > /proc/sys/vm/drop_caches` first.)
5. MagicOS rebooted: `/data` 86 GB, sda35 41943000 sectors. Nothing else in the table changed.

## What is in `darwin`

| blocks of 4 KB | what | sha256 |
|---|---|---|
| 0..1374207 | `rootfs.apfs` (5628755968 bytes): the APFS container of `sysimage/rootfs.dmg` (macOS: `hdiutil convert -format UDTO`, then bytes from LBA 34; `tools/apfs/dmg.py` wants the `lzfse` module) | `9f430a58...` |
| 1374208..2039807 | `sysimage/cryptex-os.dmg` as it is | `2279bf44...` |
| 2039808..2042367 | `sysimage/cryptex-app.dmg` as it is | `6b9d1f09...` |

The same as upstream's. Written from MagicOS: `adb push` of the three files to `/data/local/tmp`, sha256 there,
then a guard script (`dw.sh`: refuses unless `/sys/class/block/sda35` says `PARTNAME=darwin`, start 202997760,
size 41943000, partition 35) `dd`s each to sda35 with `bs=4M seek=0 / 1342 / 1992`; read back with the caches
dropped: all three sha256 the same. (`tools/darwin-write.py` is redwood's: sda34, its GUID, `/dev/block/by-name/
darwin`, which MagicOS does not make. Magisk's `su -c` does not pass `adb exec-in`'s stdin: copy first.)

## Booting it

Images (VM, `cd userland && CRYPTEXES=flash FLASHBOOT=1 GL=1 REST=1 RC_JOBS=rc.jobs.renoir ./build.sh`; needs
`../fw-wifi/{amss20.bin,bd_k9gl.elf,m3.bin,network}` in the VM's tree, now in `build.sh`):
`prebuilt/sysimg/flashboot-renoir.apfs` (67 MB: stage0, the ADSP firmware, the package manager's prefix with
Sileo) and `prebuilt/sysimg/root-flash-wifi-ssh.apfs` (init, jobs for the whole volume, GPU/Wi-Fi/ADSP firmware,
sshd).

```
fastboot oem ramdisk; fastboot stage devices/renoir/prebuilt/sysimg/flashboot-renoir.apfs     # 2 s
FOLLOW=10 KERNEL=devices/renoir/prebuilt/vm-kernel/mach.development.bcm2837.touch \
RAMDISK=devices/renoir/prebuilt/sysimg/root-flash-wifi-ssh.apfs devices/renoir/run-open.sh 1800 \
  root_apfs=ufs_darwin ufs=2 sb_delay=8 bootcpu=7 cpus=8 cpufreq=1 touchscreen=1 board=renoir gpu=1 haptic=5 \
  wifi=1 wifi_file=0 wifi_mac=5c:d0:6e:ea:63:12
```

The log: `-- the iOS root volume on /dev/ufs_darwin as the root --`, `[ ok ] the OS cryptex (/dev/ufs_darwin,c=1)`,
`[ ok ] the App cryptex (...c=2)`; `mount`: `/dev/ufs_darwin on / (apfs, local, read-only)`. Reads go through the
loader (`ufs_native` not tried on renoir yet).

- `wifi_file=0`: the kernel does not join `fw/network` itself. Away from home it found a neighbour's network
  with the home network's name, failed its handshake (reason 15), and after that iOS's own join from Settings
  got no address (169.254...). With `wifi_file=0` iOS joins the user's iPhone hotspot and gets 172.20.10.2.
- SSH: `ssh -i ssh/id_ed25519 root@<address>` (the phone's hotspot, Mac in the same network).
- Sileo: register it after each boot with `/var/jb/bin/sh /var/jb/prep.sh` over SSH (rc.jobs.renoir does not run
  it). Sources: Procursus, Havoc, Chariz.
- **iOS keeps its data** (`var_store=1 ufs_write=1`, 2026-10-29 22:45): `/private/var` is packed into darwin from
  block 2097152 (two 2 GiB slots, written in turn; `userland/src/var_store.c`) when the run ends in order
  (`fastboot oem stop`, ~8 s, ~160 MB), and unpacked by init at the next boot. Checked by the user: dark mode,
  a moved icon and the hotspot came back. The loader serves writes only to a darwin it recognises: it had
  redwood's unique GUID alone; `xnuloader/loader.c` now also takes renoir's (`BLOCK_DARWIN_UNIQ_RENOIR`), the
  name, type, LBAs, size and unit still checked; image `prebuilt/xnu-usb-renoir-crashlog.img` (22:28; the one
  before as `.before-renoir-guid`). The log: `ufs: darwin (LBA 25374720..30617594 of unit 0): its blocks from
  2042368 on may be written this run`.
- `var_store_secs=N` (new): saves every N seconds as well. Needed with `adsp=1`, where USB is gone and the run
  cannot be ended in order. The everyday run:
  `... var_store=1 ufs_write=1 var_store_secs=300 ... cpufreq=1 fb_scan=1 fb_threads=6 fb_filter=0 ... haptic=5 torch=3 adsp=1 sound=4 sound_below=144 wifi=1 wifi_file=0 ...`
- **Torch** (`torch=3`): works (`torch=2` test, and in iOS without the DSP). With `adsp=1` it stays off while the
  battery is at 45 C or more (the driver's guard): renoir charged at 1.5 A at 100 % and the battery reached 46 C
  (22:50). Nothing manages heat yet (no thermal throttling of the charger, CPU or GPU): the next thing to do.
- After `fastboot oem stop` the phone sometimes resets into Xiaomi's fastboot a little later: boot the loader and
  stage the boot volume again before the next run.

## Heat and frames (2026-10-30, 02:20)

- **Thermal guard** (`hakios.cpufreq.c`, `thermal_thread`, with `cpufreq=1`; `thermal=0` turns it off): once a
  second the hottest TSENS sensor (both controllers, TM at 0x0c263000 / 0x0c265000, `SN_STATUS` 0xa0 + 4 x n,
  tenths of a degree, bit 21 valid) and the battery's temperature (from the ADSP's battery manager) pick a level,
  and every CPU group is held at or under 100/80/60/40 % of its top frequency (battery from 40/42/44 C, SoC from
  75/85/95 C; left 2 C / 5 C lower). Before it every core ran at its top row all the time. Seen: idle 34.7 C,
  using the phone 48-59 C, level 0. Kernel `prebuilt/vm-kernel/mach.development.bcm2837` (= `.thermal`, 02:06).
- From the Mi 10 fork (`JopangJovan/hackios-xnu-kernel-umi`, `devices/xiaomi-mi-10/boot.args`): `fb_threads=6
  fb_filter=0` (the frame copied by six threads, nearest-neighbour scaling): already supported here, now in the run
  command; the user found it quick ("richtig schnell"), not measured yet. Still at 60 Hz and tearing.
- Done (see GPU.md, 02:31; the user: no more tearing): that fork's `hakios.display.c` (`fb_scan=1`): the copy timed to the command-mode panel's DMA and TE
  (INTF 0x0ae6b800 +0x298 TE line, +0x2b0 DMA write pointer; dispcc 0x3000/0x2080 as renoir's), the panel's own rate
  given to CoreAnimation. Also there: Bluetooth on QCA6390 over UART to iOS's stack, brightness by DCS 0x51.
