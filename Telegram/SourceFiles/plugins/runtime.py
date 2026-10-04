import ast
import logging
from pathlib import Path
import re
import sys
import traceback
import types


class _LogHandler(logging.Handler):
    def emit(self, record):
        _write_log(self.format(record))


class BasePlugin:
    def __init__(self):
        self.id = ""
        self.name = ""
        self.version = ""
        self.logger = logging.getLogger("plugin.unbound")

    def log(self, message):
        self.logger.info(message)

    def on_plugin_load(self):
        pass

    def on_plugin_unload(self):
        pass


_base_module = types.ModuleType("base_plugin")
_base_module.BasePlugin = BasePlugin
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
                "__id__", "__name__", "__version__"
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
    return result


class PluginHost:
    def __init__(self):
        self._loaded = []
        self._ids = set()

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

    def stop(self):
        while self._loaded:
            plugin_id, module_name, instance = self._loaded.pop()
            self._unload_one(instance, module_name)
            self._ids.discard(plugin_id)
            _write_log(f"Plugin Host: unloaded {plugin_id}")
