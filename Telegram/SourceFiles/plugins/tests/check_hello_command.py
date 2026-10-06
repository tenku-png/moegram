import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import types


def check(runtime, fixture):
    source = fixture / "hello_command.py"
    provenance = json.loads((fixture / "provenance.json").read_text())
    original = source.read_bytes()
    assert hashlib.sha256(original).hexdigest() == provenance["sha256"]
    logs = []
    module = types.ModuleType("_hello_command_test_runtime")
    module._write_log = logs.append
    module._publish_composer = lambda plugin_id, config: None
    sys.modules[module.__name__] = module
    exec(compile(runtime.read_text(), str(runtime), "exec"), module.__dict__)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        plugins = root / "plugins"
        plugins.mkdir()
        shutil.copy2(source, plugins / source.name)
        host = module.PluginHost()
        assert host.load(plugins) == 1
        plugin = host._loaded[0][2]
        descriptors = plugin.create_settings()
        assert [type(value).__name__ for value in descriptors] == ["Header", "Input", "Text"]
        assert descriptors[1].key == "template"
        assert descriptors[1].default == "Hello, {name}!"
        assert plugin.get_setting("missing", 42) == 42
        assert host.dispatch_send(123, ".hello Alice") == "Hello, Alice!"
        assert host.dispatch_send(123, ".hello Alice Smith") == "Hello, Alice Smith!"
        assert host.dispatch_send(123, ".hello") == "Usage: .hello <name>"
        assert host.dispatch_send(123, "untouched") == "untouched"
        host.stop()
        assert host.dispatch_send(123, ".hello Alice") == ".hello Alice"
        settings = root / "plugin-settings"
        settings.mkdir()
        (settings / "hello_world.json").write_text(json.dumps({"template": "Welcome, {name}."}))
        assert host.load(plugins) == 1
        assert host.dispatch_send(123, ".hello Alice") == "Welcome, Alice."
        host._loaded[0][2]._settings["template"] = "{bad}"
        assert host.dispatch_send(123, ".hello Alice").startswith("Template must contain")
        host.stop()
        (settings / "hello_world.json").unlink()
        (plugins / "00_faulty.py").write_text(
            "from base_plugin import BasePlugin\n"
            "__id__='faulty_send'\n__name__='Faulty Send'\n"
            "class Plugin(BasePlugin):\n"
            "    def on_plugin_load(self):\n"
            "        self.add_on_send_message_hook(priority=10)\n"
            "    def on_send_message_hook(self, account, params):\n"
            "        params.message='corrupted'\n"
            "        raise SystemExit('intentional send exception')\n"
        )
        assert host.load(plugins) == 2
        assert host.dispatch_send(123, ".hello Alice") == "Hello, Alice!"
        assert any("intentional send exception" in line for line in logs)
        host.stop()
        (plugins / "00_faulty.py").unlink()
        (plugins / "00_strategies.py").write_text(
            "from base_plugin import BasePlugin, HookResult, HookStrategy\n"
            "__id__='send_strategies'\n__name__='Send Strategies'\n"
            "class Plugin(BasePlugin):\n"
            "    def on_plugin_load(self):\n"
            "        self.add_on_send_message_hook(priority=10)\n"
            "    def on_send_message_hook(self, account, params):\n"
            "        if params.message=='cancel':\n"
            "            return HookResult(strategy=HookStrategy.CANCEL)\n"
            "        if params.message=='final':\n"
            "            params.message='.hello Final'\n"
            "            return HookResult(strategy=HookStrategy.MODIFY_FINAL, params=params)\n"
            "        if params.message=='modify':\n"
            "            params.message='.hello Chained'\n"
            "            return HookResult(strategy=HookStrategy.MODIFY, params=params)\n"
            "        params.message='ignored mutation'\n"
            "        return HookResult()\n"
        )
        assert host.load(plugins) == 2
        assert host.dispatch_send(123, "cancel") is None
        assert host.dispatch_send(123, "final") == ".hello Final"
        assert host.dispatch_send(123, "modify") == "Hello, Chained!"
        assert host.dispatch_send(123, ".hello Alice") == "Hello, Alice!"
        host.stop()
        assert (plugins / source.name).read_bytes() == original
    assert source.read_bytes() == original
    print("PASS: original hello_command, settings descriptors/default/override, send strategies, exceptions, unload, 0 changed source lines")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime", type=Path, default=Path(__file__).parent.parent / "runtime.py")
    parser.add_argument("--fixture", type=Path, default=Path(__file__).parent / "fixtures" / "exteragram_hello_command")
    args = parser.parse_args()
    check(args.runtime, args.fixture)
