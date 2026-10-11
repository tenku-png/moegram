# Development

moegram is AyuGram Desktop (itself a Telegram Desktop fork) plus an embedded
CPython plugin engine. Builds and day-to-day work happen on Arch Linux.

## Repository layout

- `Telegram/SourceFiles/` — client code (Telegram Desktop + AyuGram + moegram).
- `Telegram/SourceFiles/plugins/` — glue between the client and the plugin engine.
- `Telegram/SourceFiles/settings/sections/settings_moegram_plugins.*` — Plugins tab.
- `Telegram/ThirdParty/moegram-plugin-engine/` — the engine (separate repo, submodule).
- `plugins/` — example plugins.
- `docs/` — plugin API notes.
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

`tde2e` from TDLib (commit taken from the AyuGram PKGBUILD):

```sh
git clone https://github.com/tdlib/td.git out/arch/deps/tdlib
git -C out/arch/deps/tdlib checkout 022d60202e446ad1287b9fb68e687c8a0760788b
cmake -S out/arch/deps/tdlib -B out/arch/deps/tdlib-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DTD_E2E_ONLY=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/out/arch/deps/tdlib-install"
cmake --build out/arch/deps/tdlib-build
cmake --install out/arch/deps/tdlib-build
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
cmake -S . -B out/arch-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DDESKTOP_APP_USE_PACKAGED=ON \
  -DDESKTOP_APP_DISABLE_AUTOUPDATE=ON -DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON \
  -DTDESKTOP_API_TEST=ON \
  -Dtde2e_DIR="$PWD/out/arch/deps/tdlib-install/lib/cmake/tde2e"
cmake --build out/arch-release --target Telegram
DESTDIR="$PWD/out/release-stage" cmake --install out/arch-release
strip --strip-unneeded out/release-stage/usr/bin/moegram
tar -C out/release-stage -c --zstd -f moegram-vX.Y.Z-x86_64.tar.zst usr
```

Upload the archive to a GitHub release (`gh release create ... -R tenku-png/moegram`),
then update `pkgver`, `_tag` and `sha256sums` in the PKGBUILD.

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
