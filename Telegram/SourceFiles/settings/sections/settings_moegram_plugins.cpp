#include "settings/sections/settings_moegram_plugins.h"

#include "plugins/plugin_host.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"

#include "ui/painter.h"
#include "styles/style_menu_icons.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <algorithm>
#include "styles/style_settings.h"
#include "styles/style_settings_moegram_plugins.h"
#include "styles/style_widgets.h"

#include <QtCore/QPointer>

namespace Settings {
namespace {

void RenderSettings(
	not_null<Ui::VerticalLayout*> content,
	std::vector<MoeGram::Plugins::PluginSettings> pages,
	const QString &pluginId);
class PluginPageFactory;

class PluginSettingsPage final : public AbstractSection {
public:
	PluginSettingsPage(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		QString pluginId,
		QString name,
		Type type);
	rpl::producer<QString> title() override;
	Type id() const override;

private:
	QString _name;
	Type _type;

};

class PluginPageFactory final
: public AbstractSectionFactory
, public std::enable_shared_from_this<PluginPageFactory> {
public:
	PluginPageFactory(QString pluginId, QString name);
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue) const override;

private:
	QString _pluginId;
	QString _name;

};

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
	const auto section = static_cast<AbstractSection*>(content->parentWidget());
	const auto controller = section->controller();
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
			auto button = AddButtonWithIcon(
				content,
				rpl::single(item.text),
				st::settingsButtonNoIcon);
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
		case MoeGram::Plugins::SettingKind::Selector: {
			if (item.choices.empty() || item.selected < 0
				|| item.selected >= item.choices.size()) {
				break;
			}
			const auto label = std::make_shared<rpl::variable<QString>>(
				item.choices[item.selected]);
			const auto selected = std::make_shared<int>(item.selected);
			const auto button = AddButtonWithLabel(
				content,
				rpl::single(item.text),
				label->value(),
				st::settingsButtonNoIcon);
			const auto pluginId = page.id;
			button->setClickedCallback([=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					SingleChoiceBox(box, {
						.title = rpl::single(item.text),
						.options = item.choices,
						.initialSelection = *selected,
						.callback = [=](int index) {
							if (!weakContent) {
								return;
							}
							*selected = index;
							*label = item.choices[index];
							(void)MoeGram::Plugins::UpdatePluginSetting(
								pluginId,
								item.key,
								index,
								[weakContent, pluginId](bool reload) {
									RefreshAfterChange(weakContent, pluginId, reload);
								});
						},
					});
				}));
			});
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
	const auto weakContent = QPointer<Ui::VerticalLayout>(content);
	const auto found = ranges::find(pages, pluginId, &MoeGram::Plugins::PluginSettings::id);
	if (found == pages.end()) {
		AddText(content, u"This plugin is disabled or unavailable."_q);
	} else if (found->items.empty()) {
		Ui::AddSubsectionTitle(content, rpl::single(found->name));
		AddText(content, u"This plugin has no settings."_q);
	} else {
		AddSettingRows(content, weakContent, *found);
	}
	content->resizeToWidth(content->width());
}

class PluginToggle final : public Ui::Checkbox {
public:
	PluginToggle(QWidget *parent, QString name, bool checked)
	: Ui::Checkbox(parent, QString(), checked, st::pluginManagerToggle, st::defaultToggle)
	, _name(std::move(name)) {
		setFocusPolicy(Qt::StrongFocus);
	}

	QString accessibilityName() override { return _name; }

private:
	QString _name;

};

