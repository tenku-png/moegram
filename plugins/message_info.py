from base_plugin import BasePlugin, MenuItemData, MenuItemType

__id__ = "message_info"
__name__ = "Message Info"
__version__ = "1.0.0"


class MessageInfoPlugin(BasePlugin):
    def on_plugin_load(self):
        self.add_menu_item(MenuItemData(
            menu_type=MenuItemType.MESSAGE_CONTEXT_MENU,
            text="Show message info",
            on_click=self.show_message_info,
        ))

    def show_message_info(self, context):
        message = context["message"]
        self.show_toast(
            f"Message ID: {message.message_id}, Chat ID: {message.chat_id}"
        )
