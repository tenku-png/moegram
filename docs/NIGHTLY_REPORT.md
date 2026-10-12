# Nightly report: plugin API v2 (moegram 1.1.0)

Night of 12.10.26. Branch `feature/plugin-api-v2` in moegram and in
moegram-plugin-engine, merged into `main`, tagged `v1.1.0`.

Release: https://github.com/tenku-png/moegram/releases/tag/v1.1.0
Archive: `moegram-v1.1.0-x86_64.tar.zst`,
sha256 `36130b684ec8ff93e057da5d2dc08c5064a6c1e40a4cb66b536a37154fe07c37`.

## Done

All six items from the task are done.

| # | Feature | Engine commit | Client commit |
| --- | --- | --- | --- |
| 1 | `set_effect("particles", …)` | 917940c | 545537dc27 |
| 2 | `ui.settings.Slider`, `ui.settings.ColorPicker` | f68931f | cdb8a60ca6 |
| 3 | `on_app_active` / `on_app_inactive`, `on_chat_opened`, `current_chat_id()` | d15c6b7 | 6964d73ef1 |
| 4 | GIF/WebP overlays, `"draggable": True`, `update_overlay()` | 320fef7 | b4daf8931d |
| 5 | `messages.send_media`, `reply_to`/`chat_id` in `SendParams`, `get_media`, `send_photo` / `send_file` / `send_video_note` | 54d02d5 | e426720e6b, 825605d98f |
| 6 | Native "Send as video message" in the send files box | — | f1b97f4a03, e7bc91acda |

Other commits: engine `d20a92d` (engine 0.11.0), moegram `392a44da0b`
(submodule), `5e8eebcbd1` (docs/PLUGINS.md), `865653f257` (changelog),
`141f595b5c` (PKGBUILD 1.1.0).

## Decisions

- Particles (`moegram/ui/plugin_particles.cpp`): one transparent,
  mouse-transparent child per area (main window body, `HistoryWidget`,
  `Dialogs::Widget`), `Ui::Animations::Basic`, everything drawn with QPainter
  paths/text. Stops when the layer is not visible or the window is minimized,
  restarts on show / window state change. Optional `color` field added (also
  used by the settings color picker). If two plugins use the same area, the
  later plugin id wins (same rule as other single-value effects).
- `set_effect("particles", {})` = defaults (snow, 80, window), `None` = off,
  same as `scroll_motion_blur`. Added `clear_effect(name)`.
- Slider value is `int` when default/min/max/step are all ints, else float
  rounded to the step. `Host::SettingValue` got a `double` alternative.
  ColorPicker stores upper-case `#RRGGBB`, no alpha. UI: settings slider
  (`MakeSliderWithLabel`) and the stock `ColorEditor` box.
- App/chat events are overridable methods (`on_app_active`, …), like
  `on_message`, not a subscription API. `app_*` need `ui`, chat ones need
  `messages.read` (a chat id is message metadata). `current_chat_id()` is
  global "last opened in any window"; `None` when no chat is open.
- Draggable overlays: clicks and drags now go to small invisible hit widgets
  (children of the anchor) instead of the anchor's event filter, because the
  anchor never sees presses that land on its children. After a drag the
  runtime only remembers the position (no republish, so an animation does
  not restart). With `storage` the position is saved as
  `_overlay_pos.<anchor>.<key or id>` and restored by `add_overlay`.
- Media: `get_media` needs `messages.read`, sending needs the new
  `messages.send_media`. Existing grant records don't get it automatically
  (default deny); new legacy plugins get it like every other capability.
  All media work goes to the account of the active window. Photos are saved
  as JPEG into `$TMP/moegram-plugin-media/`, documents keep their name there;
  download timeout 10 minutes, files over 512 MB are refused.
  `send_video_note` expects a ready square mp4 (no conversion in the plugin
  path) and uses `ApiWrap::sendVoiceMessage(..., video = true)`, i.e.
  `SendMediaType::Round`. `reply_to` accepts a message id or
  `(chat_id, message_id)`.
- Round video (item 6): the camera recorder's encoder mirrors frames and
  draws a logo, so a separate converter was written
  (`moegram/media/round_video_converter.cpp`): FFmpeg decode → rotation from
  metadata → crop → 400×400 → H.264 (openh264 or any H.264 encoder) + AAC
  mono 48 kHz, ≤ 60 s, ~30 fps, faststart mp4. The box shows the first frame
  with a draggable circle and Size / Start / Length sliders; conversion runs
  on `crl::async`, then the file box closes and a toast says it's preparing.
  The menu item appears on right click of Send only for a single video when
  the chat allows video messages; the normal send menu items are kept below.
