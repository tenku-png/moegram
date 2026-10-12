#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

namespace MoeGram::Ui {

// Called on the main thread by the plugin host. Keys look like
// "palette", "chat_header", "blur:menu", "menu:drawer", "anchor:chat.top_bar".
void SetPluginUi(QString id, QString key, std::optional<QJsonValue> value);

struct PluginValue {
	QString pluginId;
	QJsonValue value;
};

// Every plugin's current description for a key, in plugin id order.
[[nodiscard]] std::vector<PluginValue> PluginValues(const QString &key);
// Fires the key whenever any plugin changes it.
[[nodiscard]] rpl::producer<QString> PluginValueChanges();
[[nodiscard]] rpl::producer<> PluginValueChanges(QString key);

// Sends a click/event to a plugin callback ({"callback": id} in JSON).
bool InvokePluginCallback(
	const QString &pluginId,
	const QJsonValue &callback,
	QJsonObject args = {});

[[nodiscard]] std::optional<QColor> ParseColor(const QJsonValue &value);

// Palette bytes without plugin overrides, for code that saves themes.
[[nodiscard]] QByteArray PaletteForSave();

}
