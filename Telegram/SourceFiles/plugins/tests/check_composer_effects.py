from dataclasses import FrozenInstanceError
from pathlib import Path
import shutil
import json
import sys
import tempfile
import types


def check():
    source = Path(__file__).resolve().parents[3] / "ThirdParty/moegram-plugin-engine/python/runtime.py"
    root = Path(__file__).resolve().parents[4]
    logs, configurations = [], []
    module = types.ModuleType("_composer_test_runtime")
    module._write_log = logs.append
    module._publish_composer = lambda plugin_id, config: configurations.append((plugin_id, config))
    sys.modules[module.__name__] = module
    exec(compile(source.read_text(), str(source), "exec"), module.__dict__)
    effects = module.ComposerEffects(insertion="pop", deletion="scatter")
    try:
        effects.speed = 2
        raise AssertionError("mutable configuration")
    except FrozenInstanceError:
        pass
    for kwargs in ({"speed": float("nan")}, {"speed": True}, {"speed": 0},
                   {"style": "unknown"}, {"intensity": 3}, {"caret": "draw_python"}, {"edit": "unknown"}):
        try:
            module.ComposerEffects(**kwargs)
            raise AssertionError(f"accepted {kwargs}")
        except ValueError:
            pass
    with tempfile.TemporaryDirectory() as directory:
        plugins = Path(directory) / "plugins"
        plugins.mkdir()
        shutil.copyfile(root / "plugins/typing_fx.py", plugins / "typing_fx.py")
        host = module.PluginHost()
        assert host.load(plugins) == 1
        assert host.plugins_snapshot()[0][1] == "Typing FX"
        plugin = host._loaded[0][2]
        assert configurations[-1] == ("typing_fx", (True, True, True, True, True, 1., 1., "pop"))
        host.settings_snapshot()
        assert set(plugin._setting_types) == {"insert", "delete", "cursor", "send", "speed_choice", "intensity_choice", "style"}
        host.update_setting("typing_fx", "delete", False)
        assert configurations[-1][1][1] is False
        host.update_setting("typing_fx", "speed_choice", 2)
        assert configurations[-1][1][5] == 1.6
        previous = configurations[-1]
        for invalid in (True, -1, 3, "2", None):
            host.update_setting("typing_fx", "speed_choice", invalid)
            assert configurations[-1] == previous
        host.update_setting("typing_fx", "style", 2)
        assert configurations[-1][1][7] == "bouncy"
        assert host.set_plugin_enabled("typing_fx", False)
        assert configurations[-1] == ("typing_fx", None)
        try:
            plugin.set_composer_effects(effects)
            raise AssertionError("unloaded plugin registered effects")
        except RuntimeError:
            pass
        assert host.set_plugin_enabled("typing_fx", True)
        assert configurations[-1][1][1] is False
        host.settings_snapshot()
        host.update_setting("typing_fx", "intensity_choice", 2)
        host.stop()
        assert configurations[-1] == ("typing_fx", None)
        assert host.load(plugins) == 1
        assert configurations[-1][1][6:] == (1.6, "bouncy")
        host.stop()
        (plugins / "failed.py").write_text('''from base_plugin import BasePlugin
from moegram.composer import ComposerEffects
__id__ = "failed_fx"
__name__ = "Failed FX"
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.set_composer_effects(ComposerEffects(deletion="scatter"))
        raise RuntimeError("intentional effects load failure")
    def on_plugin_unload(self):
        raise RuntimeError("intentional effects unload failure")
''')
        assert host.load(plugins) == 1
        assert ("failed_fx", None) in configurations
        assert any("intentional effects load failure" in log for log in logs)
        host.stop()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        plugins = root / "plugins"
        plugins.mkdir()
        shutil.copyfile(Path(__file__).resolve().parents[4] / "plugins/typing_fx.py", plugins / "typing_fx.py")
        settings = root / "plugin-settings"
        settings.mkdir()
        path = settings / "typing_fx.json"
        path.write_text(json.dumps({"speed": "1.5", "intensity": "0.4", "delete": False}))
        host = module.PluginHost()
        assert host.load(plugins) == 1
        assert configurations[-1][1][1] is False
        assert configurations[-1][1][5:7] == (1.6, 0.5)
        assert json.loads(path.read_text())["speed_choice"] == 2
        host.stop()
        path.write_text(json.dumps({"speed": "nan", "intensity": "bad"}))
        assert host.load(plugins) == 1
        assert configurations[-1][1][5:7] == (1., 1.)
        host.stop()
    print("PASS: immutable config, validation, Typing FX, settings persistence, disable/reload, failed load cleanup")


if __name__ == "__main__":
    check()
