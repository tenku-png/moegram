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

[[nodiscard]] QString MoeGramPluginsPath();

// Copies a plugin file into the plugins folder as <id>.py and loads it.
void InstallMoeGramPlugin(
	not_null<Window::SessionController*> controller,
	const QString &path,
	const QString &id,
	bool enable);

// Opens a file dialog and shows the plugin install box for the chosen file.
void ChooseMoeGramPlugin(not_null<Window::SessionController*> controller);

} // namespace Settings
