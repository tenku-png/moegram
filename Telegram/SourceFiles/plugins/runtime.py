import ast
import logging
import json
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


SDK_VERSION = "0.5.0"
API_VERSION = 1


@dataclass
class Header:
    text: str


@dataclass
class Input:
    key: str
    text: str
    default: str = ""
    subtext: str | None = None
    icon: str | None = None


@dataclass
class Switch:
    key: str
    text: str
    default: bool


@dataclass
class Text:
    text: str
    subtext: str | None = None
    icon: str | None = None


_ui_module = types.ModuleType("ui")
_ui_module.__path__ = []
_settings_module = types.ModuleType("ui.settings")
_settings_module.Header = Header
_settings_module.Input = Input
_settings_module.Text = Text
_settings_module.Switch = Switch
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
        self._settings = {}
        self.logger = logging.getLogger("plugin.unbound")

    def log(self, message):
        self.logger.info(message)

    def on_plugin_load(self):
        pass

    def on_plugin_unload(self):
        pass

    def add_on_send_message_hook(self, priority: int = 0):
        if not isinstance(priority, int):
            raise TypeError("Send hook priority must be an integer")
        self._send_priority = priority

    def on_send_message_hook(self, account: int, params) -> HookResult:
        return HookResult()

    def get_setting(self, key: str, default=None):
        return self._settings.get(key, default)

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
        self._sequence = 0
        self._seen = set()
        self._recent = deque()
        self._created = set()
        self._created_recent = deque()
        self._deleted = set()
        self._deleted_recent = deque()

    def load(self, directory):
        for path in sorted(Path(directory).glob("*.py")):
            if path.is_symlink() or not path.is_file() or path.name.startswith("_"):
                continue
            self._load_one(path)
        _write_log(f"Plugin Host: loaded {len(self._loaded)} plugin(s)")
        return len(self._loaded)

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
            settings_path = path.parent.parent / "plugin-settings" / f"{plugin_id}.json"
            if settings_path.is_file():
                settings = json.loads(settings_path.read_text(encoding="utf-8"))
                if not isinstance(settings, dict):
                    raise ValueError("Plugin settings must be a JSON object")
                instance._settings = settings
            instance.logger = logging.getLogger(f"plugin.{plugin_id}")
            load_started = True
            instance.on_plugin_load()
            self._loaded.append((plugin_id, module_name, instance))
            self._ids.add(plugin_id)
            _write_log(
                f"Plugin Host: loaded {plugin_id} "
                f"({instance.name}, {instance.version})"
            )
        except BaseException:
            _report_failure(f"load failed: {path.name}")
            if load_started:
                self._unload_one(instance, module_name)
            elif module_name is not None:
                sys.modules.pop(module_name, None)

    def _unload_one(self, instance, module_name):
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
            plugin_id, module_name, instance = self._loaded.pop()
            instance._update_hooks.clear()
            instance._send_priority = None
            instance._menu_items.clear()
            self._unload_one(instance, module_name)
            self._ids.discard(plugin_id)
            _write_log(f"Plugin Host: unloaded {plugin_id}")
        self._seen.clear()
        self._recent.clear()
        self._created.clear()
        self._created_recent.clear()
        self._deleted.clear()
        self._deleted_recent.clear()
