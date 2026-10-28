# UEFI for renoir

HakiOS boots XNU from a UEFI firmware ported from
[Project Silicium / Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium). redwood's UEFI
is a Mu-Silicium port modelled on `lisa` (Xiaomi 11 Lite 5G NE, SM7325 — same SoC package).

## ✅ GOOD NEWS: renoir already has a Mu-Silicium platform

Checked the Mu-Silicium repo (2026-10-28): **`Platforms/Xiaomi/renoirPkg` already exists and is
actively maintained** (last touched 2026-08-29). This is the single biggest piece of the port, and
it's already built for us. What's there:

- `Platforms/Xiaomi/renoirPkg/` — full platform: `renoir.dsc/.dec/.fdf`, `DeviceBuild.py`
  (stuart build, AARCH64, RELEASE/DEBUG), `MemoryMapLib`, `ConfigurationMapLib`.
- `Resources/Configs/renoir.toml` — UEFI FD `base = 0x9FD00000`, `size = 0x200000`; boot shim
  requires a kernel header; boot image header v1, gzip kernel, append dtb.
- `Resources/DTBs/renoir.dtb` + `renoir.dts` — renoir's device tree, already extracted.

**Crucially:** `renoir.dsc` includes `CedrosPkg/CedrosPkg.dsc.inc` — the **same SM7325/SM7350 SoC
package that `lisa` uses**. redwood's HakiOS UEFI is a lisa-based port, so renoir uses the *same SoC
package as redwood's base*. That means the HakiOS-specific UEFI changes are nearly drop-in here.

Build it stock first, to confirm it boots on your renoir, before adding HakiOS:

```sh
# in a Mu-Silicium checkout, per its setup_env.sh
python3 Platforms/Xiaomi/renoirPkg/DeviceBuild.py --setup
python3 Platforms/Xiaomi/renoirPkg/DeviceBuild.py --update
python3 Platforms/Xiaomi/renoirPkg/DeviceBuild.py -t RELEASE
# -> Build/renoirPkg/RELEASE_CLANGPDB/FV/*.fd ; package to boot.img and `fastboot boot` it
```

- Mu-Silicium platforms: https://github.com/Project-Silicium/Mu-Silicium
- UEFIReader (extracts the phone's own UEFI drivers from `xbl.img`): https://github.com/WOA-Project/UEFIReader

## Adding HakiOS to the existing renoirPkg (the actual remaining work)

Because renoirPkg already exists, you do **not** port Mu-Silicium from scratch. You replay the same
HakiOS changes redwood applied to its lisa-based package onto renoirPkg:

1. `BootShim/BootShim.S` — copy the "XNUP" container behind the UEFI image to `0xF0000000`.
2. `BootManagerLib` — start the XNU loader (`XnuLoader.[ch]`) from the container instead of a menu.

Mirror redwood's `uefi/files/` into this package: `XnuLoader.[ch]` under `BootManagerLib`, and point
at `Resources/Configs/renoir.toml` + `Resources/DTBs/renoir.dtb` (both already present upstream).
The renoir-specific memory map and extracted firmware `Binaries/` that redwood had to make are
**already in renoirPkg** — that's the work that's saved.

## If you must port it (modelled on redwood's `uefi/`)

redwood's `uefi/` is structured as:

- `BASE` — the Mu-Silicium commit the port is based on.
- `mu-silicium.patch` — changes to existing files:
  - `BootShim/BootShim.S` (copies the "XNUP" container behind the UEFI image to `0xF0000000`),
  - `BootManagerLib` (starts the XNU loader from the container instead of a menu).
- `files/` — new files:
  - `Platforms/Xiaomi/renoirPkg` (memory map, driver list) — **renoir's own memory map**,
  - `Silicon/.../BootManagerLib/XnuLoader.[ch]`,
  - `Resources/Configs/renoir.toml`,
  - `Resources/DTBs/renoir.dtb` (from renoir's `vendor_boot`),
  - `Binaries/renoir` (renoir's own-firmware UEFI drivers, extracted from `xbl.img` with
    UEFIReader).
- `apply.sh` / `export.sh` — clone+apply the port / save changes back.

Because renoir is the SM7350 sibling of redwood's SM7325, the `KodiakPkg`-class SoC package and the
driver list should be close; the memory map, DTB and extracted `Binaries/` are what's genuinely
renoir-specific.

## Build (per upstream)

```sh
devices/renoir/uefi/apply.sh          # clones Mu-Silicium (+submodules, ~3 GB) and applies the port
python3 -m venv venv && venv/bin/pip install -r Mu-Silicium/pip-requirements.txt
cd Mu-Silicium
source ../venv/bin/activate
python3 build_uefi.py -d renoir -r RELEASE   # -> Build/renoirPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd
```

After a build, upstream `mkxnuimg.py` (via `./xl enter`) takes the UEFI, and
`tools/update-prebuilt.sh` refreshes `devices/renoir/prebuilt/`.
