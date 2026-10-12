# Writing moegram plugins

How to write a plugin for moegram. This covers what the engine supports
today: engine 0.10.0, plugin API 1, SDK 0.9.0.

If something is not listed here, it is not supported. Don't guess APIs from
ExteraGram (Android) docs: only the subset below exists on desktop.

## Quick start

A plugin is a single `.py` file:

```python
from base_plugin import BasePlugin

__id__ = "hello_world"
__name__ = "Hello World"
__version__ = "1.0.0"
__author__ = "your name"
__description__ = "Logs a line when it loads"


class HelloWorld(BasePlugin):
    def on_plugin_load(self):
        self.log("Hello World loaded")

    def on_plugin_unload(self):
        self.log("Hello World unloaded")
```

Install it any of these ways:

- Settings → Plugins → **Add plugin**, pick the file.
- Send the file to any chat (e.g. Saved Messages) and click it in moegram.
- Drop it into `~/.local/share/moegram/plugins/` and restart moegram.

The first two copy the file as `<__id__>.py` and load it immediately.

Log output (`self.log`, `self.logger`, tracebacks) goes to
`~/.local/share/moegram/log.txt`. Look there first when a plugin doesn't work.

## File rules

The engine reads the plugin's metadata from the source **before running any of
it**, so the rules are strict:

- Exactly one class that inherits from `BasePlugin` (directly or through your
  own base class). That class is the entrypoint. Zero or two → the plugin is
  rejected with `no_entrypoint`.
- Metadata fields are module-level assignments of **plain string literals**
  (a list literal for `__capabilities__`), each assigned once. No f-strings,
  no variables, no concatenation.
- File names starting with `_` are ignored.

### Metadata

| Field | Required | Notes |
| --- | --- | --- |
| `__id__` | yes | `[a-zA-Z][a-zA-Z0-9_-]{1,31}`. Stable identity: settings, permissions and the on/off state are stored by id. Never change it after release. |
| `__name__` | yes | Shown in the plugin list. |
| `__version__` | no | Default `"1.0"`. |
| `__author__` | no | Shown under the name. |
| `__description__` | no | Shown on the plugin card and in the install box. |
| `__moegram_api_version__` | no | `"1"`. Marks a native moegram plugin. Without it the plugin is treated as legacy API 1 (fine). Any other value → plugin is not loaded. |
| `__moegram_engine__` | no | Engine version range, e.g. `">=0.10,<1"`. Plugin is not loaded if the engine doesn't match. |
| `__capabilities__` | no | Permissions the plugin needs, see below. |
| `__icon__`, `__app_version__`, `__sdk_version__` | no | ExteraGram fields. Accepted, never enforced. |

## Permissions (`__capabilities__`)

| Capability | Allows |
| --- | --- |
| `messages.read` | Receiving messages, edits, deletes, updates, request hooks, outgoing message hook |
| `messages.send` | Replacing the text of an outgoing message (needs `messages.modify` too) |
| `messages.modify` | Cancelling/replacing outgoing messages, cancelling/modifying requests |
| `ui` | Menu items, toasts, settings page, composer effects, pill, timers, palette, chat header, blur, effects |
| `ui.advanced` | `self.ui.find()`: changing named parts of the window |
| `storage` | `get_setting` / `set_setting` |

Two modes:

- **No `__capabilities__`** (legacy): all of them are allowed on first install.
  Simplest choice for personal plugins.
- **Declared `__capabilities__`**: every listed capability starts **denied**.
  The user has to allow them in Settings → Plugins → ⓘ (Permissions), then
  turn the plugin off and on so `on_plugin_load` runs again with access.

Calling an API without its capability raises `PermissionError`. An unknown
capability name makes the plugin invalid.

Permissions are API checks, **not a sandbox**. Plugins run in the client's
Python process and can use files, network, subprocesses and C libraries
directly. Only install plugins you trust.

## Lifecycle

```python
class MyPlugin(BasePlugin):
    def on_plugin_load(self):
        # register hooks, menu items, composer effects here
        ...

    def on_plugin_unload(self):
        # optional cleanup; hooks and menu items are removed automatically
        ...
```

- `on_plugin_load()` runs when the plugin is enabled, installed, or the client
  starts. Register everything here.
- `on_plugin_unload()` runs on disable, removal, reinstall and client exit.
- An exception in any callback is logged and isolated: it never crashes the
  client and doesn't stop other plugins.
