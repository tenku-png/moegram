#include "moegram/bridge/application_bridge.h"

#include "core/application.h"
#include "plugins/plugin_host.h"
#include "ui/widgets/fields/composer_effects.h"
#include "window/window_controller.h"

namespace MoeGram::Bridge {

ApplicationRuntime::ApplicationRuntime(Core::Application *application) {
	_host = std::make_unique<MoeGram::Plugins::Host>(
		[](const QString &message) { LOG((message)); },
		[application](const QString &message) {
			if (const auto window = application->activeWindow()) {
				window->showToast(message);
			}
		}, [](QString id, std::optional<MoeGram::Plugins::ComposerConfig> value) {
			auto config = std::optional<::Ui::ComposerEffectsConfig>();
			if (value) {
				config = ::Ui::ComposerEffectsConfig{
					.insertion = value->insertion,
					.deletion = value->deletion,
					.caret = value->caret,
					.send = value->send,
					.edit = value->edit,
					.speed = value->speed,
					.intensity = value->intensity,
					.style = (value->style == u"bouncy"_q)
						? ::Ui::ComposerEffectStyle::Bouncy
						: (value->style == u"soft"_q)
						? ::Ui::ComposerEffectStyle::Soft
						: ::Ui::ComposerEffectStyle::Pop,
				};
			}
			::Ui::ComposerEffectsController::SetPlugin(std::move(id), config);
		});
	(void)_host->start(cWorkingDir() + u"plugins/"_q);
}

ApplicationRuntime::~ApplicationRuntime() = default;

}
