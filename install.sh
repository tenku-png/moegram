#!/bin/sh
# moegram installer for Arch Linux and Arch-based distros.
#
#   curl -fsSL https://raw.githubusercontent.com/tenku-png/moegram/main/install.sh | sh
#
# Builds the moegram-bin package from the release binary with makepkg and
# installs it with pacman, so dependencies and removal are handled by pacman.
# Remove with: sudo pacman -R moegram-bin
set -eu

repo="tenku-png/moegram"
pkgbuild="https://raw.githubusercontent.com/$repo/main/packaging/aur/moegram-bin/PKGBUILD"

fail() {
	printf 'moegram: %s\n' "$1" >&2
	exit 1
}

[ "$(id -u)" -ne 0 ] || fail "run this as a normal user, not root (sudo is asked when needed)"
[ "$(uname -m)" = "x86_64" ] || fail "only x86_64 builds are available for now"

distro=""
if [ -r /etc/os-release ]; then
	. /etc/os-release
	distro="${ID:-} ${ID_LIKE:-}"
fi
case " $distro " in
	*" arch "*) ;;
	*) fail "this build only works on Arch Linux and Arch-based distros (EndeavourOS, CachyOS, Manjaro...). See README for other options." ;;
esac

for tool in curl makepkg pacman; do
	command -v "$tool" >/dev/null 2>&1 || fail "$tool is missing (sudo pacman -S --needed base-devel curl)"
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT INT TERM

printf 'moegram: downloading package recipe...\n'
curl -fsSL "$pkgbuild" -o "$work/PKGBUILD" || fail "could not download PKGBUILD"

cd "$work"
printf 'moegram: building and installing (pacman will ask for your password)...\n'
# stdin may be the curl pipe, so prompts read from the terminal.
makepkg -si --needed </dev/tty

printf '\nmoegram is installed. Launch it from your app menu or run: moegram\n'
