from base_plugin import BasePlugin
from ui.settings import Header, Selector

__id__ = "mono_color"
__name__ = "Mono Color"
__author__ = "tenku-png"
__version__ = "1.0.0"
__description__ = "Recolors the accent of the current theme."
__capabilities__ = ["ui", "storage"]

ACCENTS = {
    "Violet": "#7a5cff",
    "Pink": "#ff5c9a",
    "Mint": "#2ec4a6",
    "Orange": "#ff8a3d",
}

# Palette keys that carry the accent in default themes.
KEYS = (
    "windowBgActive", "windowActiveTextFg", "activeButtonBg",
    "activeButtonBgOver", "lightButtonFg", "dialogsBgActive",
    "dialogsUnreadBg", "historyLinkInFg", "msgOutBg", "msgFileOutBg",
    "historySendIconFg", "sideBarBgActive", "menuIconFgOver",
    "checkboxFg", "radialFg", "filterInputActiveBg",
)


class MonoColor(BasePlugin):
    def on_plugin_load(self):
        self.apply()

    def apply(self, *_):
        names = list(ACCENTS)
        name = names[self.get_setting("accent", 0) % len(names)]
        accent = ACCENTS[name]
        colors = {key: accent for key in KEYS}
        colors["msgOutBg"] = accent + "40"       # soft bubbles
        colors["msgFileOutBg"] = accent + "40"
        self.set_palette(colors)

    def create_settings(self):
        return [
            Header(text="Accent"),
            Selector(key="accent", text="Color", items=list(ACCENTS),
                     default=0, on_change=self.apply),
        ]
