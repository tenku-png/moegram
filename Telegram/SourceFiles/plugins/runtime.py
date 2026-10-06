import ast
import logging
import math
import json
import os
from dataclasses import dataclass
from collections import deque
from enum import Enum
from pathlib import Path
import re
import sys
import traceback
import types


class _LogHandler(logging.Handler):
    def emit(self, record):
        _write_log(self.format(record))


SDK_VERSION = "0.8.0"
API_VERSION = 1


@dataclass(frozen=True, slots=True)
class ComposerEffects:
    insertion: str = "none"
    deletion: str = "none"
    caret: str = "none"
    send: str = "none"
    edit: str = "none"
    speed: float = 1.0
    intensity: float = 1.0
    style: str = "pop"

    def __post_init__(self):
        if self.style not in ("soft", "pop", "bouncy"):
            raise ValueError("Unsupported composer style")
        for field, effect in (("insertion", "pop"), ("deletion", "scatter"),
                              ("caret", "smooth"), ("send", "fade_out"),
                              ("edit", "soft")):
            if getattr(self, field) not in ("none", effect):
                raise ValueError(f"Unsupported {field} effect")
        for field, low, high in (("speed", 0.5, 2.0), ("intensity", 0.25, 2.0)):
            value = getattr(self, field)
            if type(value) not in (int, float) or not math.isfinite(value):
                raise ValueError(f"{field} must be finite")
            if not low <= value <= high:
                raise ValueError(f"{field} must be between {low} and {high}")


_composer_module = types.ModuleType("moegram.composer")
_composer_module.ComposerEffects = ComposerEffects
_composer_module.API_VERSION = 1
_moegram_module = types.ModuleType("moegram")
_moegram_module.__path__ = []
_moegram_module.composer = _composer_module
sys.modules["moegram"] = _moegram_module
sys.modules["moegram.composer"] = _composer_module


@dataclass
class Header:
    text: str


@dataclass
class Divider:
    text: str | None = None


@dataclass
class Input:
    key: str
    text: str
    default: str = ""
    subtext: str | None = None
    icon: str | None = None
    on_change: object = None
    on_long_click: object = None
    link_alias: str | None = None


@dataclass
class Switch:
    key: str
    text: str
    default: bool
    subtext: str | None = None
    icon: str | None = None
    on_change: object = None
    on_long_click: object = None
    link_alias: str | None = None


@dataclass
class Selector:
    key: str
    text: str
    default: int
    items: list[str]
    icon: str | None = None
    on_change: object = None
    on_long_click: object = None
    link_alias: str | None = None


@dataclass
class Text:
    text: str
    subtext: str | None = None
    icon: str | None = None
    accent: bool = False
    red: bool = False
    on_click: object = None
    on_long_click: object = None
    link_alias: str | None = None


_ui_module = types.ModuleType("ui")
_ui_module.__path__ = []
_settings_module = types.ModuleType("ui.settings")
_settings_module.Header = Header
_settings_module.Divider = Divider
_settings_module.Input = Input
_settings_module.Text = Text
_settings_module.Switch = Switch
_settings_module.Selector = Selector
_ui_module.settings = _settings_module
sys.modules["ui"] = _ui_module
sys.modules["ui.settings"] = _settings_module


@dataclass
class SendParams:
    message: str


@dataclass(slots=True)
class TL_messages_setTyping:
    pass


@dataclass(slots=True)
class TL_account_updateStatus:
    offline: bool


@dataclass(frozen=True, slots=True)
class BoolResponse:
    value: bool


@dataclass(frozen=True, slots=True)
class RpcError:
    code: int
    text: str


@dataclass(frozen=True, slots=True)
class Message:
    account_id: int
    message_id: int
    chat_id: int | None
    sender_id: int | None
    text: str | None
    outgoing: bool | None
    api_version: int = API_VERSION

    @property
    def id(self):
        return self.message_id

    @property
    def messageText(self):
        return self.text

    @property
    def message(self):
        return self.text

    @property
    def out(self):
        return self.outgoing

    @property
    def peer_id(self):
        return _peer(self.chat_id)

    @property
    def from_id(self):
        return _peer(self.sender_id)

    def getId(self):
        return self.message_id

    def getDialogId(self):
        return self.chat_id

    def isOut(self):
        return self.outgoing


class HookStrategy(Enum):
    DEFAULT = "default"
    CANCEL = "cancel"
    MODIFY = "modify"
    MODIFY_FINAL = "modify_final"


@dataclass
class HookResult:
    strategy: HookStrategy = HookStrategy.DEFAULT
    request: object = None
    response: object = None
    update: object = None
    updates: object = None
    error: object = None
    params: object = None


@dataclass(frozen=True, slots=True)
class Peer:
    user_id: int = 0
    chat_id: int = 0
    channel_id: int = 0


