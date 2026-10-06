import json
from pathlib import Path
import shutil
import sys
import tempfile
import types


def check():
    base = Path(__file__).resolve().parent
    runtime = base.parent / "runtime.py"
    hello = base / "fixtures/exteragram_hello_command/hello_command.py"
    logs = []
    module = types.ModuleType("_plugin_settings_test_runtime")
    module._write_log = logs.append
    module._publish_composer = lambda plugin_id, config: None
    sys.modules[module.__name__] = module
    exec(compile(runtime.read_text(), str(runtime), "exec"), module.__dict__)

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        plugins = root / "plugins"
        plugins.mkdir()
        shutil.copyfile(hello, plugins / "hello_command.py")
        (plugins / "settings_probe.py").write_text('''from base_plugin import BasePlugin, MenuItemData, MenuItemType
from ui.settings import Divider, Header, Input, Switch, Text
__id__ = "settings_probe"
__name__ = "Settings Probe"
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.menu_clicks = 0
        self.add_menu_item(MenuItemData(
            menu_type=MenuItemType.MESSAGE_CONTEXT_MENU,
            text="Probe",
            on_click=self.on_menu_click,
            item_id="probe",
        ))
    def on_menu_click(self, context):
        self.menu_clicks += 1
    def create_settings(self):
        return [
            Header(text="Probe"),
            Divider(text="Group"),
            Input(key="template", text="Probe value", default="probe", on_change=self.refresh),
            Input(key="bad_callback", text="Safe fallback", default="saved", on_change=object()),
            Switch(key="enabled", text="Enabled", default=True, on_change=self.fail),
            Text(text="Example", subtext="Read only"),
            object(),
        ]
    def fail(self, value):
        raise RuntimeError("intentional setting callback exception")
    def refresh(self, value):
        self.set_setting("derived", value, reload_settings=True)
''')
        (plugins / "broken_settings.py").write_text('''from base_plugin import BasePlugin
__id__ = "broken_settings"
__name__ = "Broken Settings"
class Plugin(BasePlugin):
    def create_settings(self):
        raise RuntimeError("intentional create_settings exception")
''')
        host = module.PluginHost()
        assert host.load(plugins) == 3
        assert all(entry[3] for entry in host.plugins_snapshot())
        pages = host.settings_snapshot()
        by_id = {page[0]: page for page in pages}
        instances = {plugin_id: instance for plugin_id, _, instance in host._loaded}
        assert [item[0] for item in host.message_menu_items()] == [
            "settings_probe:probe"
        ]
        payload = {
            "account_id": 123,
            "message_id": 456,
            "chat_id": 789,
            "sender_id": 321,
            "text": "probe",
            "outgoing": False,
        }
        host.dispatch_menu_item("settings_probe:probe", payload)
        assert instances["settings_probe"].menu_clicks == 1
        hello_rows = by_id["hello_world"][2]
        assert [row[0] for row in hello_rows] == ["header", "input", "text"]
        assert hello_rows[1][1] == "template"
        assert hello_rows[1][4] == "Hello, {name}!"
        probe_rows = by_id["settings_probe"][2]
        assert [row[0] for row in probe_rows] == [
            "header", "divider", "input", "input", "switch", "text"
        ]
        assert by_id["broken_settings"][2][0][0] == "error"
        assert host.update_setting("hello_world", "template", "Welcome, {name}.") is False
        assert host.dispatch_send(123, ".hello Alice") == "Welcome, Alice."
        assert host.update_setting("settings_probe", "template", "independent") is True
        assert instances["settings_probe"].get_setting("template") == "independent"
        assert instances["hello_world"].get_setting("template") == "Welcome, {name}."
        assert instances["settings_probe"].get_setting("derived") == "independent"
        assert host.update_setting("settings_probe", "bad_callback", "still saved") is False
        assert host.update_setting("settings_probe", "enabled", False) is False
        assert any("intentional setting callback exception" in line for line in logs)
        assert any("on_change is not callable; ignored" in line for line in logs)
        assert (root / "plugin-settings/hello_world.json").is_file()
        assert (root / "plugin-settings/settings_probe.json").is_file()
        assert host.set_plugin_enabled("hello_world", False)
        assert host.dispatch_send(123, ".hello Alice") == ".hello Alice"
        assert not next(entry for entry in host.plugins_snapshot()
                        if entry[0] == "hello_world")[3]
        assert host.set_plugin_enabled("hello_world", True)
        assert host.dispatch_send(123, ".hello Alice") == "Welcome, Alice."
        assert host.set_plugin_enabled("settings_probe", False)
        assert host.message_menu_items() == []
        host.dispatch_menu_item("settings_probe:probe", payload)
        assert instances["settings_probe"].menu_clicks == 1
        host.stop()

        assert host.load(plugins) == 2
        statuses = {entry[0]: entry for entry in host.plugins_snapshot()}
        assert statuses["settings_probe"][3] is False
        assert host.dispatch_send(123, ".hello Alice") == "Welcome, Alice."
        assert host.set_plugin_enabled("settings_probe", True)
        pages = host.settings_snapshot()
        by_id = {page[0]: page for page in pages}
        assert by_id["hello_world"][2][1][4] == "Welcome, {name}.", by_id["hello_world"]
        assert by_id["settings_probe"][2][2][4] == "independent"
        assert host.dispatch_send(123, ".hello Alice") == "Welcome, Alice."
        assert host.set_plugin_enabled("hello_world", False)
        host.stop()
        enabled_state = json.loads(
            (root / "plugin-settings/.enabled.json").read_text(encoding="utf-8"))
        assert enabled_state["hello_world"] is False
        assert enabled_state["settings_probe"] is True
        assert any("intentional create_settings exception" in line for line in logs)
        host = module.PluginHost()
        assert host.load(plugins) == 2
        statuses = {entry[0]: entry for entry in host.plugins_snapshot()}
        assert statuses["hello_world"][3] is False
        assert statuses["settings_probe"][3] is True
        assert host.dispatch_send(123, ".hello Alice") == ".hello Alice"
        assert host.set_plugin_enabled("hello_world", True)
        assert host.dispatch_send(123, ".hello Alice") == "Welcome, Alice."
        host.stop()

    print("PASS: native settings, enable/disable, unload, and restart persistence")


if __name__ == "__main__":
    check()
