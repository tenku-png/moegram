#include "moegram/ui/plugin_menus.h"

#include "data/data_peer.h"
#include "main/main_session.h"
#include "moegram/ui/plugin_ui.h"
#include "plugins/telegram_event_adapter.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QtCore/QJsonArray>

namespace MoeGram::Ui {
namespace {

struct Item {
	QString pluginId;
	QString text;
	int priority = 0;
	QJsonValue callback;
};

std::vector<Item> Items(const QString &key) {
	auto result = std::vector<Item>();
	for (const auto &[pluginId, value] : PluginValues(key)) {
		for (const auto &entry : value.toArray()) {
			const auto object = entry.toObject();
			const auto text = object.value(u"text"_q).toString().trimmed();
			if (!text.isEmpty()) {
				result.push_back({
					.pluginId = pluginId,
					.text = text.left(64),
					.priority = object.value(u"priority"_q).toInt(),
					.callback = object.value(u"on_click"_q),
				});
			}
		}
	}
	ranges::stable_sort(result, ranges::greater(), &Item::priority);
	return result;
}

} // namespace

void SetupMainMenuItems(
		not_null<::Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto rebuild = [=] {
		container->clear();
		for (const auto &item : Items(u"menu:drawer"_q)) {
			Settings::AddButtonWithIcon(
				container,
				rpl::single(item.text),
				st::mainMenuButton,
				{ &st::menuIconCustomize }
			)->setClickedCallback([=] {
				InvokePluginCallback(item.pluginId, item.callback, {
					{ u"account"_q, QString::number(session->userId().bare) },
				});
			});
		}
		container->resizeToWidth(container->width());
	};
	PluginValueChanges(
		u"menu:drawer"_q
	) | rpl::on_next(rebuild, container->lifetime());
	rebuild();
}

void AddChatMenuItems(const ChatMenuAdd &add, not_null<PeerData*> peer) {
	const auto items = Items(u"menu:chat"_q);
	if (items.empty()) {
		return;
	}
	const auto chatId = Plugins::DialogId(peer->id);
	const auto account = QString::number(peer->session().userId().bare);
	for (const auto &item : items) {
		add(item.text, [=] {
			InvokePluginCallback(item.pluginId, item.callback, {
				{ u"account"_q, account },
				{ u"chat_id"_q, chatId },
				{ u"dialog_id"_q, chatId },
			});
		});
	}
}

}
