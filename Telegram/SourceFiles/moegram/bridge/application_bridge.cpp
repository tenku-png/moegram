#include "moegram/bridge/application_bridge.h"

#include "core/application.h"
#include "data/data_peer.h"
#include "dialogs/dialogs_key.h"
#include "main/main_session.h"
#include "plugins/plugin_host.h"
#include "plugins/telegram_event_adapter.h"
#include "moegram/bridge/media.h"
#include "moegram/composer/composer_bridge.h"
#include "moegram/ui/plugin_pills.h"
#include "moegram/ui/plugin_ui.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace MoeGram::Bridge {

ApplicationRuntime::ApplicationRuntime(Core::Application *application) {
	_host = std::make_unique<MoeGram::Plugins::Host>(
		[](const QString &message) { LOG((message)); },
		[application](const QString &message) {
			if (const auto window = application->activeWindow()) {
				window->showToast(message);
			}
		}, Composer::SetPluginEffects);
	_host->setPillCallback(MoeGram::Ui::SetPluginPill);
	_host->setUiCallback(MoeGram::Ui::SetPluginUi);
	_host->setMediaCallback(HandlePluginMedia);
	(void)_host->start(cWorkingDir() + u"plugins/"_q);
	application->appDeactivatedValue(
	) | rpl::distinct_until_changed(
	) | rpl::skip(1) | rpl::on_next([](bool deactivated) {
		Plugins::Publish(Plugins::AppEvent{
			.name = deactivated ? u"app_inactive"_q : u"app_active"_q,
		});
	}, _lifetime);
}

ApplicationRuntime::~ApplicationRuntime() = default;

void TrackActiveChat(not_null<Window::SessionController*> controller) {
	const auto account = controller->session().uniqueId();
	controller->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		const auto peer = key.peer();
		return peer
			? std::make_optional(Plugins::DialogId(peer->id))
			: std::optional<qint64>();
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](std::optional<qint64> chat) {
		Plugins::Publish(Plugins::AppEvent{
			.name = u"chat_opened"_q,
			.accountId = account,
			.chatId = chat,
		});
	}, controller->lifetime());
}

}
