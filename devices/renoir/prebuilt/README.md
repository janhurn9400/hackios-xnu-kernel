# renoir prebuilt (not committed)

The built boot artifacts go here, exactly as upstream's `devices/poco-x5-pro/prebuilt/`:

- `SILICIUM_UEFI.fd` — the built renoir UEFI firmware (from `uefi/`).
- `BootShim.bin` — the boot shim.
- `device-tree.dtb` — renoir's own Android/MIUI kernel device tree.
- `ramdisk` — the boot ramdisk.
- `mkbootimg.py` — image packer.

These are large and/or vendor-derived — keep them **out of git** (add to `.gitignore`). Regenerate
them with the `uefi/` build + upstream `tools/update-prebuilt.sh`.
