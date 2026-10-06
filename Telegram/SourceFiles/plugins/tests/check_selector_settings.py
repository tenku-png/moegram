from pathlib import Path
import sys
import tempfile
import types


def check():
    source = Path(__file__).resolve().parent.parent / "runtime.py"
    logs = []
    module = types.ModuleType("_selector_test_runtime")
    module._write_log = logs.append
    module._publish_composer = lambda *_: None
    sys.modules[module.__name__] = module
    exec(compile(source.read_text(), str(source), "exec"), module.__dict__)
    with tempfile.TemporaryDirectory() as directory:
        plugins = Path(directory) / "plugins"
        plugins.mkdir()
        (plugins / "selector.py").write_text('''from base_plugin import BasePlugin
from ui.settings import Selector
__id__ = "selector_probe"
__name__ = "Selector Probe"
class Plugin(BasePlugin):
    def changed(self, index):
        self.set_setting("observed", index)
        raise RuntimeError("intentional selector callback exception")
    def create_settings(self):
        return [
            Selector("mode", "Mode", 1, ["Soft", "Pop", "Bouncy"], on_change=self.changed),
            Selector("empty", "Invalid", 0, []),
            Selector("bool", "Invalid", True, ["One", "Two"]),
            Selector("bad", "Invalid", 0, [object()]),
        ]
''')
        host = module.PluginHost()
        assert host.load(plugins) == 1
        rows = host.settings_snapshot()[0][2]
        assert rows[0] == ("selector", "mode", "Mode", "", "", (1, ("Soft", "Pop", "Bouncy")))
        assert [row[0] for row in rows[1:]] == ["error"] * 3
        for value in (True, -1, 3, "2", 1.0):
            host.update_setting("selector_probe", "mode", value)
        instance = host._loaded[0][2]
        assert instance.get_setting("mode") is None
        host.update_setting("selector_probe", "mode", 2)
        assert instance.get_setting("mode") == instance.get_setting("observed") == 2
        assert any("intentional selector callback exception" in log for log in logs)
        host.stop()
        assert host.load(plugins) == 1
        assert host.settings_snapshot()[0][2][0][5][0] == 2
        instance = host._loaded[0][2]
        instance.set_setting("mode", 99)
        assert host.settings_snapshot()[0][2][0][5][0] == 1
        instance.set_setting("mode", True)
        assert host.settings_snapshot()[0][2][0][5][0] == 1
        host.stop()
    print("PASS: Selector signature, immutable options snapshot, invalid descriptors/indices, callbacks, persistence")


if __name__ == "__main__":
    check()