- Fixed on the way: in the plugin info box `ui.advanced` was labelled
  "Plugin storage"; it is "Window parts" now, plus "Send media".
- Engine version bumped to 0.11.0 (new APIs), plugin API stays 1.

## Problems met

- The 1.0.1 binary contained 869 strings with `$HOME/...` (from
  `__FILE__`) and paths of an old `~/.codex/...` worktree from the
  prebuilt `tde2e`. Fixed by rebuilding `tde2e` with
  `-ffile-prefix-map` into `out/arch/deps/tdlib-clean-install` and
  configuring `out/arch-release` with `-ffile-prefix-map=$PWD=.`. The 1.1.0
  binary and archive have no `$HOME` paths or mail strings; the only
  `tdata` hits are generic strings in the client code (`tdata/config`, …).
  Keep these flags for future releases (they are build-dir options, not
  committed).
- First Release link failed with undefined `AttachParticles`: the build was
  started before the new files were added to CMake. A second incremental run
  fixed it. No build or test failures in the new code otherwise, nothing was
  rolled back.
- A GIL bug in my first version of the app event dispatch (Python objects
  released after `PyGILState_Release`) crashed the new test; fixed before
  commit.
- Tests with declared `__capabilities__` start denied, so the app-events
  test grants `ui` through `SetPluginPermission`.

## Not done / limits

- No live video preview in the round video box (first frame only), and the
  Start slider has no thumbnails.
- `Message` still has no media type field (`get_media` callback gets only a
  path). Easy to add when a plugin needs it.
- Overlay physics (throw/bounce), send-menu items for plugins
  (`SEND_MENU`), avatar tint, story rings and server profile colors from
  `moegram-native-features.md` were not part of tonight's list.
- No real-account test: media send/get and round video sending were not
  tried against Telegram (the run was with a clean profile, no login, as
  requested). The converter itself was tested on a generated 75 s 720p
  video: output 400×400 H.264 + AAC mono 48 kHz, exactly 60 s, crop correct.

## How to check

Engine tests (25 s):

```sh
cmake -S Telegram/ThirdParty/moegram-plugin-engine -B out/engine -G Ninja
cmake --build out/engine
LANG=C.UTF-8 LC_ALL=C.UTF-8 ctest --test-dir out/engine --output-on-failure
```

23/23 pass. New: `engine_particles`, `engine_settings`, `engine_app_events`,
`engine_overlay`, `engine_media`; every check_*.py got stubs for the new
natives (`_current_chat_id`, `_request_media`, `_publish_media`). All of
them use a `QTemporaryDir` with `plugins/` inside, so `plugin-settings/`
stays in the temp dir.

Client, clean profile (what was done tonight):

```sh
W=$(mktemp -d); mkdir $W/plugins
cp <test plugins> $W/plugins/
moegram -workdir $W
grep -E "Plugin Host|v2:" $W/log.txt
```

Result: engine started (CPython 3.14.7), 4 test plugins loaded, particles
drawn over the login screen, `current_chat_id()` = None, `app_active`
delivered, `get_media` answered `None` ("no account"), `send_photo` queued
and refused by the client ("no account").

Per feature, with an account:

1. Particles: plugin with `self.set_effect("particles", {"mode": "sakura"})`;
   try `area` chat/dialogs; minimize the window, CPU should drop to idle.
2. Settings: plugin returning `Slider("count", "Count", 50, 10, 400, 10)` and
   `ColorPicker("c", "Color")`; Settings → Plugins → ⚙. Value lands in
   `plugin-settings/<id>.json`.
3. Events: log in `on_app_active`, `on_chat_opened`; switch windows/chats.
4. Overlays: `self.ui.find("dialogs.header").add_overlay({"image": "x.gif",
   "rect": (4, 4, 32, 32), "draggable": True})`, drag it, restart: position
   is kept (needs `storage`).
5. Media: message menu item that calls `get_media` then `send_photo(...,
   reply_to=message.message_id)`; `on_send_message_hook` prints
   `params.reply_to` when replying. Grant "Send media" for plugins with
   declared capabilities.
6. Attach one video in a chat → right click Send → "Send as video message" →
   move the circle, pick start/length → Send.
