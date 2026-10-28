#!/bin/bash
# Put HakiOS into a Mu-Silicium tree that already has renoirPkg (upstream Mu-Silicium does).
# Unlike redwood's apply.sh this does not clone at a fixed BASE: renoirPkg is upstream, so it works
# on a current tree (build-renoirpkg.sh makes one). What it adds:
#   - redwood's mu-silicium.patch (BootShim XNUP copy, BootManager starts XnuLoaderMain); device-neutral
#   - XnuLoader.[ch] from redwood's port; device-neutral (payload at 0xF0000000, log to "logdump")
#   - the "XNU Payload" region at 0xF0000000 + 0x04000000 in renoirPkg's memory map. Free on renoir:
#     the map ends at Display Reserved (0xE3400000), and no reserved-memory node of renoir's own
#     device tree overlaps it.
# Running it twice is harmless.
#
# usage: devices/renoir/uefi/apply.sh MU-SILICIUM-DIR
set -e
MU=$(readlink -f "${1:?usage: apply.sh MU-SILICIUM-DIR}")
HERE=$(dirname "$(readlink -f "$0")")
REDWOOD=$HERE/../../poco-x5-pro/uefi
[ -d "$MU/Platforms/Xiaomi/renoirPkg" ] || { echo "apply.sh: $MU has no renoirPkg"; exit 1; }

if git -C "$MU" apply --check --whitespace=nowarn "$REDWOOD/mu-silicium.patch" 2>/dev/null; then
	git -C "$MU" apply --whitespace=nowarn "$REDWOOD/mu-silicium.patch"
	echo "apply.sh: BootShim + BootManagerLib patched"
elif git -C "$MU" apply --check --reverse --whitespace=nowarn "$REDWOOD/mu-silicium.patch" 2>/dev/null; then
	echo "apply.sh: BootShim + BootManagerLib already patched"
else
	echo "apply.sh: mu-silicium.patch does not apply to $MU"; exit 1
fi

cp "$REDWOOD"/files/Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/XnuLoader.[ch] \
	"$MU/Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/"

python3 - "$MU/Platforms/Xiaomi/renoirPkg/Library/MemoryMapLib/MemoryMapLib.c" <<'EOF'
import sys
path = sys.argv[1]
src = open(path, 'rb').read().decode()
if '"XNU Payload"' in src:
    print('apply.sh: XNU Payload region already in the memory map'); sys.exit()
anchor = src.index('{"Display Reserved"')
eol = src.index('\n', anchor) + 1
nl = '\r\n' if src[eol - 2:eol] == '\r\n' else '\n'
line = '  {"XNU Payload",       0xF0000000, 0x04000000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},' + nl
open(path, 'wb').write((src[:eol] + line + src[eol:]).encode())
print('apply.sh: XNU Payload region added to renoirPkg memory map')
EOF
echo "apply.sh: done; build with: python3 build_uefi.py -d renoir -r RELEASE (in $MU, venv active)"
