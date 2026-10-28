#!/bin/bash
# Set up and run the kernel build in the Ubuntu VM (aarch64), as root, in a clone of this fork.
# What upstream's xnu/README.md asks for on Linux, without theos's interactive installer: only the
# two things xnu-deps/setup.sh takes from $THEOS -- the cctools (toolchain/linux/iphone/bin, the
# aarch64 build theos itself installs, from github.com/L1ghtmann/llvm-project) and
# sdks/iPhoneOS16.5.sdk (github.com/theos/sdks, sparse). Then xnu/apply.sh, xnu-deps/setup.sh and
# xnu-build.sh. Safe to run again: what is there is kept.
#
# usage (in the VM, from the repository top): devices/renoir/setup-kernel-vm.sh [JOBS]
set -e -o pipefail
cd "$(dirname "$0")/../.."
JOBS=${1:-$(nproc)}
export THEOS=${THEOS:-$HOME/theos}
log() { echo ">>> setup-kernel-vm: $*"; }

if command -v apt-get >/dev/null; then
	log "packages"
	DEBIAN_FRONTEND=noninteractive apt-get install -y -q build-essential git curl xz-utils clang lld \
		python3 perl flex bison m4 autoconf automake libtool libxml2-dev gawk lzfse >/dev/null
	# bsd/kern/makesyscalls.sh writes "\(" in awk strings: gawk reads it as "(", Ubuntu's default
	# mawk keeps the backslash and init_sysent.c does not compile
	update-alternatives --set awk /usr/bin/gawk
fi

if [ ! -x "$THEOS/toolchain/linux/iphone/bin/ld" ]; then
	log "cctools: iOSToolchain-$(uname -m) (L1ghtmann/llvm-project, as theos installs it)"
	mkdir -p "$THEOS/toolchain"
	curl -fsSL "https://github.com/L1ghtmann/llvm-project/releases/latest/download/iOSToolchain-$(uname -m).tar.xz" |
		tar -xJ -C "$THEOS/toolchain"
fi
"$THEOS/toolchain/linux/iphone/bin/ld" -v 2>&1 | head -1

if [ ! -d "$THEOS/sdks/iPhoneOS16.5.sdk/usr/include" ]; then
	log "iPhoneOS16.5.sdk (theos/sdks, sparse)"
	rm -rf "$THEOS/sdks-git"
	git clone -q --depth 1 --filter=blob:none --sparse https://github.com/theos/sdks.git "$THEOS/sdks-git"
	git -C "$THEOS/sdks-git" sparse-checkout set iPhoneOS16.5.sdk
	mkdir -p "$THEOS/sdks"
	ln -sfn ../sdks-git/iPhoneOS16.5.sdk "$THEOS/sdks/iPhoneOS16.5.sdk"
fi

[ -d xnu-src/xnu-8796.121.2 ] || { log "xnu/apply.sh"; xnu/apply.sh; }
log "xnu-deps/setup.sh (keeps what is done)"; xnu-deps/setup.sh
log "xnu-build.sh -j$JOBS -k (ends with an error in check_all_exports by design; see the kernel's date)"
./xnu-build.sh -j"$JOBS" -k || true
k=xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837
ls -la --time-style=+%F_%T "$k" && log "kernel: $k"