- Callbacks run on a single plugin worker thread, not the UI thread. Don't
  block for long: slow hooks delay message sending and other plugins.
  Don't call plugin APIs from your own threads — they raise `PermissionError`.

Instance attributes available: `self.id`, `self.name`, `self.version`,
`self.logger` (a standard `logging.Logger`), `self.log(text)`.

## Messages

### Simple callbacks

```python
from base_plugin import BasePlugin

class Watcher(BasePlugin):
    def on_message(self, message):        # new INCOMING messages only
        ...

    def on_message_edit(self, message):   # edits, both directions
        ...

    def on_message_delete(self, message): # deletes, both directions
        ...
```

Needs `messages.read`. No registration needed: just define the methods.

`message` is a read-only `Message`:

| Field | Type | Notes |
| --- | --- | --- |
| `account_id` | int | Your Telegram user id for the account that received it |
| `message_id` | int | |
| `chat_id` | int \| None | Bot API style: user `id`, group `-id`, channel `-100…id` |
| `sender_id` | int \| None | `None` for anonymous channel posts or unknown |
| `text` | str \| None | Plain text, no formatting/entities. `None` if unknown |
| `outgoing` | bool \| None | |

For deletes, the fields are the last copy the client had, and some may be `None`.

ExteraGram-style aliases also work: `message.id`, `message.message`,
`message.messageText`, `message.out`, `message.peer_id`, `message.from_id`,
`getId()`, `getDialogId()`, `isOut()`.

Not available: media, files, entities/formatting, replies, reactions, chat
titles, user names, sending new messages from a plugin.

### Update hooks (ExteraGram style)

Receives both incoming and outgoing messages:

```python
from base_plugin import BasePlugin, HookResult

class Logger(BasePlugin):
    def on_plugin_load(self):
        self.add_hook("TL_updateNewMessage")
        self.add_hook("Channel", match_substring=True)  # any name containing "Channel"

    def on_update_hook(self, update_name, account, update):
        if "Delete" in update_name:
            self.log(f"deleted {update.messages}")
        else:
            m = update.message
            self.log(f"{update_name}: {m.chat_id} {m.text!r}")
        return HookResult()
```

Supported names: `TL_updateNewMessage`, `TL_updateNewChannelMessage`,
`TL_updateEditMessage`, `TL_updateEditChannelMessage`,
`TL_updateDeleteMessages`, `TL_updateDeleteChannelMessages`.

- `add_hook(name, match_substring=False, priority=0)`, `remove_hook(name)`.
  Higher priority runs first.
- New/edit updates: `update.message` (a `Message`). Delete updates:
  `update.messages` (tuple of ids); channel deletes also have `update.channel_id`.
- `update.message_snapshots`: tuple of `Message` for all cases.
- Update hooks are **observe-only**. Return `HookResult()` or `None`.
  `CANCEL`/`MODIFY` are ignored here.

## Changing outgoing messages

```python
from base_plugin import BasePlugin, HookResult, HookStrategy

__id__ = "shout"
__name__ = "Shout"

class Shout(BasePlugin):
    def on_plugin_load(self):
        self.add_on_send_message_hook()   # required, defining the method is not enough

    def on_send_message_hook(self, account, params):
        text = params.message
        if not isinstance(text, str) or not text.startswith(".shout "):
            return HookResult()                       # leave as is
        params.message = text[len(".shout "):].upper()
        return HookResult(strategy=HookStrategy.MODIFY, params=params)
```

- Registration needs `messages.read`. Replacing text needs `messages.send` and
  `messages.modify`. Cancelling needs `messages.modify`.
- `params.message` is the outgoing text. It must stay a `str`.
- Return values:
  - `HookResult()` / `None`: no change.
  - `HookStrategy.MODIFY` with `params=params`: send the new text, next plugins still run.
  - `HookStrategy.MODIFY_FINAL`: send the new text, skip remaining plugins.
  - `HookStrategy.CANCEL`: don't send anything.
- `add_on_send_message_hook(priority=0)`: higher priority runs first.
- Only text messages pass through this hook. If a plugin fails or lacks
  permission, the original message is sent unchanged.

## Request hooks

Only two Telegram requests can be intercepted:

| Request | DTO | Allowed |
| --- | --- | --- |
| `TL_messages_setTyping` | no fields | `CANCEL` (hide "typing…") |
| `TL_account_updateStatus` | `request.offline: bool` | `CANCEL`, or `MODIFY` `offline` |