def _peer(value):
    if value is None:
        return None
    if value <= -1000000000000:
        return Peer(channel_id=-value - 1000000000000)
    if value < 0:
        return Peer(chat_id=-value)
    return Peer(user_id=value)


@dataclass(frozen=True, slots=True)
class Update:
    update_name: str
    account_id: int
    message_snapshots: tuple[Message, ...]
    sequence: int
    pts: int | None = None
    pts_count: int | None = None
    source: str = "model"
    api_version: int = API_VERSION

    @property
    def message(self):
        if "Delete" in self.update_name:
            raise AttributeError("Delete update has messages, not message")
        return self.message_snapshots[0]

    @property
    def messages(self):
        if "Delete" not in self.update_name:
            raise AttributeError("Message update has message, not messages")
        return tuple(message.message_id for message in self.message_snapshots)

    @property
    def channel_id(self):
        if self.update_name != "TL_updateDeleteChannelMessages":
            raise AttributeError("Only channel delete update has channel_id")
        peer = _peer(self.message_snapshots[0].chat_id)
        return peer.channel_id if peer else 0


class MenuItemType(Enum):
    MESSAGE_CONTEXT_MENU = "MESSAGE_CONTEXT_MENU"
    DRAWER_MENU = "DRAWER_MENU"
    MAIN_MENU = "MAIN_MENU"
    CHAT_ACTION_MENU = "CHAT_ACTION_MENU"
    PROFILE_ACTION_MENU = "PROFILE_ACTION_MENU"


@dataclass
class MenuItemData:
    menu_type: MenuItemType
    text: str
    on_click: object
    item_id: str | None = None
    icon: str | None = None
    subtext: str | None = None
    condition: str | None = None
    priority: int = 0


def show_toast(text):
    if not isinstance(text, str):
        raise TypeError("Toast text must be a string")
    text.encode("utf-8")
    import _moegram_runtime
    _moegram_runtime._show_toast(text)


class BasePlugin:
    def __init__(self):
        self.id = ""
        self.name = ""
        self.version = ""
        self._update_hooks = {}
        self._send_priority = None
        self._menu_items = []
        self._composer_active = False
        self._settings = {}
        self._settings_path = None
        self._setting_callbacks = {}
        self._setting_types = {}
        self._settings_reload_requested = False
        self.logger = logging.getLogger("plugin.unbound")

    def log(self, message):
        self.logger.info(message)

    def on_plugin_load(self):
        pass

    def on_plugin_unload(self):
        pass

    def set_composer_effects(self, effects):
        if not self._composer_active:
            raise RuntimeError("Composer effects registration requires a loaded plugin")
        if effects is not None and not isinstance(effects, ComposerEffects):
            raise TypeError("Expected ComposerEffects or None")
        payload = None if effects is None else (
            effects.insertion != "none", effects.deletion != "none",
            effects.caret != "none", effects.send != "none", effects.edit != "none",
            float(effects.speed), float(effects.intensity), effects.style,
        )
        _publish_composer(self.id, payload)

    def create_settings(self):
        return []

    def add_on_send_message_hook(self, priority: int = 0):
        if not isinstance(priority, int):
            raise TypeError("Send hook priority must be an integer")
        self._send_priority = priority

    def on_send_message_hook(self, account: int, params) -> HookResult:
        return HookResult()

    def get_setting(self, key: str, default=None):
        return self._settings.get(key, default)

    def set_setting(self, key: str, value, reload_settings: bool = False):
        if not isinstance(key, str) or not key:
            raise ValueError("Setting key must be a nonempty string")
        if type(reload_settings) is not bool:
            raise TypeError("reload_settings must be a bool")
        settings = dict(self._settings)
        settings[key] = value
        encoded = json.dumps(settings, ensure_ascii=False, allow_nan=False)
        path = self._settings_path
        if path is None:
            raise RuntimeError("Plugin settings storage is unavailable")
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_text(encoded, encoding="utf-8")
        os.replace(temporary, path)
        self._settings = settings
        self._settings_reload_requested |= reload_settings
        return reload_settings

    def add_hook(self, name: str, match_substring: bool = False, priority: int = 0):
        if not isinstance(name, str) or not name:
            raise ValueError("Hook name must be a nonempty string")
        if not isinstance(priority, int):
            raise TypeError("Hook priority must be an integer")
        self._update_hooks[name] = (bool(match_substring), priority)

    def remove_hook(self, name: str):
        self._update_hooks.pop(name, None)

    def pre_request_hook(self, request_name: str, account: int, request) -> HookResult:
        return HookResult()

    def post_request_hook(self, request_name: str, account: int, response, error) -> HookResult:
        return HookResult()

    def on_update_hook(self, update_name: str, account: int, update: Update) -> HookResult:
        return HookResult()

    def on_message(self, message: Message):
        pass

    def on_message_edit(self, message: Message):
        pass

    def on_message_delete(self, message: Message):
        pass

    def add_menu_item(self, item: MenuItemData):
        if not isinstance(item, MenuItemData):
            raise TypeError("Menu item must be MenuItemData")
        if item.menu_type is not MenuItemType.MESSAGE_CONTEXT_MENU:
            raise ValueError("Only MESSAGE_CONTEXT_MENU is supported")
        if not isinstance(item.text, str) or not item.text.strip():
            raise ValueError("Menu item text must be a nonempty string")
        item.text.encode("utf-8")
        if not callable(item.on_click):
            raise TypeError("Menu item on_click must be callable")
        if type(item.priority) is not int:
            raise TypeError("Menu item priority must be an integer")
        item_id = item.item_id
        if item_id is not None:
            if not isinstance(item_id, str) or not item_id:
                raise ValueError("Menu item id must be a nonempty string")
            item_id.encode("utf-8")
        else:
            item_id = f"message_context_{len(self._menu_items) + 1}"
        if any(existing_id == item_id for existing_id, *_ in self._menu_items):
            raise ValueError(f"Duplicate menu item id: {item_id}")
        self._menu_items.append((item_id, item.text, item.priority, item.on_click))
        return item_id

    def show_toast(self, text):
        show_toast(text)