class PluginCard final : public Ui::AbstractButton {
public:
	PluginCard(QWidget *parent, MoeGram::Plugins::PluginInfo plugin,
			Fn<void(MoeGram::Plugins::PluginAction, bool)> change,
			Fn<void()> info, Fn<void()> settings, Fn<void()> remove)
	: Ui::AbstractButton(parent)
	, _plugin(std::move(plugin)) {
		setClickedCallback(info);
		setAccessibleName(_plugin.name);
		setFocusPolicy(Qt::StrongFocus);
		_toggle = Ui::CreateChild<PluginToggle>(this, u"Enable %1"_q.arg(_plugin.name),
			_plugin.desiredEnabled);
		_toggle->setAccessibleName(u"Enable %1"_q.arg(_plugin.name));
		_toggle->checkedChanges() | rpl::on_next([=](bool checked) {
			change(MoeGram::Plugins::PluginAction::Enable, checked);
		}, lifetime());
		const auto action = [&](const style::icon &icon, QString label, Fn<void()> callback) {
			const auto button = Ui::CreateChild<Ui::IconButton>(this, st::pluginCardAction);
			button->setIconOverride(&icon);
			button->setToolTip(label);
			button->setAccessibleName(label);
			button->setFocusPolicy(Qt::StrongFocus);
			button->setClickedCallback(std::move(callback));
			_actions.push_back(button);
			return button;
		};
		action(_plugin.pinned ? st::menuIconUnpin : st::menuIconPin,
			_plugin.pinned ? u"Unpin"_q : u"Pin"_q,
			[=] { change(MoeGram::Plugins::PluginAction::Pin, !_plugin.pinned); });
		action(st::menuIconInfo, u"Permissions and details"_q, info);
		const auto settingsButton = action(st::menuIconSettings, u"Settings"_q, settings);
		settingsButton->setDisabled(!_plugin.enabled);
		if (!_plugin.enabled) settingsButton->setToolTip(u"Enable the plugin to open settings"_q);
		action(st::menuIconDeleteAttention, u"Remove plugin"_q, remove);
	}

protected:
	int resizeGetHeight(int width) override {
		const auto padding = st::pluginManagerPadding;
		_toggle->moveToRight(padding, st::pluginCardTitleTop, width);
		for (auto i = 0; i != int(_actions.size()); ++i) {
			_actions[i]->moveToLeft(padding + st::pluginCardIconSize
				+ st::pluginManagerGap + i * st::pluginCardAction.width,
				st::pluginCardActionsTop, width);
		}
		return st::pluginCardHeight;
	}

	void paintEvent(QPaintEvent *) override {
		auto p = Painter(this);
		p.fillRect(rect(), isOver() ? st::windowBgOver : st::windowBg);
		const auto padding = st::pluginManagerPadding;
		const auto size = st::pluginCardIconSize;
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawEllipse(QRect(padding, st::pluginCardTitleTop, size, size));
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFgActive);
		p.drawText(QRect(padding, st::pluginCardTitleTop, size, size),
			Qt::AlignCenter, (_plugin.name.front().isLetterOrNumber()
				? _plugin.name.left(1) : _plugin.id.left(1)).toUpper());
		const auto left = padding + size + st::pluginManagerGap;
		const auto available = width() - left - padding;
		const auto text = [&](int top, QString value, bool secondary, int reserve = 0) {
			p.setFont(secondary ? st::normalFont : st::semiboldFont);
			p.setPen(secondary ? st::windowSubTextFg : st::windowFg);
			p.drawText(left, top + p.fontMetrics().ascent(),
				p.fontMetrics().elidedText(value, Qt::ElideRight, std::max(0, available - reserve)));
		};
		text(st::pluginCardTitleTop, _plugin.name, false, _toggle->width());
		text(st::pluginCardMetaTop, _plugin.version
			+ (_plugin.author.isEmpty() ? QString() : u" · "_q + _plugin.author), true);
		text(st::pluginCardDescriptionTop, !_plugin.error.isEmpty() ? _plugin.error
			: _plugin.description.isEmpty() ? u"No description provided"_q : _plugin.description, true);
		if (hasFocus()) {
			p.setPen(st::windowActiveTextFg);
			p.setBrush(Qt::NoBrush);
			p.drawRect(rect().adjusted(0, 0, -st::lineWidth, -st::lineWidth));
		}
	}

private:
	MoeGram::Plugins::PluginInfo _plugin;
	Ui::Checkbox *_toggle = nullptr;
	std::vector<Ui::IconButton*> _actions;

};

