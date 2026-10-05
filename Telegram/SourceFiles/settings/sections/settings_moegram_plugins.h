#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Settings {

class MoeGramPlugins final : public Section<MoeGramPlugins> {
public:
	MoeGramPlugins(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

[[nodiscard]] Type MoeGramPluginsId();

} // namespace Settings
