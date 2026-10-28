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

# ---- 4. mono/nasm-Umgehung (aus redwoods uefi/README) ---------------------------------------
# mu_nasm kommt über NuGet und will Mono; ohne/bei Problemen auf System-nasm zeigen.
NASM_BIN="$(command -v nasm || true)"
NDISASM_BIN="$(command -v ndisasm || true)"
if [ -n "$NASM_BIN" ]; then
  D="Mu_Basecore/BaseTools/Bin/mu_nasm_extdep/Linux-x86-64"
  mkdir -p "$D"
  echo 'version: 20016.1.1' > "Mu_Basecore/BaseTools/Bin/mu_nasm_extdep/extdep_state.yaml"
  ln -sf "$NASM_BIN" "$D/nasm"
  [ -n "$NDISASM_BIN" ] && ln -sf "$NDISASM_BIN" "$D/ndisasm" || true
  echo ">>> nasm-Umgehung gesetzt ($NASM_BIN)"
fi

# ---- 5. renoirPkg: setup -> update -> build -------------------------------------------------
DB="Platforms/Xiaomi/renoirPkg/DeviceBuild.py"
echo ">>> stuart setup…";  python3 "$DB" --setup
echo ">>> stuart update…"; python3 "$DB" --update
echo ">>> build RELEASE…"; python3 "$DB" -t RELEASE

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
