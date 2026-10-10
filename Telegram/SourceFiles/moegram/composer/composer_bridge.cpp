#include "moegram/composer/composer_bridge.h"

#include "ui/widgets/fields/composer_effects.h"
#include "ui/widgets/fields/input_field.h"

namespace MoeGram::Composer {
namespace {

::Ui::ComposerCurve Curve(const QString &name) {
	using Curve = ::Ui::ComposerCurve;
	return (name == u"smooth"_q) ? Curve::Smooth
		: (name == u"linear"_q) ? Curve::Linear
		: (name == u"cubic"_q) ? Curve::Cubic
		: (name == u"back"_q) ? Curve::Back
		: (name == u"elastic"_q) ? Curve::Elastic
		: (name == u"bounce"_q) ? Curve::Bounce
		: Curve::Auto;
}

} // namespace

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
			.motion = (value->motion == u"drop"_q)
				? ::Ui::ComposerMotion::Drop
				: (value->motion == u"zoom"_q)
				? ::Ui::ComposerMotion::Zoom
				: (value->motion == u"spin"_q)
				? ::Ui::ComposerMotion::Spin
				: ::Ui::ComposerMotion::Pop,
			.curve = Curve(value->curve),
			.dust = (value->shatter == u"dust"_q),
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