_base_module = types.ModuleType("base_plugin")
_base_module.BasePlugin = BasePlugin
_base_module.SDK_VERSION = SDK_VERSION
_base_module.API_VERSION = API_VERSION
_base_module.Message = Message
_base_module.Update = Update
_base_module.Peer = Peer
_base_module.MenuItemData = MenuItemData
_base_module.MenuItemType = MenuItemType
_base_module.show_toast = show_toast
_base_module.HookResult = HookResult
_base_module.HookStrategy = HookStrategy
sys.modules["base_plugin"] = _base_module

_handler = _LogHandler()
_handler.setFormatter(logging.Formatter("%(name)s [%(levelname)s] %(message)s"))
_logger = logging.getLogger("plugin")
_logger.handlers = [_handler]
_logger.setLevel(logging.DEBUG)
_logger.propagate = False


def _report_failure(context):
    _write_log(f"Plugin Host: {context}\n{traceback.format_exc()}")


def _metadata(tree):
    result = {}
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if isinstance(target, ast.Name) and target.id in (
                "__id__", "__name__", "__version__", "__moegram_api_version__"
            ):
                if target.id in result:
                    raise ValueError(f"Duplicate metadata: {target.id}")
                if not isinstance(node.value, ast.Constant):
                    raise ValueError(f"Metadata must be a literal: {target.id}")
                value = node.value.value
                if not isinstance(value, str) or not value.strip():
                    raise ValueError(f"Metadata must be a nonempty string: {target.id}")
                result[target.id] = value
    plugin_id = result.get("__id__", "")
    if not re.fullmatch(r"[a-zA-Z][a-zA-Z0-9_-]{1,31}", plugin_id):
        raise ValueError("Plugin id must match [a-zA-Z][a-zA-Z0-9_-]{1,31}")
    if "__name__" not in result:
        raise ValueError("Missing __name__ metadata")
    result.setdefault("__version__", "1.0")
    required_api = result.get("__moegram_api_version__", "1")
    if required_api != str(API_VERSION):
        raise ValueError(f"Unsupported MoeGram API version: {required_api}")
    return result


