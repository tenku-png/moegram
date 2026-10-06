#include "moegram/composer/composer_bridge.h"

#include "ui/widgets/fields/composer_effects.h"
#include "ui/widgets/fields/input_field.h"

namespace MoeGram::Composer {

void SetPluginEffects(
		QString id,
		std::optional<Plugins::ComposerConfig> value) {
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
}

void Attach(not_null<::Ui::InputField*> field) {
	field->enableComposerEffects();
}

void BeforeSend(not_null<::Ui::InputField*> field) {
	field->prepareComposerTransition(::Ui::ComposerTransition::Send);
}

void EnterEdit(not_null<::Ui::InputField*> field) {
	field->prepareComposerTransition(::Ui::ComposerTransition::EditEnter);
	field->finishComposerTransition();
}

void BeforeEditExit(not_null<::Ui::InputField*> field, bool confirmed) {
	if (confirmed) {
		field->prepareComposerTransition(::Ui::ComposerTransition::EditExit);
	}
}

}
