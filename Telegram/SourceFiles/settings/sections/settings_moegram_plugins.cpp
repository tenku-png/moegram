#include "settings/sections/settings_moegram_plugins.h"

#include "plugins/plugin_host.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"

#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QPointer>

namespace Settings {
namespace {

void RenderSettings(
	not_null<Ui::VerticalLayout*> content,
	std::vector<MoeGram::Plugins::PluginSettings> pages,
	const QString &pluginId);
void RequestPluginList(QPointer<Ui::VerticalLayout> content);
void ShowPluginSettings(
	QPointer<Ui::VerticalLayout> content,
	QString pluginId);

void AddText(
		not_null<Ui::VerticalLayout*> content,
		const QString &text,
		const QString &subtext = QString()) {
	content->add(object_ptr<Ui::FlatLabel>(
		content,
		text,
		st::defaultFlatLabel),
		st::defaultSubsectionTitlePadding,
		style::al_justify);
	if (!subtext.isEmpty()) {
		content->add(object_ptr<Ui::FlatLabel>(
			content,
			subtext,
			st::defaultFlatLabel),
			st::defaultSubsectionTitlePadding,
			style::al_justify);
	}
}

void RefreshAfterChange(
		QPointer<Ui::VerticalLayout> content,
		QString pluginId,
		bool reload) {
	if (!reload || !content) {
		return;
	}
	ShowPluginSettings(content, std::move(pluginId));
}

void AddSettingRows(
		not_null<Ui::VerticalLayout*> content,
		QPointer<Ui::VerticalLayout> weakContent,
		const MoeGram::Plugins::PluginSettings &page) {
	for (const auto &item : page.items) {
		switch (item.kind) {
		case MoeGram::Plugins::SettingKind::Header:
			Ui::AddSubsectionTitle(content, rpl::single(item.text));
			break;
		case MoeGram::Plugins::SettingKind::Divider:
			if (item.text.isEmpty()) {
				Ui::AddDivider(content);
			} else {
				Ui::AddDividerText(content, rpl::single(item.text));
			}
			break;
		case MoeGram::Plugins::SettingKind::Text:
			AddText(content, item.text, item.subtext);
			break;
		case MoeGram::Plugins::SettingKind::Input: {
			Ui::AddSkip(content);
			Ui::AddSubsectionTitle(content, rpl::single(item.text));
			auto field = content->add(object_ptr<Ui::InputField>(
				content,
				st::defaultInputField,
				Ui::InputField::Mode::SingleLine,
				rpl::single(QString()),
				item.value),
				st::defaultSubsectionTitlePadding,
				style::al_justify);
			const auto pluginId = page.id;
			const auto key = item.key;
			field->changes() | rpl::on_next([=] {
				const auto value = field->getLastText();
				(void)MoeGram::Plugins::UpdatePluginSetting(
					pluginId,
					key,
					value,
					[weakContent, pluginId](bool reload) {
						RefreshAfterChange(weakContent, pluginId, reload);
					});
			}, field->lifetime());
			if (!item.subtext.isEmpty()) {
				AddText(content, item.subtext);
			}
		} break;
		case MoeGram::Plugins::SettingKind::Switch: {
			auto button = content->add(object_ptr<Ui::SettingsButton>(
				content,
				rpl::single(item.text),
				st::settingsButtonNoIcon),
				st::defaultSubsectionTitlePadding,
				style::al_justify);
			button->toggleOn(rpl::single(item.checked));
			const auto pluginId = page.id;
			const auto key = item.key;
			button->toggledChanges() | rpl::on_next([=](bool checked) {
				(void)MoeGram::Plugins::UpdatePluginSetting(
					pluginId,
					key,
					checked,
					[weakContent, pluginId](bool reload) {
						RefreshAfterChange(weakContent, pluginId, reload);
					});
			}, button->lifetime());
			if (!item.subtext.isEmpty()) {
				AddText(content, item.subtext);
			}
		} break;
		case MoeGram::Plugins::SettingKind::Error:
			AddText(content, item.text);
			break;
		}
	}
}

void RenderSettings(
		not_null<Ui::VerticalLayout*> content,
		std::vector<MoeGram::Plugins::PluginSettings> pages,
		const QString &pluginId) {
	content->clear();
	auto back = content->add(object_ptr<Ui::SettingsButton>(
		content,
		rpl::single(u"Back to plugins"_q),
		st::settingsButtonNoIcon));
	const auto weakContent = QPointer<Ui::VerticalLayout>(content);
	back->addClickHandler([weakContent] {
		RequestPluginList(weakContent);
	});
	const auto found = ranges::find(pages, pluginId, &MoeGram::Plugins::PluginSettings::id);
	if (found == pages.end()) {
		AddText(content, u"This plugin is disabled or unavailable."_q);
	} else if (found->items.empty()) {
		Ui::AddSubsectionTitle(content, rpl::single(found->name));
		AddText(content, u"This plugin has no settings."_q);
	} else {
		const auto startsWithOwnHeader = (found->items.front().kind
			== MoeGram::Plugins::SettingKind::Header)
			&& (found->items.front().text == found->name);
		if (!startsWithOwnHeader) {
			Ui::AddSubsectionTitle(content, rpl::single(found->name));
		}
		AddSettingRows(content, weakContent, *found);
	}
	content->resizeToWidth(content->width());
}

void RenderPluginList(
		not_null<Ui::VerticalLayout*> content,
		std::vector<MoeGram::Plugins::PluginInfo> plugins) {
	content->clear();
	if (plugins.empty()) {
		AddText(content, u"No plugins found in the plugins/ directory."_q);
		content->resizeToWidth(content->width());
		return;
	}
	AddText(content, u"Choose a plugin, enable or disable it, or edit its settings."_q);
	const auto weakContent = QPointer<Ui::VerticalLayout>(content);
	for (auto i = plugins.begin(); i != plugins.end(); ++i) {
		const auto &plugin = *i;
		Ui::AddSkip(content);
		if (i != plugins.begin()) {
			Ui::AddDivider(content);
		}
		auto toggle = content->add(object_ptr<Ui::SettingsButton>(
			content,
			rpl::single(plugin.name),
			st::settingsButtonNoIcon),
			st::defaultSubsectionTitlePadding,
			style::al_justify);
		toggle->toggleOn(rpl::single(plugin.enabled));
		const auto pluginId = plugin.id;
		toggle->toggledChanges() | rpl::on_next([=](bool enabled) {
			toggle->setToggleLocked(true);
			if (!MoeGram::Plugins::SetPluginEnabled(
					pluginId,
					enabled,
					[weakContent](bool, std::vector<MoeGram::Plugins::PluginInfo> updated) {
						if (weakContent) {
							RenderPluginList(weakContent, std::move(updated));
						}
					})) {
				RequestPluginList(weakContent);
			}
		}, toggle->lifetime());
		AddText(content, u"Version %1"_q.arg(plugin.version));
		if (!plugin.error.isEmpty()) {
			AddText(content, plugin.error);
		}
		if (plugin.enabled) {
			auto edit = content->add(object_ptr<Ui::SettingsButton>(
				content,
				rpl::single(u"Edit settings"_q),
				st::settingsButtonNoIcon),
				st::defaultSubsectionTitlePadding,
				style::al_justify);
			edit->addClickHandler([weakContent, pluginId] {
				ShowPluginSettings(weakContent, pluginId);
			});
		}
	}
	content->resizeToWidth(content->width());
}

void RequestPluginList(QPointer<Ui::VerticalLayout> content) {
	if (!content || !MoeGram::Plugins::RequestPlugins(
			[content](std::vector<MoeGram::Plugins::PluginInfo> plugins) {
				if (content) {
					RenderPluginList(content, std::move(plugins));
				}
			})) {
		if (content) {
			content->clear();
			AddText(content, u"Plugin Host is unavailable."_q);
		}
	}
}

void ShowPluginSettings(
		QPointer<Ui::VerticalLayout> content,
		QString pluginId) {
	if (!content || !MoeGram::Plugins::RequestPluginSettings(
			[content, pluginId](std::vector<MoeGram::Plugins::PluginSettings> pages) {
				if (content) {
					RenderSettings(content, std::move(pages), pluginId);
				}
			})) {
		if (content) {
			content->clear();
			AddText(content, u"This plugin is currently unavailable."_q);
		}
	}
}

} // namespace

MoeGramPlugins::MoeGramPlugins(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MoeGramPlugins::title() {
	return rpl::single(u"MoeGram Plugins"_q);
}

void MoeGramPlugins::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	AddText(content, u"Loading plugins…"_q);
	Ui::ResizeFitChild(this, content);
	RequestPluginList(content);
}

Type MoeGramPluginsId() {
	return MoeGramPlugins::Id();
}

} // namespace Settings
