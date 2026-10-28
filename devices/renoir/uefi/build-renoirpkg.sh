#!/usr/bin/env bash
#
# Phase 1 — baut das STOCK renoirPkg UEFI (noch ohne HakiOS) in einer Ubuntu/Debian-VM.
# Ziel: die Mu-Silicium-Basis für renoir bauen und als boot.img per `fastboot boot` testen,
# BEVOR HakiOS draufgelegt wird (siehe APPLYING-HAKIOS.md für Phase 4).
#
# Gedacht für die Ubuntu-VM (auch Apple-Silicon/arm64 ist ok — der Build ist aarch64/CLANGPDB).
# NICHT auf dem macOS-Host getestet; Mu-Silicium ist Linux-only.
#
# Nutzung:
#   chmod +x build-renoirpkg.sh && ./build-renoirpkg.sh [ZIEL-VERZEICHNIS]
#
set -euo pipefail

WORK="${1:-$HOME/hakios-build}"
MU="$WORK/Mu-Silicium"

echo ">>> Arbeitsverzeichnis: $WORK"
mkdir -p "$WORK"

# ---- 1. Pakete (Debian/Ubuntu) --------------------------------------------------------------
# Entspricht Mu-Siliciums setup_env.sh (redwood-README nennt die Arch-Pendants:
#   git base-devel lld nasm clang llvm aarch64-linux-gnu-gcc python python-pip python-virtualenv)
if command -v apt-get >/dev/null 2>&1; then
  echo ">>> installiere Build-Pakete (sudo)…"
  sudo apt-get update
  sudo apt-get install -y \
    git build-essential lld nasm clang llvm \
    gcc-aarch64-linux-gnu python3 python3-pip python3-venv \
    device-tree-compiler mono-devel uuid-dev
else
  echo "!! kein apt-get — installiere die Äquivalente von: git build-essential lld nasm clang llvm"
  echo "   gcc-aarch64-linux-gnu python3 python3-venv device-tree-compiler mono-devel  und starte neu."
  exit 1
fi

# ---- 2. Mu-Silicium holen (hat renoirPkg bereits upstream) ----------------------------------
if [ ! -d "$MU" ]; then
  echo ">>> klone Mu-Silicium…"
  git clone https://github.com/Project-Silicium/Mu-Silicium.git "$MU"
fi
cd "$MU"

# ---- 3. Python-Umgebung (edk2-pytool-extensions / stuart) ------------------------------------
echo ">>> Python-venv + pip-requirements…"
python3 -m venv venv
# shellcheck disable=SC1091
source venv/bin/activate
pip install --upgrade pip
pip install -r pip-requirements.txt

# ---- 4. stuart setup: holt die Submodule (Mu_Basecore usw.) ---------------------------------
# Muss VOR der nasm-Umgehung laufen: die legt Dateien in Mu_Basecore/ an, und ein nicht-leerer
# Ordner lässt das Submodul-Klonen scheitern ("already exists and is not an empty directory").
DB="Platforms/Xiaomi/renoirPkg/DeviceBuild.py"
echo ">>> stuart setup…";  python3 "$DB" --setup

# ---- 5. mono/nasm-Umgehung (aus redwoods uefi/README) ---------------------------------------
# mu_nasm kommt über NuGet und will Mono; ohne/bei Problemen auf System-nasm zeigen.
NASM_BIN="$(command -v nasm || true)"
NDISASM_BIN="$(command -v ndisasm || true)"
if [ -n "$NASM_BIN" ]; then
  # host-spezifisch: x86-64 (redwoods PC) und ARM-64 (Apple-Silicon-VM) — stuart sucht <OS>-<ARCH>-<BITS>
  for H in Linux-x86-64 Linux-ARM-64; do
    D="Mu_Basecore/BaseTools/Bin/mu_nasm_extdep/$H"
    mkdir -p "$D"
    ln -sf "$NASM_BIN" "$D/nasm"
    [ -n "$NDISASM_BIN" ] && ln -sf "$NDISASM_BIN" "$D/ndisasm" || true
  done
  echo 'version: 20016.1.1' > "Mu_Basecore/BaseTools/Bin/mu_nasm_extdep/extdep_state.yaml"
  echo ">>> nasm-Umgehung gesetzt ($NASM_BIN)"
fi

# ---- 6. renoirPkg: update -> build ----------------------------------------------------------
# build_uefi.py (Mu-Silicium) statt DeviceBuild.py direkt: setzt FD_BASE/FD_SIZE aus
# Resources/Configs/renoir.toml; DeviceBuild.py kennt kein "-t".
echo ">>> stuart update…"; python3 "$DB" --update
echo ">>> build RELEASE…"; python3 build_uefi.py -d renoir -r RELEASE

FD="$(find Build/renoirPkg -name '*.fd' 2>/dev/null | head -1 || true)"
echo
if [ -n "$FD" ]; then
  echo ">>> FERTIG: $MU/$FD"
  echo ">>> Nächster Schritt: in ein boot.img packen und testen —"
  echo ">>>   fastboot boot boot.img   (renoir in fastboot; nichts wird geflasht)"
  echo ">>> Für HakiOS-Integration danach: siehe ../uefi/APPLYING-HAKIOS.md"
else
  echo "!! Kein .fd gebaut — Build-Log oben prüfen (häufig: fehlende Pakete, nasm/mono, Submodule)."
  exit 1
fi