class PluginHost:
    def __init__(self):
        self._loaded = []
        self._ids = set()
        self._plugins = {}
        self._enabled_path = None
        self._enabled_state = {}
        self._manager_state = {"enabled": True, "pinned": []}
        self._sequence = 0
        self._seen = set()
        self._recent = deque()
        self._created = set()
        self._created_recent = deque()
        self._deleted = set()
        self._deleted_recent = deque()

    def load(self, directory):
        self._loaded.clear()
        self._ids.clear()
        self._plugins.clear()
        self._enabled_path = Path(directory).parent / "plugin-settings" / ".enabled.json"
        self._manager_state = {"enabled": True, "pinned": []}
        self._manager_path = self._enabled_path.with_name(".manager.json")
        try:
            state = json.loads(self._manager_path.read_text(encoding="utf-8"))
            if (type(state) is dict and type(state.get("enabled")) is bool
                    and type(state.get("pinned")) is list
                    and all(type(value) is str for value in state["pinned"])):
                self._manager_state = state
        except FileNotFoundError:
            pass
        except BaseException:
            _report_failure("plugin manager state load failed")
        enabled = self._read_enabled_state()
        self._enabled_state = dict(enabled)
        for path in sorted(Path(directory).glob("*.py")):
            if path.is_symlink() or not path.is_file() or path.name.startswith("_"):
                continue
            try:
                tree = ast.parse(path.read_bytes(), filename=str(path))
                metadata = _metadata(tree)
                plugin_id = metadata["__id__"]
                if plugin_id in self._plugins:
                    raise ValueError(f"Duplicate plugin id: {plugin_id}")
                entry = {
                    "path": path,
                    "name": metadata["__name__"],
                    "version": metadata["__version__"],
                    "error": "",
                }
                entry.update(self._presentation(tree))
                self._plugins[plugin_id] = entry
                if self._manager_state["enabled"] and enabled.get(plugin_id, True) and not self._load_one(path):
                    entry["error"] = "Plugin failed to load; see log"
            except BaseException:
                _report_failure(f"discovery failed: {path.name}")
        _write_log(f"Plugin Host: loaded {len(self._loaded)} plugin(s)")
        return len(self._loaded)

    @staticmethod
    def _presentation(tree):
        values = {"author": "", "description": "", "declared": ""}
        names = {"__author__": "author", "__description__": "description",
                 "__permissions__": "declared", "__capabilities__": "declared"}
        for node in tree.body:
            if isinstance(node, ast.Assign):
                for target in node.targets:
                    if isinstance(target, ast.Name) and target.id in names:
                        try:
                            value = ast.literal_eval(node.value)
                            if isinstance(value, (list, tuple)) and all(type(x) is str for x in value):
                                value = ", ".join(value)
                            if type(value) is str:
                                values[names[target.id]] = value[:2048]
                        except (ValueError, TypeError):
                            pass
        references = {node.attr for node in ast.walk(tree) if isinstance(node, ast.Attribute)}
        references |= {node.id for node in ast.walk(tree) if isinstance(node, ast.Name)}
        references |= {node.name for node in ast.walk(tree) if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))}
        groups = {
            "Read messages": {"on_message", "on_message_edit", "on_message_delete", "on_update_hook"},
            "Send/modify messages": {"add_on_send_message_hook", "on_send_message_hook", "pre_request_hook"},
            "UI integration": {"add_menu_item", "show_toast", "set_composer_effects", "create_settings"},
            "Plugin storage": {"get_setting", "set_setting"},
        }
        values["detected"] = ", ".join(label for label, symbols in groups.items() if references & symbols)
        return values

    def manager_snapshot(self):
        return (self._manager_state["enabled"], [
            (*row, entry["author"], entry["description"], entry["declared"],
             entry["detected"], plugin_id in self._manager_state["pinned"],
             self._enabled_state.get(plugin_id, True))
            for row in self.plugins_snapshot()
            for plugin_id in [row[0]] for entry in [self._plugins[plugin_id]]
        ])

    def manage_plugin(self, plugin_id, enabled, action):
        if action == "enable":
            return self.set_plugin_enabled(plugin_id, enabled)
        if action not in ("global", "pin", "remove") or type(enabled) is not bool:
            return False
        entry = self._plugins.get(plugin_id)
        if action != "global" and entry is None:
            return False
        state = dict(self._manager_state)
        if action == "global":
            state["enabled"] = enabled
        elif action == "pin":
            pins = set(state["pinned"])
            pins.add(plugin_id) if enabled else pins.discard(plugin_id)
            state["pinned"] = sorted(pins)
        else:
            path = entry["path"]
            if path.is_symlink() or not path.is_file():
                return False
            if not self._write_enabled_state(plugin_id, False):
                return False
            self._unload_plugin(plugin_id)
            try:
                path.unlink()
            except OSError:
                entry["error"] = "Could not remove file; plugin was disabled"
                _report_failure(f"plugin removal failed: {plugin_id}")
                return False
            del self._plugins[plugin_id]
            return True
        try:
            self._manager_path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self._manager_path.with_suffix(".tmp")
            temporary.write_text(json.dumps(state, sort_keys=True), encoding="utf-8")
            os.replace(temporary, self._manager_path)
        except OSError:
            _report_failure("plugin manager state save failed")
            return False
        self._manager_state = state
        if action == "global":
            for current_id, current in self._plugins.items():
                if not enabled:
                    self._unload_plugin(current_id)
                elif current_id not in self._ids and self._enabled_state.get(current_id, True):
                    current["error"] = "" if self._load_one(current["path"]) else "Plugin failed to load; see log"
        return True

    def _read_enabled_state(self):
        path = self._enabled_path
        if path is None or not path.is_file():
            return {}
        try:
            state = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(state, dict) or any(
                    not isinstance(key, str) or type(value) is not bool
                    for key, value in state.items()):
                raise ValueError("Plugin enabled state must be a boolean object")
            return state
        except BaseException:
            _report_failure("plugin enabled state load failed")
            return {}

    def _write_enabled_state(self, plugin_id, enabled):
        path = self._enabled_path
        if path is None:
            return False
        state = dict(self._enabled_state)
        state[plugin_id] = enabled
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = path.with_suffix(path.suffix + ".tmp")
            temporary.write_text(
                json.dumps(state, ensure_ascii=False, sort_keys=True),
                encoding="utf-8")
            os.replace(temporary, path)
            self._enabled_state = state
            return True
        except BaseException:
            _report_failure(f"plugin enabled state save failed: {plugin_id}")
            return False

    def plugins_snapshot(self):
        return [
            (plugin_id, entry["name"], entry["version"],
             plugin_id in self._ids, entry["error"])
            for plugin_id, entry in self._plugins.items()
        ]

    def set_plugin_enabled(self, plugin_id, enabled):
        if type(enabled) is not bool:
            raise TypeError("Plugin enabled state must be a bool")
        entry = self._plugins.get(plugin_id)
        if entry is None:
            return False
        loaded = plugin_id in self._ids
        if loaded == enabled and self._enabled_state.get(plugin_id, True) == enabled:
            return True
        if not self._write_enabled_state(plugin_id, enabled):
            return False
        if enabled and self._manager_state["enabled"]:
            if not self._load_one(entry["path"]):
                entry["error"] = "Plugin failed to load; see log"
                self._write_enabled_state(plugin_id, False)
                return False
            entry["error"] = ""
        else:
            self._unload_plugin(plugin_id)
        return True

    def _unload_plugin(self, plugin_id):
        for index, (current_id, module_name, instance) in enumerate(self._loaded):
            if current_id != plugin_id:
                continue
            self._loaded.pop(index)
            instance._update_hooks.clear()
            instance._send_priority = None
            instance._menu_items.clear()
            instance._setting_callbacks.clear()
            instance._setting_types.clear()
            self._unload_one(instance, module_name)
            self._ids.discard(plugin_id)
            _write_log(f"Plugin Host: unloaded {plugin_id}")
            return True
        return False

    def _load_one(self, path):
        module_name = None
        instance = None
        load_started = False
        try:
            source = path.read_bytes()
            tree = ast.parse(source, filename=str(path))
            metadata = _metadata(tree)
            plugin_id = metadata["__id__"]
            if plugin_id in self._ids:
                raise ValueError(f"Duplicate plugin id: {plugin_id}")
            module_name = f"_moegram_plugin_{plugin_id}"
            module = types.ModuleType(module_name)
            module.__file__ = str(path)
            sys.modules[module_name] = module
            for node in tree.body:
                if isinstance(node, ast.Assign):
                    node.targets = [
                        target for target in node.targets
                        if not (isinstance(target, ast.Name) and target.id == "__name__")
                    ]
            tree.body = [
                node for node in tree.body
                if not isinstance(node, ast.Assign) or node.targets
            ]
            exec(compile(tree, str(path), "exec"), module.__dict__)
            classes = [
                module.__dict__.get(node.name)
                for node in tree.body
                if isinstance(node, ast.ClassDef)
            ]
            classes = [
                cls for cls in classes
                if isinstance(cls, type) and cls is not BasePlugin
                and issubclass(cls, BasePlugin)
            ]
            if len(classes) != 1:
                raise ValueError("Define exactly one BasePlugin subclass")
            instance = classes[0]()
            instance.id = plugin_id
            instance.name = metadata["__name__"]
            instance.version = metadata["__version__"]
            instance._update_hooks = {}
            instance._send_priority = None
            instance._menu_items = []
            instance._settings = {}
            instance._setting_callbacks = {}
            instance._setting_types = {}
            settings_path = path.parent.parent / "plugin-settings" / f"{plugin_id}.json"
            instance._settings_path = settings_path
            instance._settings_reload_requested = False
            if settings_path.is_file():
                try:
                    settings = json.loads(settings_path.read_text(encoding="utf-8"))
                    if not isinstance(settings, dict):
                        raise ValueError("Plugin settings must be a JSON object")
                    instance._settings = settings
                except BaseException:
                    _report_failure(f"settings load failed: {plugin_id}")
            instance.logger = logging.getLogger(f"plugin.{plugin_id}")
            load_started = True
            instance._composer_active = True
            instance.on_plugin_load()
            self._loaded.append((plugin_id, module_name, instance))
            self._ids.add(plugin_id)
            _write_log(
                f"Plugin Host: loaded {plugin_id} "
                f"({instance.name}, {instance.version})"
            )
            return True
        except BaseException:
            _report_failure(f"load failed: {path.name}")
            if load_started:
                self._unload_one(instance, module_name)
            elif module_name is not None:
                sys.modules.pop(module_name, None)
            return False

    def _unload_one(self, instance, module_name):
        instance._composer_active = False
        _publish_composer(instance.id, None)
        try:
            instance.on_plugin_unload()
        except BaseException:
            _report_failure(f"unload failed: {module_name}")
        finally:
            sys.modules.pop(module_name, None)

    def dispatch_send(self, account, message):
        hooks = [(instance._send_priority, order, plugin_id, instance)
                 for order, (plugin_id, _, instance) in enumerate(self._loaded)
                 if instance._send_priority is not None]
        hooks.sort(key=lambda hook: (-hook[0], hook[1]))
        for _, _, plugin_id, instance in hooks:
            params = SendParams(message)
            try:
                result = instance.on_send_message_hook(account, params)
                if result is None:
                    continue
                if not isinstance(result, HookResult):
                    raise TypeError("Send hook must return HookResult or None")
                if result.strategy == HookStrategy.CANCEL:
                    return None
                if result.strategy == HookStrategy.DEFAULT:
                    continue
                if result.strategy not in (HookStrategy.MODIFY, HookStrategy.MODIFY_FINAL):
                    raise ValueError("Unsupported send hook strategy")
                candidate = result.params if result.params is not None else params
                if not isinstance(candidate, SendParams) or not isinstance(candidate.message, str):
                    raise TypeError("Send hook params.message must remain a string")
                candidate.message.encode("utf-8")
                message = candidate.message
                if result.strategy == HookStrategy.MODIFY_FINAL:
                    break
            except BaseException:
                _report_failure(f"on_send_message_hook failed: {plugin_id}")
        return message

    def _matching_hooks(self, name):
        hooks = []
        for order, (plugin_id, _, instance) in enumerate(self._loaded):
            priorities = [priority for pattern, (substring, priority)
                          in instance._update_hooks.items()
                          if (pattern in name if substring else pattern == name)]
            if priorities:
                hooks.append((-max(priorities), order, plugin_id, instance))
        return sorted(hooks, key=lambda hook: hook[:2])

    def dispatch_request(self, name, account, offline):
        if name not in ("TL_messages_setTyping", "TL_account_updateStatus"):
            raise ValueError("Unsupported request DTO")
        for _, _, plugin_id, instance in self._matching_hooks(name):
            request = (TL_account_updateStatus(offline)
                       if name == "TL_account_updateStatus" else TL_messages_setTyping())
            try:
                result = instance.pre_request_hook(name, account, request)
                if result is None:
                    continue
                if not isinstance(result, HookResult):
                    raise TypeError("pre_request_hook must return HookResult or None")
                if result.strategy == HookStrategy.CANCEL:
                    return None
                if result.strategy == HookStrategy.DEFAULT:
                    continue
                if result.strategy not in (HookStrategy.MODIFY, HookStrategy.MODIFY_FINAL):
                    raise ValueError("Unsupported request strategy")
                candidate = result.request if result.request is not None else request
                if type(candidate) is not type(request):
                    raise TypeError("Request DTO type must remain unchanged")
                if isinstance(candidate, TL_account_updateStatus):
                    if type(candidate.offline) is not bool:
                        raise TypeError("Request offline must remain a bool")
                    offline = candidate.offline
                if result.strategy == HookStrategy.MODIFY_FINAL:
                    break
            except BaseException:
                _report_failure(f"pre_request_hook failed: {plugin_id}")
        return (offline,)

    def dispatch_response(self, name, account, value, error_code, error_text):
        response = BoolResponse(value) if value is not None else None
        error = RpcError(error_code, error_text) if error_code is not None else None
        for _, _, plugin_id, instance in self._matching_hooks(name):
            try:
                result = instance.post_request_hook(name, account, response, error)
                if result is not None:
                    if not isinstance(result, HookResult):
                        raise TypeError("post_request_hook must return HookResult or None")
                    if result.strategy is not HookStrategy.DEFAULT:
                        _write_log(f"Plugin Host: {plugin_id}: {result.strategy.name} "
                                   "is unsupported for read-only response notifications")
            except BaseException:
                _report_failure(f"post_request_hook failed: {plugin_id}")

    def dispatch_update(self, name, payloads, pts=None, pts_count=None, native=False):
        if not payloads:
            return
        supported = {
            "TL_updateNewMessage": "on_message",
            "TL_updateNewChannelMessage": "on_message",
            "TL_updateEditMessage": "on_message_edit",
            "TL_updateEditChannelMessage": "on_message_edit",
            "TL_updateDeleteMessages": "on_message_delete",
            "TL_updateDeleteChannelMessages": "on_message_delete",
        }
        if name not in supported:
            raise ValueError(f"Unsupported update: {name}")
        if native and pts is not None:
            key = (name, pts, pts_count, tuple(
                (p["account_id"], p["chat_id"], p["message_id"]) for p in payloads
            ))
            if key in self._seen:
                return
            self._seen.add(key)
            self._recent.append(key)
            if len(self._recent) > 4096:
                self._seen.remove(self._recent.popleft())
        if supported[name] == "on_message":
            identity = tuple((p["account_id"], p["chat_id"], p["message_id"]) for p in payloads)
            if identity in self._created:
                return
            self._created.add(identity)
            self._created_recent.append(identity)
            if len(self._created_recent) > 4096:
                self._created.remove(self._created_recent.popleft())
        delete_keys = []
        if supported[name] == "on_message_delete":
            for payload in payloads:
                chat = payload["chat_id"]
                scope = chat if chat is not None and chat <= -1000000000000 else None
                delete_keys.append((payload["account_id"], scope, payload["message_id"]))
            if all(key in self._deleted for key in delete_keys):
                return
        deliver_delete = []
        for key in delete_keys:
            deliver_delete.append(key not in self._deleted)
            if key not in self._deleted:
                self._deleted.add(key)
                self._deleted_recent.append(key)
                if len(self._deleted_recent) > 4096:
                    self._deleted.remove(self._deleted_recent.popleft())
        snapshots = tuple(Message(**payload) for payload in payloads)
        self._sequence += 1
        update = Update(name, snapshots[0].account_id, snapshots, self._sequence,
                        pts, pts_count, "telegram" if native else "model")
        for _, _, plugin_id, instance in self._matching_hooks(name):
            try:
                result = instance.on_update_hook(name, update.account_id, update)
                if result is not None:
                    if not isinstance(result, HookResult):
                        raise TypeError("on_update_hook must return HookResult or None")
                    if result.strategy is not HookStrategy.DEFAULT:
                        _write_log(f"Plugin Host: {plugin_id}: {result.strategy.name} "
                                   "is unsupported for read-only update notifications")
            except BaseException:
                _report_failure(f"on_update_hook failed: {plugin_id}")
        callback = supported[name]
        for plugin_id, _, instance in self._loaded:
            for index, message in enumerate(update.message_snapshots):
                if delete_keys and not deliver_delete[index]:
                    continue
                if callback == "on_message" and message.outgoing is not False:
                    continue
                try:
                    getattr(instance, callback)(message)
                except BaseException:
                    _report_failure(f"{callback} failed: {plugin_id}")

    def message_menu_items(self):
        items = []
        for plugin_order, (plugin_id, _, instance) in enumerate(self._loaded):
            for item_order, (item_id, text, priority, _) in enumerate(
                    instance._menu_items):
                items.append((f"{plugin_id}:{item_id}", text, priority,
                              plugin_order, item_order))
        items.sort(key=lambda item: (-item[2], item[3], item[4]))
        return [(item_id, text, priority)
                for item_id, text, priority, _, _ in items]

    @staticmethod
    def _setting_text(value, field, optional=False):
        if optional and value is None:
            return ""
        if not isinstance(value, str):
            raise TypeError(f"{field} must be a string")
        value.encode("utf-8")
        return value

    def settings_snapshot(self):
        pages = []
        for plugin_id, _, instance in self._loaded:
            rows = []
            instance._setting_callbacks = {}
            instance._setting_types = {}
            try:
                descriptors = instance.create_settings()
                if not isinstance(descriptors, (list, tuple)):
                    raise TypeError("create_settings() must return a list")
                seen_keys = set()
                for descriptor in descriptors:
                    try:
                        if isinstance(descriptor, Header):
                            rows.append(("header", "", self._setting_text(
                                descriptor.text, "Header.text"), "", "", False))
                        elif isinstance(descriptor, Divider):
                            rows.append(("divider", "", self._setting_text(
                                descriptor.text, "Divider.text", optional=True),
                                "", "", False))
                        elif isinstance(descriptor, Text):
                            if descriptor.on_click is not None or descriptor.on_long_click is not None:
                                _write_log(
                                    f"Plugin Host: {plugin_id}: Text callbacks are unsupported; ignored")
                            rows.append(("text", "", self._setting_text(
                                descriptor.text, "Text.text"), self._setting_text(
                                descriptor.subtext, "Text.subtext", optional=True),
                                "", False))
                        elif isinstance(descriptor, Selector):
                            key = self._setting_text(descriptor.key, "selector.key")
                            if not key or key in seen_keys:
                                raise ValueError("Invalid or duplicate selector key")
                            seen_keys.add(key)
                            title = self._setting_text(descriptor.text, "selector.text")
                            if not isinstance(descriptor.items, (list, tuple)) or not 0 < len(descriptor.items) <= 64:
                                raise ValueError("Selector needs 1–64 options")
                            items = tuple(self._setting_text(item, "selector.items") for item in descriptor.items)
                            default = descriptor.default
                            if type(default) is not int or not 0 <= default < len(items):
                                raise ValueError("Selector.default must be a valid index")
                            value = instance.get_setting(key, default)
                            if type(value) is not int or not 0 <= value < len(items):
                                value = default
                            instance._setting_types[key] = (int, len(items))
                            if callable(descriptor.on_change):
                                instance._setting_callbacks[key] = descriptor.on_change
                            elif descriptor.on_change is not None:
                                _write_log(f"Plugin Host: {plugin_id}: selector.on_change is not callable; ignored")
                            if descriptor.on_long_click is not None:
                                _write_log(f"Plugin Host: {plugin_id}: selector.on_long_click is unsupported; ignored")
                            rows.append(("selector", key, title, "", "", (value, items)))
                        elif isinstance(descriptor, (Input, Switch)):
                            kind = "input" if isinstance(descriptor, Input) else "switch"
                            key = self._setting_text(descriptor.key, f"{kind}.key")
                            if not key:
                                raise ValueError(f"{kind}.key must not be empty")
                            if key in seen_keys:
                                raise ValueError(f"Duplicate setting key: {key}")
                            seen_keys.add(key)
                            title = self._setting_text(descriptor.text, f"{kind}.text")
                            subtext = self._setting_text(
                                descriptor.subtext, f"{kind}.subtext", optional=True)
                            default = descriptor.default
                            expected = str if kind == "input" else bool
                            if kind == "input":
                                if default is not None and not isinstance(default, str):
                                    raise TypeError("Input.default must be a string or None")
                                default = default or ""
                            elif type(default) is not bool:
                                raise TypeError("Switch.default must be a bool")
                            callback = descriptor.on_change
                            if callback is not None and not callable(callback):
                                _write_log(
                                    f"Plugin Host: {plugin_id}: {kind}.on_change is not callable; ignored")
                                callback = None
                            if descriptor.on_long_click is not None:
                                _write_log(
                                    f"Plugin Host: {plugin_id}: {kind}.on_long_click is unsupported; ignored")
                            value = instance.get_setting(key, default)
                            if (kind == "input" and not isinstance(value, str)) or (
                                    kind == "switch" and type(value) is not bool):
                                _write_log(
                                    f"Plugin Host: {plugin_id}: invalid saved value for {key}; using default")
                                value = default
                            instance._setting_types[key] = expected
                            if callback is not None:
                                instance._setting_callbacks[key] = callback
                            rows.append((kind, key, title, subtext,
                                         value if kind == "input" else "",
                                         value if kind == "switch" else False))
                        else:
                            _write_log(
                                f"Plugin Host: {plugin_id}: unsupported settings item "
                                f"{type(descriptor).__name__}; ignored")
                    except BaseException:
                        _report_failure(f"invalid settings item: {plugin_id}")
                        rows.append(("error", "", "Invalid plugin setting; see log", "", "", False))
            except BaseException:
                _report_failure(f"create_settings failed: {plugin_id}")
                rows = [("error", "", "Settings unavailable; see log", "", "", False)]
            pages.append((plugin_id, instance.name, rows))
        return pages

    def update_setting(self, plugin_id, key, value):
        for current_id, _, instance in self._loaded:
            if current_id != plugin_id:
                continue
            expected = instance._setting_types.get(key)
            valid = (type(value) is int and 0 <= value < expected[1]) if isinstance(expected, tuple) else (expected is not None and type(value) is expected)
            if not valid:
                _write_log(f"Plugin Host: {plugin_id}: rejected invalid setting update for {key}")
                return False
            try:
                instance._settings_reload_requested = False
                instance.set_setting(key, value)
                callback = instance._setting_callbacks.get(key)
                if callback is not None:
                    try:
                        callback(value)
                    except BaseException:
                        _report_failure(f"settings callback failed: {plugin_id}:{key}")
                reload_settings = instance._settings_reload_requested
                instance._settings_reload_requested = False
                return reload_settings
            except BaseException:
                _report_failure(f"settings save failed: {plugin_id}:{key}")
                return False
        return False

    def dispatch_menu_item(self, item_id, payload):
        try:
            message = Message(**payload)
        except BaseException:
            _report_failure("message menu snapshot construction failed")
            return
        context = {
            "account": message.account_id,
            "dialog_id": message.chat_id,
            "message": message,
        }
        for plugin_id, _, instance in self._loaded:
            prefix = f"{plugin_id}:"
            if not item_id.startswith(prefix):
                continue
            local_id = item_id[len(prefix):]
            for registered_id, _, _, callback in instance._menu_items:
                if registered_id != local_id:
                    continue
                try:
                    callback(context)
                except BaseException:
                    _report_failure(f"message menu callback failed: {plugin_id}")
                return

    def stop(self):
        while self._loaded:
            self._unload_plugin(self._loaded[-1][0])
        self._seen.clear()
        self._recent.clear()
        self._created.clear()
        self._created_recent.clear()
        self._deleted.clear()
        self._deleted_recent.clear()
