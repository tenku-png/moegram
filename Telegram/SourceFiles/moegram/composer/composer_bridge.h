#pragma once

#include "base/basic_types.h"
#include "plugins/plugin_host.h"

namespace Ui {
class InputField;
}

namespace MoeGram::Composer {

void SetPluginEffects(
	QString id,
	std::optional<Plugins::ComposerConfig> value);
void Attach(not_null<::Ui::InputField*> field);
void BeforeSend(not_null<::Ui::InputField*> field);
void EnterEdit(not_null<::Ui::InputField*> field);
void BeforeEditExit(not_null<::Ui::InputField*> field, bool confirmed = true);

}
