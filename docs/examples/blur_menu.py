from base_plugin import BasePlugin

__id__ = "blur_menu"
__name__ = "Blur Menu"
__author__ = "tenku-png"
__version__ = "1.0.0"
__description__ = "Frosted glass menus and bars, motion blur on fast scroll."
__capabilities__ = ["ui"]


class BlurMenu(BasePlugin):
    def on_plugin_load(self):
        self.set_blur("menu", radius=20, opacity=0.72)
        self.set_blur("chat_header", radius=28, opacity=0.65)
        self.set_blur("composer", radius=28, opacity=0.7)
        self.set_blur("dialogs_header", radius=24, opacity=0.7)
        self.set_effect("scroll_motion_blur", {"strength": 1.0, "threshold": 1.5})
