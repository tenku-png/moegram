# UI compatibility API (SDK 0.5.0, API 1)

The current UI slice supports message context menu items and text toasts.

```python
from base_plugin import BasePlugin, MenuItemData, MenuItemType

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
```

`MenuItemData` accepts ExteraGram field names: `menu_type`, `text`, `on_click`,
`item_id`, `icon`, `subtext`, `condition`, and `priority`. This slice registers
only `MESSAGE_CONTEXT_MENU`; it renders `text` and sorts by descending
`priority`. `icon`, `subtext`, and Android MVEL `condition` are currently ignored.
Equal priorities retain sorted plugin filename order, followed by registration
order within a plugin.

The click callback receives a dictionary with `account`, `dialog_id`, and
`message`. The message is the frozen `base_plugin.Message` snapshot with stable
`account_id`, `message_id`, `chat_id`, `sender_id`, `text`, and `outgoing`
fields. Existing compatibility aliases such as `getId()`, `getDialogId()`,
`messageText`, and `isOut()` are available. No TDesktop or Qt object is exposed.
Register menu items from `on_plugin_load()`; registrations are snapshotted when
the Host starts.

`BasePlugin.show_toast(text)` and `base_plugin.show_toast(text)` queue a plain
text toast to the desktop UI thread. Menu callbacks execute on the plugin worker;
exceptions are logged and isolated. Stopping the Plugin Host removes the
registered menu entries and rejects clicks after unload begins.

This does not implement chat/profile/drawer menus, settings screens, dialogs,
icons, subtext, conditions, arbitrary widgets, or overlay UI.
