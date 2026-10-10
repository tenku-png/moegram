# MoeGram

MoeGram is a custom Telegram Desktop client with support for third-party
Python plugins and capability control.

MoeGram is based on [ayugram-desktop](https://github.com/AyuGram/AyuGramDesktop)
(which is based on [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)).

## Build

```sh
cmake -B out -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DTDESKTOP_API_ID=<your_api_id> -DTDESKTOP_API_HASH=<your_api_hash>
cmake --build out
```

See [DEVELOPMENT.md](DEVELOPMENT.md) for details.

## License

MoeGram is licensed under [GPL-3.0](LICENSE), same as its upstream projects.
