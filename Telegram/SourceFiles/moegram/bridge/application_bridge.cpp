#include "moegram/bridge/application_bridge.h"

#include "core/application.h"
#include "plugins/plugin_host.h"
#include "moegram/composer/composer_bridge.h"
#include "moegram/ui/plugin_pills.h"
#include "moegram/ui/plugin_ui.h"
#include "window/window_controller.h"

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
	(void)_host->start(cWorkingDir() + u"plugins/"_q);
}

ApplicationRuntime::~ApplicationRuntime() = default;

}
