import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import types


def check():
    runtime = Path(__file__).resolve().parent.parent / "runtime.py"
    fixture = Path(__file__).resolve().parent / "fixtures/exteragram_ghost_mode"
    original = (fixture / "ghost_mode.py").read_bytes()
    assert hashlib.sha256(original).hexdigest() == json.loads((fixture / "provenance.json").read_text())["sha256"]
    logs = []
    module = types.ModuleType("_request_test_runtime")
    module._write_log = logs.append
    module._publish_composer = lambda plugin_id, config: None
    sys.modules[module.__name__] = module
    exec(compile(runtime.read_text(), str(runtime), "exec"), module.__dict__)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        plugins = root / "plugins"
        plugins.mkdir()
        shutil.copyfile(fixture / "ghost_mode.py", plugins / "ghost_mode.py")
        host = module.PluginHost()
        assert host.load(plugins) == 1
        ghost = host._loaded[0][2]
        rows = ghost.create_settings()
        assert [row.key for row in rows] == ["dont_send_typing", "force_offline"]
        assert all(type(row).__name__ == "Switch" and row.default is True for row in rows)
        assert host.dispatch_request("TL_messages_setTyping", 123, None) is None
        assert host.dispatch_request("TL_account_updateStatus", 123, False) == (True,)
        assert host.dispatch_request("TL_account_updateStatus", 123, True) == (True,)
        ghost.remove_hook("TL_messages_setTyping")
        assert host.dispatch_request("TL_messages_setTyping", 123, None) == (None,)
        host.stop()
        assert not ghost._update_hooks
        assert host.dispatch_request("TL_account_updateStatus", 123, False) == (False,)
        settings = root / "plugin-settings"
        settings.mkdir()
        (settings / "ghost_mode.json").write_text(json.dumps({"dont_send_typing": False, "force_offline": False}))
        assert host.load(plugins) == 1
        assert host.dispatch_request("TL_messages_setTyping", 123, None) == (None,)
        assert host.dispatch_request("TL_account_updateStatus", 123, False) == (False,)
        host.stop()
        (settings / "ghost_mode.json").unlink()
        (plugins / "00_faulty.py").write_text('''from base_plugin import BasePlugin, HookResult, HookStrategy
__id__ = "faulty_request"
__name__ = "Faulty"
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook("TL_account", match_substring=True, priority=100)
    def pre_request_hook(self, name, account, request):
        request.offline = "invalid"
        raise SystemExit("intentional request exception")
    def post_request_hook(self, name, account, response, error):
        raise KeyboardInterrupt("intentional response exception")
''')
        (plugins / "zz_probe.py").write_text('''from base_plugin import BasePlugin, HookResult
__id__ = "request_probe"
__name__ = "Probe"
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook("TL_account", True, -1)
        self.add_hook("TL_account_updateStatus", False, -1)
        self.calls = 0
    def pre_request_hook(self, name, account, request):
        assert account == 123 and request.offline is True
        self.calls += 1
        self.request = request
        return HookResult()
    def post_request_hook(self, name, account, response, error):
        if error:
            assert response is None and error.code == 400 and error.text == "TEST_ERROR"
        else:
            assert response.value is True and error is None
        self.log("observed response")
''')
        assert host.load(plugins) == 3
        assert host.dispatch_request("TL_account_updateStatus", 123, False) == (True,)
        probe = host._loaded[-1][2]
        assert probe.calls == 1
        probe.request.offline = False
        assert host.dispatch_request("TL_account_updateStatus", 123, False) == (True,)
        host.dispatch_response("TL_account_updateStatus", 123, True, None, "")
        host.dispatch_response("TL_account_updateStatus", 123, None, 400, "TEST_ERROR")
        assert logs.count("plugin.request_probe [INFO] observed response") == 2
        assert any("intentional request exception" in line for line in logs)
        assert any("intentional response exception" in line for line in logs)
        assert not any("failed: request_probe" in line for line in logs)
        host.stop()
        shutil.rmtree(plugins)
        plugins.mkdir()
        for strategy, expected in [("DEFAULT", False), ("MODIFY", True), ("MODIFY_FINAL", True), ("CANCEL", None)]:
            (plugins / "strategy.py").write_text(f'''from base_plugin import BasePlugin, HookResult, HookStrategy
__id__ = "strategy"
__name__ = "Strategy"
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook("TL_account_updateStatus")
    def pre_request_hook(self, name, account, request):
        request.offline = True
        return HookResult(strategy=HookStrategy.{strategy}, request=request)
''')
            assert host.load(plugins) == 1
            assert host.dispatch_request("TL_account_updateStatus", 123, False) == (None if expected is None else (expected,))
            host.stop()
        (plugins / "strategy.py").unlink()
        for name in ("10_first", "20_second"):
            (plugins / (name + ".py")).write_text(f'''from base_plugin import BasePlugin, HookResult, HookStrategy
__id__ = "{name[3:]}"
__name__ = "Order"
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook("TL_account_updateStatus", priority=5)
    def pre_request_hook(self, name, account, request):
        self.log("order {name}")
        request.offline = True
        return HookResult(strategy=HookStrategy.MODIFY_FINAL, request=request)
''')
        assert host.load(plugins) == 2
        assert host.dispatch_request("TL_account_updateStatus", 123, False) == (True,)
        assert any("order 10_first" in log for log in logs)
        assert not any("order 20_second" in log for log in logs)
        host.stop()
        shutil.rmtree(plugins)
        plugins.mkdir()
        for result in ("123", "HookResult(strategy='invalid')",
                       "HookResult(strategy=HookStrategy.MODIFY, request=object())",
                       "HookResult(strategy=HookStrategy.MODIFY, request=request)"):
            (plugins / "invalid.py").write_text(f'''from base_plugin import BasePlugin, HookResult, HookStrategy
__id__ = "invalid"
__name__ = "Invalid"
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook("TL_account_updateStatus")
    def pre_request_hook(self, name, account, request):
        request.offline = 1
        return {result}
''')
            assert host.load(plugins) == 1
            assert host.dispatch_request("TL_account_updateStatus", 123, False) == (False,)
            host.stop()
        before = len(logs)
        host.dispatch_response("TL_account_updateStatus", 123, True, None, "")
        assert len(logs) == before
        assert (fixture / "ghost_mode.py").read_bytes() == original
    print("PASS original ghost_mode: source hash, registration, CANCEL/MODIFY, settings, errors, response observation, unload")


if __name__ == "__main__":
    check()
