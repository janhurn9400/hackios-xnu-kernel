#!/usr/bin/env python3
"""
Bring-up aid: make XnuLoader stage 0 say on screen what it did before it resets, for when the
"logdump" partition comes back empty and the phone cannot tell why. Prints whether the loader ran,
how many GPT partition handles UEFI offers, whether "logdump" was among them and how the last write
went, then waits 30 s. Edits a Mu-Silicium tree that apply.sh has prepared; run it again after a
fresh apply.sh. Not for normal builds (the loader is meant to run unattended).

usage: debug-onscreen.py MU-SILICIUM-DIR
"""
import os, sys

lib = os.path.join(sys.argv[1], 'Silicon/Silicium/SiliciumPkg/Library/BootManagerLib')
c_path, inf_path = os.path.join(lib, 'XnuLoader.c'), os.path.join(lib, 'BootManagerLib.inf')
src = open(c_path).read()
if 'mDbgParts' in src:
    sys.exit('debug-onscreen.py: already applied')


def put(old, new):
    global src
    assert src.count(old) == 1, f'debug-onscreen.py: anchor not found once: {old!r}'
    src = src.replace(old, new)


put('#include "XnuLoader.h"\n',
    '#include "XnuLoader.h"\n#include <Library/UefiLib.h>\n\n'
    'STATIC UINTN      mDbgParts;\nSTATIC EFI_STATUS mDbgLocate = EFI_NOT_STARTED;\n'
    'STATIC BOOLEAN    mDbgFound;\nSTATIC EFI_STATUS mDbgWrite = EFI_NOT_STARTED;\n')
put('  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid, NULL, &Count, &Handles);\n',
    '  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid, NULL, &Count, &Handles);\n'
    '  mDbgLocate = Status;\n  mDbgParts  = EFI_ERROR (Status) ? 0 : Count;\n')
put('    if (!EFI_ERROR (Status)) {\n      return BlockIo;\n',
    '    if (!EFI_ERROR (Status)) {\n      mDbgFound = TRUE;\n      return BlockIo;\n')
put('  Status = BlockIo->WriteBlocks (BlockIo, BlockIo->Media->MediaId, 0, WriteSize, mLog);\n',
    '  Status = BlockIo->WriteBlocks (BlockIo, BlockIo->Media->MediaId, 0, WriteSize, mLog);\n'
    '  mDbgWrite = Status;\n')
# renoir's punch hole sits top left (x 67-131 px) and the console font is tiny at 1080x2400:
# start a quarter down the screen, well in from the left edge
PAD = ' ' * 16
put('  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);\n',
    '  gST->ConOut->ClearScreen (gST->ConOut);\n'
    '  AsciiPrint ("' + '\\n' * 30 + PAD + 'XNULOADER stage 0 ran\\n\\n'
    + PAD + 'partition handles: %lu (locate: %r)\\n\\n'
    + PAD + 'logdump found: %a\\n\\n' + PAD + 'last write: %r\\n\\n' + PAD + 'log bytes: %lu\\n\\n'
    + PAD + 'reset in 60 s\\n",\n'
    '              (UINT64)mDbgParts, mDbgLocate, mDbgFound ? "yes" : "no", mDbgWrite, (UINT64)mLogLen);\n'
    '  gBS->Stall (60000000);\n'
    '  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);\n')
open(c_path, 'w').write(src)

inf = open(inf_path).read()
if '  UefiLib\n' not in inf and '  UefiLib\r\n' not in inf:
    nl = '\r\n' if '\r\n' in inf else '\n'
    anchor = '  BaseMemoryLib' + nl
    assert inf.count(anchor) == 1, 'debug-onscreen.py: BaseMemoryLib not in BootManagerLib.inf'
    open(inf_path, 'w', newline='').write(inf.replace(anchor, anchor + '  UefiLib' + nl))
print('debug-onscreen.py: XnuLoader prints a summary and waits 30 s before it resets')
