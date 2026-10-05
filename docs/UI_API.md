# UI compatibility API (SDK 0.6.0, API 1)

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

`Settings > MoeGram Plugins` lists discovered plugins in deterministic order,
shows each plugin's version and enabled state, and provides enable/disable and
`Edit settings` actions. Disabling unloads the plugin and unregisters its hooks
and menu callbacks. Enabled state is stored in
`<workdir>/plugin-settings/.enabled.json` and persists across restarts.

The settings screen uses a native desktop subset of ExteraGram's
`ui.settings`: `Header`, `Switch`, `Input`, `Text`, and `Divider`.
Settings values are stored per plugin ID and persist across restarts. Settings
are editable while the plugin is enabled; disabled plugins must be enabled
before opening their settings.
See the settings compatibility section in [DEVELOPMENT.md](../DEVELOPMENT.md)
for callback behavior and unsupported descriptor fields.

This does not implement chat/profile/drawer menus, dialogs, Android icons,
subtext for menu items, MVEL conditions, arbitrary widgets, or overlay UI.
