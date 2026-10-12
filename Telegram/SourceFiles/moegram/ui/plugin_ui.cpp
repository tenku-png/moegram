#include "moegram/ui/plugin_ui.h"

#include "plugins/plugin_host.h"
#include "moegram/ui/plugin_anchors.h"
#include "window/themes/window_theme.h"

#include <QtCore/QJsonArray>

namespace MoeGram::Ui {
namespace {

struct Store {
	std::map<QString, std::map<QString, QJsonValue>> values;
	rpl::event_stream<QString> changes;
};

Store &Values() {
	static auto result = Store();
	return result;
}

// Plugin palette layers on top of the current theme. The theme palette is
// kept in `base` while any layer is applied, so themes switched or saved
// meanwhile never get plugin colors baked in.
class PaletteOverlay final {
public:
	void refresh();
	[[nodiscard]] QByteArray saved() const {
		return _applied ? _base : style::main_palette::save();
	}

private:
	void watchTheme();

	QByteArray _base;
	bool _applied = false;
	bool _applying = false;
	bool _watching = false;
	rpl::lifetime _lifetime;

};

PaletteOverlay &Palette() {
	static auto result = PaletteOverlay();
	return result;
}

void PaletteOverlay::watchTheme() {
	if (_watching) {
		return;
	}
	_watching = true;
	Window::Theme::Background()->updates(
	) | rpl::filter([=](const Window::Theme::BackgroundUpdate &update) {
		return update.paletteChanged() && !_applying && _applied;
	}) | rpl::on_next([=] {
		// A theme replaced the whole palette, our layer is gone.
		_base = style::main_palette::save();
		_applied = false;
		crl::on_main([=] { refresh(); });
	}, _lifetime);
}

void PaletteOverlay::refresh() {
	auto colors = std::map<QByteArray, QColor>();
	for (const auto &[id, value] : Values().values["palette"]) {
		const auto object = value.toObject();
		for (auto i = object.begin(); i != object.end(); ++i) {
			if (const auto color = ParseColor(i.value())) {
				colors[i.key().toLatin1()] = *color;
			}
		}
	}
	if (colors.empty() && !_applied) {
		return;
	}
	watchTheme();
	if (!_applied) {
		_base = style::main_palette::save();
	} else if (!style::main_palette::load(_base)) {
		return;
	}
	for (const auto &[name, color] : colors) {
		(void)style::main_palette::setColor(
			QLatin1String(name),
			uchar(color.red()),
			uchar(color.green()),
			uchar(color.blue()),
			uchar(color.alpha()));
	}
	_applied = !colors.empty();
	if (!_applied) {
		_base = QByteArray();
	}
	_applying = true;
	Window::Theme::Background()->appliedEditedPalette();
	_applying = false;
}

} // namespace

void SetPluginUi(QString id, QString key, std::optional<QJsonValue> value) {
	auto &values = Values().values[key];
	if (value) {
		values[id] = *value;
	} else if (!values.erase(id)) {
		return;
	}
	if (key == u"palette"_q) {
		Palette().refresh();
	} else if (key.startsWith(u"anchor:"_q)) {
		ApplyAnchor(id, key.mid(7), value);
	}
	Values().changes.fire_copy(key);
}

std::vector<PluginValue> PluginValues(const QString &key) {
	auto result = std::vector<PluginValue>();
	const auto i = Values().values.find(key);
	if (i != end(Values().values)) {
		for (const auto &[id, value] : i->second) {
			result.push_back({ id, value });
		}
	}
	return result;
}

rpl::producer<QString> PluginValueChanges() {
	return Values().changes.events();
}

rpl::producer<> PluginValueChanges(QString key) {
	return Values().changes.events(
	) | rpl::filter(rpl::mappers::_1 == key) | rpl::to_empty;
}

bool InvokePluginCallback(
		const QString &pluginId,
		const QJsonValue &callback,
		QJsonObject args) {
	const auto id = callback.toObject().value(u"callback"_q).toInt();
	return (id > 0)
		&& Plugins::InvokePluginUiEvent(pluginId, id, std::move(args));
}

std::optional<QColor> ParseColor(const QJsonValue &value) {
	const auto text = value.toString();
	if (!text.startsWith('#') || (text.size() != 7 && text.size() != 9)) {
		return std::nullopt;
	}
	auto ok = false;
	const auto rgb = text.mid(1, 6).toUInt(&ok, 16);
	if (!ok) {
		return std::nullopt;
	}
	const auto alpha = (text.size() == 9) ? text.mid(7, 2).toUInt(&ok, 16) : 255U;
	if (!ok) {
		return std::nullopt;
	}
	return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

QByteArray PaletteForSave() {
	return Palette().saved();
}

}