```python
from base_plugin import BasePlugin, HookResult, HookStrategy

class Ghost(BasePlugin):
    def on_plugin_load(self):
        self.add_hook("TL_messages_setTyping")
        self.add_hook("TL_account_updateStatus")

    def pre_request_hook(self, request_name, account, request):
        if request_name == "TL_messages_setTyping":
            return HookResult(strategy=HookStrategy.CANCEL)
        if request_name == "TL_account_updateStatus":
            request.offline = True
            return HookResult(strategy=HookStrategy.MODIFY, request=request)
        return HookResult()

    def post_request_hook(self, request_name, account, response, error):
        # response: BoolResponse(value) or None; error: RpcError(code, text) or None
        return HookResult()
```

`post_request_hook` is observe-only and isn't called for cancelled requests.
Other request names can be registered but never fire.

## Settings page

Return a list from `create_settings()`. It appears under Settings → Plugins →
⚙ for your plugin. Needs `ui` and `storage`.

```python
from base_plugin import BasePlugin
from ui.settings import Header, Divider, Text, Switch, Input, Selector

class Configurable(BasePlugin):
    def create_settings(self):
        return [
            Header("General"),
            Switch("enabled", "Enable feature", True,
                   subtext="Turns the main feature on", on_change=self.changed),
            Input("prefix", "Command prefix", ".", subtext="Used before commands"),
            Selector("mode", "Mode", 0, ["Soft", "Normal", "Strong"],
                     on_change=self.changed),
            Divider("Text under a divider"),
            Text("About", subtext="Version 1.0"),
        ]

    def changed(self, value):
        self.log(f"new value: {value!r}")
```

| Item | Signature | Stored value |
| --- | --- | --- |
| `Header` | `Header(text)` | — |
| `Divider` | `Divider(text=None)` | — |
| `Text` | `Text(text, subtext=None)` | — (click callbacks are not supported) |
| `Switch` | `Switch(key, text, default: bool, subtext=None, on_change=None)` | `bool` |
| `Input` | `Input(key, text, default: str = "", subtext=None, on_change=None)` | `str` |
| `Selector` | `Selector(key, text, default: int, items: list[str], on_change=None)` | `int` index (1–64 items) |

- Values are saved automatically under `key`; read them with
  `self.get_setting(key, default)`.
- `on_change(value)` is called after the new value is saved. Inputs save when
  the user presses Enter or leaves the field.
- Keys must be unique. `icon`, `on_long_click`, `link_alias` are accepted and
  ignored.

### Storing your own data

```python
count = self.get_setting("count", 0)
self.set_setting("count", count + 1)
```

Values must be JSON-serialisable. Stored per plugin id, kept when the plugin
is removed. `set_setting(key, value, reload_settings=True)` inside an
`on_change` callback redraws the settings page (useful when one option shows
or hides others).

## Message context menu

```python
from base_plugin import BasePlugin, MenuItemData, MenuItemType

class MessageInfo(BasePlugin):
    def on_plugin_load(self):
        self.add_menu_item(MenuItemData(
            menu_type=MenuItemType.MESSAGE_CONTEXT_MENU,
            text="Show message info",
            on_click=self.show_info,
            priority=0,
        ))

    def show_info(self, context):
        m = context["message"]          # Message
        self.show_toast(f"id {m.message_id} in chat {m.chat_id}")
```

- Needs `ui` (and `messages.read` for clicks).
- `context` keys: `account`, `dialog_id`, `message`.
- Register in `on_plugin_load`. Higher `priority` is shown first.
  `icon`, `subtext`, `condition` are ignored.

### Main menu and chat menu

The same `add_menu_item` also adds items to the main (drawer) menu and to
the menu of a chat (right click in the chat list, ⋮ in the chat header):

```python
self.add_menu_item(MenuItemData(
    menu_type=MenuItemType.DRAWER_MENU,        # MAIN_MENU works too
    text="Clean up",
    on_click=lambda ctx: self.show_toast("clean!"),
))
self.add_menu_item(MenuItemData(
    menu_type=MenuItemType.CHAT_ACTION_MENU,
    text="Chat ID",
    on_click=lambda ctx: self.show_toast(str(ctx["chat_id"])),
))
```

- Needs `ui` only.
- Drawer `context`: `account`. Chat `context`: `account`, `chat_id`
  (`dialog_id` is the same value).
- At most 16 items per menu per plugin. `self.remove_menu_item(item_id)`
  removes one. `PROFILE_ACTION_MENU` is not supported.

