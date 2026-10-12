from base_plugin import BasePlugin

__id__ = "ram_info"
__name__ = "RAM Info"
__author__ = "tenku-png"
__version__ = "1.0.0"
__description__ = "Shows memory usage in a pill next to the chat search."
__capabilities__ = ["ui"]


def memory():
    """(used percent, used GiB) from /proc/meminfo, Linux only."""
    values = {}
    with open("/proc/meminfo") as file:
        for line in file:
            key, value = line.split(":", 1)
            values[key] = int(value.split()[0])
    total = values["MemTotal"]
    used = total - values.get("MemAvailable", values["MemFree"])
    return used * 100 / total, used / 1024 / 1024


class RamInfo(BasePlugin):
    def on_plugin_load(self):
        self.detailed = False
        self.tick()
        self.set_interval(2, self.tick)

    def tick(self):
        try:
            percent, gib = memory()
        except OSError:
            self.set_pill("RAM ?")
            return
        text = f"RAM {gib:.1f} GiB" if self.detailed else f"RAM {percent:.0f}%"
        self.set_pill(text, on_click=self.toggle)

    def toggle(self):
        self.detailed = not self.detailed
        self.tick()
