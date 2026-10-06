#include "moegram/ui/message_menu.h"

#include "plugins/telegram_event_adapter.h"
#include "ui/widgets/popup_menu.h"

namespace MoeGram::Ui {

void AddMessageMenuItems(
		not_null<::Ui::PopupMenu*> menu,
		not_null<Main::Session*> session,
		not_null<HistoryItem*> item) {
	const auto snapshot = Plugins::MessageSnapshot(session, item);
	for (const auto &pluginItem : Plugins::MessageMenuItems()) {
		menu->addAction(pluginItem.text, [id = pluginItem.id, snapshot] {
			(void)Plugins::InvokeMessageMenuItem(id, snapshot);
		});
	}
}

}
