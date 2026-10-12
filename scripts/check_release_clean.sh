#!/usr/bin/env bash
# Fails if a release binary or archive leaks build machine paths, the author's mail or profile data.
# Usage: scripts/check_release_clean.sh <binary or .tar.* archive>...
set -euo pipefail

if [ $# -eq 0 ]; then
	echo "usage: $0 <binary or archive>..." >&2
	exit 2
fi

user=${USER:-$(id -un)}
# WHY: a bare user name can be a common word in protocol strings, so only path and mail forms count.
pattern="/home/|/${user}(/|\$)|~${user}|${user}@"
# Extra words (mail, nicknames) live outside git in .release-forbidden, one per line.
forbidden="$(cd "$(dirname "$0")/.." && pwd)/.release-forbidden"
if [ -f "$forbidden" ]; then
	while IFS= read -r word; do
		if [ -n "$word" ]; then
			pattern="$pattern|$word"
		fi
	done < "$forbidden"
else
	echo "warning: no .release-forbidden, only paths are checked" >&2
fi
entries='(^|/)(tdata|plugins|plugin-settings)(/|$)|(^|/)log\.txt$'

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failed=0

scan() {
	local file=$1 label=$2 hits
	hits=$(strings -a "$file" | grep -E "$pattern" || true)
	if [ -n "$hits" ]; then
		echo "dirty: $label" >&2
		echo "$hits" | head -n 20 | sed 's/^/  /' >&2
		failed=1
	fi
}

for target in "$@"; do
	if [ ! -f "$target" ]; then
		echo "missing: $target" >&2
		failed=1
		continue
	fi
	case "$target" in
	*.tar|*.tar.*)
		bad=$(tar -tf "$target" | grep -E "$entries" || true)
		if [ -n "$bad" ]; then
			echo "dirty: $target contains profile files:" >&2
			echo "$bad" | sed 's/^/  /' >&2
			failed=1
		fi
		dir="$tmp/$(basename "$target")"
		mkdir -p "$dir"
		tar -xf "$target" -C "$dir"
		while IFS= read -r -d '' file; do
			scan "$file" "$target:${file#"$dir"/}"
		done < <(find "$dir" -type f -print0)
		;;
	*)
		scan "$target" "$target"
		;;
	esac
done

if [ "$failed" -ne 0 ]; then
	echo "release is not clean" >&2
	exit 1
fi
echo "clean: $*"
