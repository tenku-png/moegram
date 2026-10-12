#!/usr/bin/env bash
# Builds tde2e from TDLib into out/arch/deps/tdlib-install without build machine paths.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
commit=022d60202e446ad1287b9fb68e687c8a0760788b
deps="$root/out/arch/deps"
src="$deps/tdlib"
build="$deps/tdlib-build"
install="$deps/tdlib-install"
jobs=${JOBS:-8}

if [ ! -d "$src/.git" ]; then
	git clone https://github.com/tdlib/td.git "$src"
fi
git -C "$src" fetch --quiet origin "$commit" 2>/dev/null || true
git -C "$src" checkout --quiet "$commit"

flags="-ffile-prefix-map=$HOME=~ -ffile-prefix-map=$src=td -ffile-prefix-map=$build=td-build"
rm -rf "$build" "$install"
cmake -S "$src" -B "$build" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DTD_E2E_ONLY=ON \
	-DCMAKE_C_FLAGS="$flags" \
	-DCMAKE_CXX_FLAGS="$flags" \
	-DCMAKE_INSTALL_PREFIX="$install"
nice cmake --build "$build" --parallel "$jobs"
cmake --install "$build"
