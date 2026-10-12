import time

from base_plugin import BasePlugin, MenuItemData, MenuItemType

__id__ = "custom_top_bar"
__name__ = "Custom Top Bar"
__author__ = "tenku-png"
__version__ = "1.0.0"
__description__ = "A focus timer in the chat header, plus menu and overlay demos."
__capabilities__ = ["ui", "ui.advanced"]


class CustomTopBar(BasePlugin):
    def on_plugin_load(self):
        self.started = None
        self.render()
        self.set_interval(1, self.render)
        self.add_menu_item(MenuItemData(
            menu_type=MenuItemType.DRAWER_MENU,
            text="Reset focus timer",
            on_click=lambda ctx: self.reset(),
        ))
        self.add_menu_item(MenuItemData(
            menu_type=MenuItemType.CHAT_ACTION_MENU,
            text="Copy chat ID to toast",
            on_click=lambda ctx: self.show_toast(f"Chat ID: {ctx['chat_id']}"),
        ))
        bar = self.ui.find("chat.top_bar")
        bar.add_overlay({
            "rect": (0, 0, 0, 2), "background": "#7a5cff",  # accent line on top
        })
        bar.on_event("click", lambda args: self.log(f"header click at {args['x']}"))

    def render(self):
        if self.started is None:
            self.set_chat_header({
                "buttons": [{"text": "Focus", "on_click": self.start}],
            })
            return
        minutes, seconds = divmod(int(time.monotonic() - self.started), 60)
        self.set_chat_header({
            "subtitle": f"focusing for {minutes}:{seconds:02d}",
            "badges": [{"text": "FOCUS", "color": "#ffffff", "background": "#7a5cff"}],
            "buttons": [{"text": "Stop", "on_click": self.reset}],
            "background": "#7a5cff14",
        })

    def start(self):
        self.started = time.monotonic()
        self.render()

    def reset(self):
        self.started = None
        self.render()
