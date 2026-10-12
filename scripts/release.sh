#!/usr/bin/env bash
# Builds the Release archive, checks it is clean and publishes it as a GitHub release.
# Usage: scripts/release.sh <version> [notes.md]   (version without "v", tag v<version> must be pushed)
#        scripts/release.sh --build-only <version>
set -euo pipefail

publish=1
if [ "${1:-}" = "--build-only" ]; then
	publish=0
	shift
fi
version=${1:?usage: $0 [--build-only] <version> [notes.md]}
notes=${2:-}

root=$(cd "$(dirname "$0")/.." && pwd)
build=${BUILD_DIR:-$root/out/arch-release}
stage="$root/out/release-stage"
archive="$root/out/moegram-v$version-x86_64.tar.zst"
tde2e="$root/out/arch/deps/tdlib-install/lib/cmake/tde2e"
jobs=${JOBS:-8}

if [ "$publish" -eq 1 ] && [ -z "$notes" ]; then
	echo "notes file is required to publish" >&2
	exit 2
fi
if [ ! -d "$tde2e" ]; then
	"$root/scripts/build_tde2e.sh"
fi

cmake -S "$root" -B "$build" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=/usr \
	-DDESKTOP_APP_USE_PACKAGED=ON \
	-DDESKTOP_APP_DISABLE_AUTOUPDATE=ON \
	-DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON \
	-DTDESKTOP_API_TEST=ON \
	-Dtde2e_DIR="$tde2e"
nice cmake --build "$build" --target Telegram --parallel "$jobs"

rm -rf "$stage"
DESTDIR="$stage" cmake --install "$build"
strip --strip-unneeded "$stage/usr/bin/moegram"
tar -C "$stage" -c --zstd -f "$archive" usr

"$root/scripts/check_release_clean.sh" "$stage/usr/bin/moegram" "$archive"

sum=$(sha256sum "$archive" | cut -d' ' -f1)
echo "$archive"
echo "sha256 $sum"
if [ "$publish" -eq 0 ]; then
	exit 0
fi

body=$(mktemp)
trap 'rm -f "$body"' EXIT
{ cat "$notes"; printf '\n\nsha256 (`%s`):\n\n```\n%s\n```\n' "$(basename "$archive")" "$sum"; } > "$body"
gh release create "v$version" "$archive" \
	-R tenku-png/moegram \
	--verify-tag \
	--title "v$version" \
	--notes-file "$body"
