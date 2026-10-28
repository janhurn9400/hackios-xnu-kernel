# renoir device tools

The device-tree tools are **upstream and device-agnostic** — do not fork them here. Use them from
the HakiOS repo top:

- `tools/soc-args.py DEVICE-TREE.dtb` — reads `qcom,ipa`, `qcom,ipcc`, AOSS, RPMh and `apps-smmu`
  nodes and prints the `soc_*` boot-args. `pull-soc.sh` runs it for you.
- `tools/cellular_probe.py FDT [--serial N]` — read-only modem inventory from renoir's device tree
  and (optionally) MIUI over adb. It deliberately omits IMEI/IMSI/phone numbers/SIM IDs. Use it to
  find the modem/mpss/ipa/smem/reserved-memory nodes you need for the hand-added `soc_*` memory
  addresses.

renoir-specific tooling only goes here if renoir needs something redwood's `tools/modem_firmware.py`
/ `tools/cellular_probe.py` don't cover — e.g. a different modem image layout. Add it then, not now.
