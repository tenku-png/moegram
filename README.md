# MoeGram

MoeGram is a custom Telegram Desktop client with support for third-party
Python plugins and capability control.

MoeGram is based on [ayugram-desktop](https://github.com/AyuGram/AyuGramDesktop)
(which is based on [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)).

## Install

### Arch Linux and Arch-based (EndeavourOS, CachyOS, Manjaro...)

```sh
curl -fsSL https://raw.githubusercontent.com/tenku-png/moegram/main/install.sh | sh
```

The script downloads the latest release, builds a `moegram-bin` package with
`makepkg` and installs it with `pacman`, so all dependencies are pulled in
automatically. Run it again to update. Remove with:

```sh
sudo pacman -R moegram-bin
```

The release binary is built on current Arch and needs Python 3.14. If your
distro ships an older Python or Qt (Manjaro stable sometimes lags behind),
build from source instead.

Your account data lives in `~/.local/share/moegram`. Plugins go to
`~/.local/share/moegram/plugins`, or use Settings → Plugins → Add plugin.

### Other distros

There are no prebuilt packages yet. The Arch binary will not run on Debian,
Ubuntu, Fedora and others because their system libraries differ. Build from
source using the steps below.

## Build

```sh
cmake -B out -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DTDESKTOP_API_ID=<your_api_id> -DTDESKTOP_API_HASH=<your_api_hash>
cmake --build out
```

See [DEVELOPMENT.md](DEVELOPMENT.md) for details.

## License

MoeGram is licensed under [GPL-3.0](LICENSE), same as its upstream projects.