class PluginManager final : public Ui::RpWidget {
public:
	PluginManager(QWidget *parent, not_null<AbstractSection*> section)
	: Ui::RpWidget(parent)
	, _section(section) {
		_layout = Ui::CreateChild<Ui::VerticalLayout>(this);
		_global = AddButtonWithIcon(_layout, rpl::single(u"Enable Plugins"_q), st::settingsButtonNoIcon);
		_global->toggleOn(_enabled.value());
		_global->toggledChanges() | rpl::on_next([=](bool enabled) {
			if (!_refreshing) change(QString(), MoeGram::Plugins::PluginAction::Global, enabled);
		}, lifetime());
		_search = _layout->add(object_ptr<Ui::InputField>(_layout, st::defaultInputField,
			Ui::InputField::Mode::SingleLine, rpl::single(u"Search plugins"_q)),
			st::defaultSubsectionTitlePadding);
		_search->changes() | rpl::on_next([=] { render(); }, lifetime());
		const auto tabs = _layout->add(object_ptr<Ui::SettingsSlider>(_layout, st::settingsSlider),
			st::defaultSubsectionTitlePadding);
		tabs->addSection(u"All"_q);
		tabs->addSection(u"Enabled"_q);
		tabs->setActiveSectionFast(0);
		tabs->sectionActivated() | rpl::on_next([=](int index) {
			_filter = index;
			render();
		}, lifetime());
		_list = _layout->add(object_ptr<Ui::VerticalLayout>(_layout));
		AddText(_list, u"Loading plugins…"_q);
		Ui::ResizeFitChild(this, _layout);
		refresh();
	}

private:
	void refresh() {
		const auto weak = QPointer<PluginManager>(this);
		if (!MoeGram::Plugins::RequestPlugins([weak](auto plugins) {
			if (weak) weak->accept(std::move(plugins));
		})) {
			_list->clear();
			AddText(_list, u"Plugin Host is unavailable."_q);
		}
	}

	void accept(std::vector<MoeGram::Plugins::PluginInfo> plugins) {
		_plugins = std::move(plugins);
		_refreshing = true;
		_global->setToggleLocked(false);
		_enabled.force_assign(MoeGram::Plugins::PluginsEnabled());
		_refreshing = false;
		_list->setEnabled(true);
		render();
	}

	void change(QString id, MoeGram::Plugins::PluginAction action, bool value) {
		const auto weak = QPointer<PluginManager>(this);
		_global->setToggleLocked(true);
		_list->setEnabled(false);
		if (!MoeGram::Plugins::ManagePlugin(id, value, action,
				[weak](bool success, auto plugins) {
					if (!weak) return;
					weak->accept(std::move(plugins));
					if (!success) weak->controller()->showToast(u"Could not update plugin. Check the client log."_q);
				})) {
			_global->setToggleLocked(false);
			_list->setEnabled(true);
			controller()->showToast(u"Plugin Host is busy. Try again."_q);
			refresh();
		}
	}

	Window::SessionController *controller() const { return _section->controller(); }

