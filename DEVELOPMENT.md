# Development

moegram is AyuGram Desktop (itself a Telegram Desktop fork) plus an embedded
CPython plugin engine. Builds and day-to-day work happen on Arch Linux.

## Repository layout

- `Telegram/SourceFiles/` — client code (Telegram Desktop + AyuGram + moegram).
- `Telegram/SourceFiles/plugins/` — glue between the client and the plugin engine.
- `Telegram/SourceFiles/settings/sections/settings_moegram_plugins.*` — Plugins tab.
- `Telegram/ThirdParty/moegram-plugin-engine/` — the engine (separate repo, submodule).
- `plugins/` — example plugins.
- `docs/PLUGINS.md` — plugin author guide.
- `packaging/` — release packaging (`moegram-bin` PKGBUILD).

## Remotes

```sh
git clone --recurse-submodules https://github.com/tenku-png/moegram.git
cd moegram
git remote add upstream https://github.com/AyuGram/AyuGramDesktop.git
```

`main` has its own squashed history. Never merge `upstream/dev` into it and
never push upstream branches or tags to `origin`.

## Build (Arch Linux)

Packages:

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf python gperf \
  boost boost-libs glib2-devel gobject-introspection qt6-shadertools \
  libtg_owt microsoft-gsl range-v3 tl-expected vulkan-headers
```

Runtime libraries are the same as the `depends` list in
`packaging/aur/moegram-bin/PKGBUILD`. Python 3.11+ is required; the binary is
tied to the Python minor version it was built with.

`tde2e` from TDLib (commit taken from the AyuGram PKGBUILD), built into
`out/arch/deps/tdlib-install` with path maps so no local paths end up in the
binary:

```sh
scripts/build_tde2e.sh
```

Client:

```sh
git submodule update --init --recursive
cmake -S . -B out/arch-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DDESKTOP_APP_USE_PACKAGED=ON \
  -DDESKTOP_APP_DISABLE_AUTOUPDATE=ON \
  -DTDESKTOP_API_TEST=ON \
  -Dtde2e_DIR="$PWD/out/arch/deps/tdlib-install/lib/cmake/tde2e"
cmake --build out/arch-debug --target Telegram
```

`TDESKTOP_API_TEST=ON` uses the public test keys. To use your own, pass
`-DTDESKTOP_API_ID=...` and `-DTDESKTOP_API_HASH=...` instead and don't commit them.

The Debug link needs a lot of RAM (~20 GB). Close heavy apps or lower `--parallel`.

Run with a separate profile so your main account stays untouched:

```sh
out/arch-debug/bin/moegram -workdir ~/MoeGramTest
```

## Tests

Engine tests (fast, no client build needed):

```sh
cmake -S Telegram/ThirdParty/moegram-plugin-engine -B out/engine -G Ninja
cmake --build out/engine
ctest --test-dir out/engine --output-on-failure
```

Client-side plugin smoke tests: configure with `-DMOEGRAM_PLUGIN_TESTS=ON`, then
build and run the `moegram_*_smoke` targets in
`out/arch-debug/Telegram/SourceFiles/plugins/`.

## Release

```sh
git tag vX.Y.Z && git push origin vX.Y.Z
scripts/release.sh X.Y.Z notes.md
```

The script configures `out/arch-release`, builds, stages, strips and packs
`out/moegram-vX.Y.Z-x86_64.tar.zst`, then runs `scripts/check_release_clean.sh`
on the binary and the archive and only then calls `gh release create` (the
sha256 is appended to the notes). `--build-only X.Y.Z` stops before publishing.

Release builds map the source dir to `.` and `$HOME` to `~`
(`-ffile-prefix-map` in the top `CMakeLists.txt`), so `__FILE__` and debug
info don't carry local paths. The check fails on `/home/`, the author's mail,
`/$USER/`, `~$USER`, `$USER@` and on `tdata`, `plugins`, `plugin-settings` or
`log.txt` in the archive.

Then update `pkgver`, `_tag` and `sha256sums` in the PKGBUILD.

## Updating AyuGram

The AyuGram commit moegram is currently based on:
`d4b7546e77f93ac4c9bb53a3fcf7e9ffbbecf1b5` (7.2.9).

Updates are applied as a patch between the old and new upstream commits, so
upstream history never enters `main`:

```sh
git fetch --no-tags upstream dev
base=d4b7546e77f93ac4c9bb53a3fcf7e9ffbbecf1b5
new=$(git rev-parse upstream/dev)
git switch -c update-ayugram main
git diff --binary --full-index --no-renames --submodule=short "$base" "$new" -- . > /tmp/ayugram.patch
git apply --3way --index /tmp/ayugram.patch
git submodule update --init --recursive
```

Resolve conflicts, build, test, then update the base commit above in the same
commit as the update. Don't use `git submodule update --remote`.