## Toasts

```python
self.show_toast("Done!")
```

Plain text only. Needs `ui`.

## Composer effects

Animations in the message input field. Needs `ui`. Call from
`on_plugin_load` or a settings callback; pass `None` to turn them off.

```python
from base_plugin import BasePlugin
from moegram.composer import ComposerEffects

class Effects(BasePlugin):
    def on_plugin_load(self):
        self.set_composer_effects(ComposerEffects(
            insertion="pop",      # none | pop | drop | zoom | spin
            deletion="dust",      # none | scatter | dust
            caret="smooth",       # none | smooth
            send="fade_out",      # none | fade_out
            edit="soft",          # none | soft
            speed=1.0,            # 0.5 – 2.0
            intensity=1.0,        # 0.25 – 2.0
            style="pop",          # soft | pop | bouncy
            curve="auto",         # auto | smooth | linear | cubic | back | elastic | bounce
        ))
```

Invalid values raise `ValueError`. Rendering is native; the plugin only
describes the configuration.

## Pill and timers

A small label on the right of the chat list search field.

```python
self.set_pill("RAM 41%", on_click=self.clicked)   # on_click is optional
self.set_pill(None)                               # remove

timer = self.set_interval(2, self.tick)   # seconds, 0.5 – 3600
self.clear_interval(timer)
```

- Needs `ui`. Text is trimmed to 48 characters.
- A timer never stacks: if your callback is slow, ticks are skipped.
- At most 8 timers per plugin. Pill and timers are removed on unload.

## Palette

Override theme colors on top of the current theme:

```python
self.set_palette({
    "windowBg": "#101014",
    "windowFg": "#e8e8f0",
    "historyComposeAreaBg": "#16161cee",   # #RRGGBBAA works too
})
self.set_palette(None)   # remove your colors
```

- Needs `ui`. Keys are tdesktop palette names (see any `.tdesktop-theme`
  file, or Settings → Chat settings → Edit theme). Unknown keys are ignored.
- Every plugin is a layer; plugins are applied in id order, later layers win.
- Switching the theme keeps your colors on top. Saving/exporting a theme
  never includes plugin colors. Everything is reverted on unload.

## Chat header

Badges, buttons and a status line in the header of the open chat:

```python
self.set_chat_header({
    "subtitle": "focus mode",                   # replaces "last seen…"
    "badges": [{"text": "PRO", "color": "#ffffff", "background": "#7a5cff"}],
    "buttons": [{"text": "Mute", "on_click": self.mute}],
    "background": "#7a5cff22",                  # tint over the header
})
self.set_chat_header(None)
```

- Needs `ui`. At most 6 badges + buttons in total, 24 characters each.
- `on_click(args)` gets `{"text": ...}` (or call it without arguments).

## Blur

Frosted glass for menus and bars:

```python
self.set_blur("menu", radius=16, opacity=0.75, tint="#00000022")
self.set_blur("chat_header", radius=24, opacity=0.6)
self.clear_blur("menu")
```

| Target | What is blurred |
| --- | --- |
| `menu` | Every popup menu: the window under it shows through. |
| `chat_header` | Chat header shows the messages next to it, blurred. |
| `composer` | Message input area, same idea. |
| `dialogs_header` | Search bar above the chat list. |

- Needs `ui`. `radius` 1 – 64, `opacity` 0 – 1 (opacity of the normal
  background over the blur), `tint` optional `#RRGGBB(AA)`.
- Blur is computed on a ¼ size copy and refreshed at most ~30 times a
  second, so it's cheap. The bars don't move: messages don't actually
  scroll under the header, the header shows a blurred copy of the nearest
  strip of the list.
- Menu blur only sees moegram's own windows (Wayland doesn't allow reading
  other apps). Menus created before you turned blur on stay opaque.

## Effects

```python
self.set_effect("scroll_motion_blur", {"strength": 1.0, "threshold": 1.5})
self.set_effect("scroll_motion_blur", None)
```

`scroll_motion_blur` smears the chat and the chat list while you scroll
fast. `strength` 0 – 4, `threshold` is the speed (px/ms) where it starts.
Needs `ui`.

## Window parts (`self.ui`)

Needs `ui.advanced`. Changes named parts of the window. Python never gets
the widget itself, only a handle with a name; the client applies changes.