	void details(const MoeGram::Plugins::PluginInfo &plugin) {
		controller()->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(u"Permissions — %1"_q.arg(plugin.name)));
			const auto line = [=](QString value, bool warning = false) {
				const auto label = box->addRow(object_ptr<Ui::FlatLabel>(box,
					value, st::boxLabel), st::boxRowPadding, style::al_justify);
				if (warning) label->setTextColorOverride(st::attentionButtonFg->c);
			};
			line(plugin.version + (plugin.author.isEmpty() ? QString() : u" · "_q + plugin.author));
			if (!plugin.description.isEmpty()) line(plugin.description);
			line(u"Plugin ID: %1"_q.arg(plugin.id));
			line(u"Declared capabilities\n%1"_q.arg(plugin.declared.isEmpty() ? u"Not declared"_q : plugin.declared));
			line(u"Detected API references\n%1"_q.arg(plugin.detected.isEmpty() ? u"None detected"_q : plugin.detected));
			const auto permissions = MoeGram::Plugins::PluginPermissions(plugin.id);
			for (const auto &permission : permissions) {
				const auto label = permission.capability == u"messages.read"_q ? u"Read messages"_q
					: permission.capability == u"messages.send"_q ? u"Send messages"_q
					: permission.capability == u"messages.modify"_q ? u"Modify messages"_q
					: permission.capability == u"ui"_q ? u"UI access"_q : u"Plugin storage"_q;
				const auto toggle = box->addRow(object_ptr<Ui::Checkbox>(
					box, label + (permission.allowed ? u" — Allowed"_q : u" — Denied"_q), permission.allowed), st::boxRowPadding);
				toggle->checkedChanges() | rpl::on_next([=](bool allowed) {
					if (!MoeGram::Plugins::SetPluginPermission(plugin.id, permission.capability, allowed)) {
						toggle->setDisabled(true);
						line(u"Could not save permission. Check the client log."_q, true);
					} else {
						toggle->setText(label + (allowed ? u" — Allowed"_q : u" — Denied"_q));
					}
				}, toggle->lifetime());
			}
			if (!permissions.empty() && permissions.front().legacy) {
				line(u"Legacy compatibility: existing MoeGram API access was allowed. You can revoke it here."_q);
			}
			line(u"Changes apply immediately. After allowing a previously denied API, reload the plugin if it needs to register hooks again."_q);
			line(u"Network · Not sandboxed\nUnsafe/native · Not sandboxed"_q, true);
			line(u"Python plugins run inside MoeGram and can access files, the network and native libraries directly."_q);
			box->addButton(rpl::single(u"Close"_q), [=] { box->closeBox(); });
		}));
	}

	void remove(const MoeGram::Plugins::PluginInfo &plugin) {
		const auto weak = QPointer<PluginManager>(this);
		controller()->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(u"Remove %1?"_q.arg(plugin.name)));
			box->addRow(object_ptr<Ui::FlatLabel>(box,
				u"The plugin will be unloaded and its Python file deleted. Saved settings will be kept."_q,
				st::boxLabel), st::boxRowPadding, style::al_justify);
			box->addButton(rpl::single(u"Remove"_q), [=] {
				box->closeBox();
				if (weak) weak->change(plugin.id, MoeGram::Plugins::PluginAction::Remove, false);
			}, st::attentionBoxButton);
			box->addButton(rpl::single(u"Cancel"_q), [=] { box->closeBox(); });
		}));
	}

	void render() {
		_list->clear();
		auto sorted = _plugins;
		std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
			if (a.pinned != b.pinned) return a.pinned;
			const auto compare = a.name.compare(b.name, Qt::CaseInsensitive);
			return compare ? compare < 0 : a.id < b.id;
		});
		const auto query = _search->getLastText().trimmed();
		auto count = 0;
		for (const auto &plugin : sorted) {
			if (_filter == 1 && !plugin.enabled) continue;
			if (!(plugin.name + u" "_q + plugin.id + u" "_q + plugin.author + u" "_q
				+ plugin.description).contains(query, Qt::CaseInsensitive)) continue;
			if (count++) Ui::AddDivider(_list);
			_list->add(object_ptr<PluginCard>(_list, plugin,
				[=](MoeGram::Plugins::PluginAction action, bool value) { change(plugin.id, action, value); },
				[=] { details(plugin); },
				[=] { _section->showOther(std::make_shared<PluginPageFactory>(plugin.id, plugin.name)); },
				[=] { remove(plugin); }));
		}
		if (!count) AddText(_list, _plugins.empty() ? u"No plugins installed."_q : u"No matching plugins."_q);
	}

	not_null<AbstractSection*> _section;
	Ui::VerticalLayout *_layout = nullptr;
	Ui::VerticalLayout *_list = nullptr;
	Ui::SettingsButton *_global = nullptr;
	Ui::InputField *_search = nullptr;
	std::vector<MoeGram::Plugins::PluginInfo> _plugins;
	int _filter = 0;
	bool _refreshing = false;
	rpl::variable<bool> _enabled = true;

};

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

PluginSettingsPage::PluginSettingsPage(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		QString pluginId,
		QString name,
		Type type)
: AbstractSection(parent, controller)
, _name(std::move(name))
, _type(std::move(type)) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	AddText(content, u"Loading settings…"_q);
	Ui::ResizeFitChild(this, content);
	ShowPluginSettings(content, std::move(pluginId));
}

rpl::producer<QString> PluginSettingsPage::title() {
	return rpl::single(_name);
}

Type PluginSettingsPage::id() const {
	return _type;
}

PluginPageFactory::PluginPageFactory(QString pluginId, QString name)
: _pluginId(std::move(pluginId))
, _name(std::move(name)) {
}

object_ptr<AbstractSection> PluginPageFactory::create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue) const {
	return object_ptr<PluginSettingsPage>(parent, controller, _pluginId, _name,
		std::const_pointer_cast<PluginPageFactory>(shared_from_this()));
}

} // namespace

MoeGramPlugins::MoeGramPlugins(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MoeGramPlugins::title() {
	return rpl::single(u"Plugins"_q);
}

void MoeGramPlugins::setupContent() {
	const auto content = Ui::CreateChild<PluginManager>(this, this);
	Ui::ResizeFitChild(this, content);
}

Type MoeGramPluginsId() {
	return MoeGramPlugins::Id();
}

} // namespace Settings