```python
bar = self.ui.find("chat.top_bar")
bar.set_opacity(0.9)
bar.set_visible(False)            # invisible and click-through, keeps its space
bar.set_background("#20202a")     # painted under the widget
oid = bar.add_overlay({
    "text": "hi", "rect": (8, 4, 120, 24),   # px from the top left; w/h <= 0 = to the edge
    "align": "left",                           # left/right/center + top/bottom
    "color": "#ffffff", "background": "#00000066",
    "rounded": 8, "font_size": 12,
    "image": "/path/to/icon.png",            # local file, ≤ 4 MB
    "on_click": self.clicked,                 # args: anchor, overlay, x, y
})
bar.remove_overlay(oid)
bar.on_event("click", self.clicked)          # click | resize | enter | leave
bar.reset()
```

| Name | Part |
| --- | --- |
| `chat.top_bar` | Header of the open chat |
| `chat.list` | Message list |
| `compose.field` | Message input field |
| `dialogs.header` | Search bar area above the chat list |
| `dialogs.search` | The search field |
| `dialogs.list` | Chat list |

- `on_event` callbacks get `{"anchor", "event", ...}`: clicks add `x`, `y`;
  resize adds `width`, `height` (sent at most every 100 ms). Events are
  only observed, the client still handles the click.
- `set_background` only shows where the widget itself is transparent.
- At most 16 overlays per part. Everything is removed on unload.

## What is NOT available

So a plugin doesn't try to use things that don't exist:

- Sending new messages, replying, forwarding, reacting.
- Media, files, stickers, formatting entities.
- Chat/user info (names, titles, avatars), contacts, dialogs list.
- Arbitrary Telegram API requests (only the two request hooks above).
- Arbitrary Qt widgets, windows or dialogs; raw access to window parts
  (only the descriptions above).
- The profile menu (`PROFILE_ACTION_MENU`).
- Android/ExteraGram modules: `android_utils`, `client_utils`, `hook_utils`,
  `java`, `org.telegram.*`, `markdown_utils`, Xposed-style method hooks.

A plugin that only needs pure Python (requests, json, re, datetime…) can
use the standard library and any module installed in the system Python.

## Complete example

A `.calc` command that replaces `".calc 2+2*3"` with `"2+2*3 = 8"`, with a
settings toggle:

```python
import ast
import operator

from base_plugin import BasePlugin, HookResult, HookStrategy
from ui.settings import Header, Switch

__id__ = "calc"
__name__ = "Calc"
__version__ = "1.0.0"
__author__ = "you"
__description__ = "Type .calc 2+2 to send the result"
__moegram_api_version__ = "1"

OPS = {
    ast.Add: operator.add, ast.Sub: operator.sub,
    ast.Mult: operator.mul, ast.Div: operator.truediv,
    ast.Pow: operator.pow, ast.USub: operator.neg,
}


def evaluate(node):
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
        return node.value
    if isinstance(node, ast.BinOp) and type(node.op) in OPS:
        return OPS[type(node.op)](evaluate(node.left), evaluate(node.right))
    if isinstance(node, ast.UnaryOp) and type(node.op) in OPS:
        return OPS[type(node.op)](evaluate(node.operand))
    raise ValueError("unsupported expression")


class Calc(BasePlugin):
    def on_plugin_load(self):
        self.add_on_send_message_hook()

    def create_settings(self):
        return [
            Header("Calc"),
            Switch("show_expr", "Keep the expression", True),
        ]

    def on_send_message_hook(self, account, params):
        text = params.message
        if not isinstance(text, str) or not text.startswith(".calc "):
            return HookResult()
        expr = text[len(".calc "):].strip()
        try:
            result = evaluate(ast.parse(expr, mode="eval").body)
        except Exception:
            self.show_toast("Calc: can't evaluate that")
            return HookResult(strategy=HookStrategy.CANCEL)
        keep = self.get_setting("show_expr", True)
        params.message = f"{expr} = {result}" if keep else str(result)
        return HookResult(strategy=HookStrategy.MODIFY, params=params)
```

## Checklist before sharing a plugin

- `__id__` and `__name__` are set and `__id__` won't change later.
- Exactly one `BasePlugin` subclass.
- Hooks are registered in `on_plugin_load` (`add_hook`,
  `add_on_send_message_hook`, `add_menu_item`).
- Hooks return `HookResult()` when they don't change anything.
- `params.message` / `message.text` may be `None`; check before using.
- No long blocking work inside hooks.
- Test with a separate profile: `moegram -workdir ~/moegram-test`.
