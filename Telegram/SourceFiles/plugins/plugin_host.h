#pragma once

#include <QtCore/QString>

#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace MoeGram::Plugins {

enum class EventKind { Message, Edit, Delete };

struct MessageEvent {
	EventKind kind = EventKind::Message;
	quint64 accountId = 0;
	qint64 messageId = 0;
	std::optional<qint64> chatId;
	std::optional<qint64> senderId;
	std::optional<QString> text;
	std::optional<bool> outgoing;
};

struct MessageMenuItem {
	QString id;
	QString text;
	int priority = 0;
};

enum class SettingKind { Header, Switch, Input, Text, Divider, Selector, Error };

struct SettingItem {
	SettingKind kind = SettingKind::Text;
	QString key;
	QString text;
	QString subtext;
	QString value;
	bool checked = false;
	std::vector<QString> choices;
	int selected = 0;
};

struct PluginSettings {
	QString id;
	QString name;
	std::vector<SettingItem> items;
};

enum class PluginAction { Enable, Global, Pin, Remove };

struct PluginInfo {
	QString id;
	QString name;
	QString version;
	QString error;
	QString author;
	QString description;
	QString declared;
	QString detected;
	bool pinned = false;
	bool desiredEnabled = true;
	bool enabled = false;
};

struct UpdateEvent {
	QString name;
	quint64 accountId = 0;
	std::vector<MessageEvent> messages;
	std::optional<int> pts;
	std::optional<int> ptsCount;
	bool native = false;
};

class NativeEventScope final {
public:
	explicit NativeEventScope(bool active);
	~NativeEventScope();
	NativeEventScope(const NativeEventScope &) = delete;
	NativeEventScope &operator=(const NativeEventScope &) = delete;

private:
	bool _active;

};

void Publish(MessageEvent event);
void Publish(UpdateEvent event);
[[nodiscard]] bool ModelEventsEnabled();
using SendCallback = std::function<void(std::optional<QString>)>;
void FilterOutgoing(quint64 accountId, QString text, SendCallback done);

struct RequestEvent {
	QString name;
	quint64 accountId = 0;
	std::optional<bool> offline;
};

struct RequestResultEvent {
	QString name;
	quint64 accountId = 0;
	std::optional<bool> value;
	std::optional<int> errorCode;
	QString errorText;
};

using RequestCallback = std::function<void(std::optional<RequestEvent>)>;
void FilterRequest(RequestEvent event, RequestCallback done);
void Publish(RequestResultEvent event);
[[nodiscard]] bool RequestHooksEnabled();

struct ComposerConfig {
	bool insertion = false;
	bool deletion = false;
	bool caret = false;
	bool send = false;
	bool edit = false;
	double speed = 1.;
	double intensity = 1.;
	QString style;
};

class Host final {
public:
	using Logger = std::function<void(const QString &)>;
	using ComposerCallback = std::function<void(QString, std::optional<ComposerConfig>)>;
	using ToastCallback = std::function<void(const QString &)>;
	using SettingsCallback = std::function<void(std::vector<PluginSettings>)>;
	using PluginsCallback = std::function<void(std::vector<PluginInfo>)>;
	using SettingValue = std::variant<QString, bool, int>;
	using SettingCallback = std::function<void(bool)>;
	using PluginEnabledCallback = std::function<void(
		bool,
		std::vector<PluginInfo>)>;

	explicit Host(Logger logger, ToastCallback toast = nullptr,
		ComposerCallback composer = nullptr);
	~Host();

	Host(const Host &) = delete;
	Host &operator=(const Host &) = delete;

	[[nodiscard]] bool start(const QString &directory);
	void stop();
	[[nodiscard]] bool post(MessageEvent event);
	[[nodiscard]] bool post(UpdateEvent event);
	[[nodiscard]] int loadedCount() const;
	[[nodiscard]] bool filterOutgoing(
		quint64 accountId, QString text, SendCallback done);
	[[nodiscard]] bool filterRequest(RequestEvent event, RequestCallback done);
	[[nodiscard]] bool post(RequestResultEvent event);
	[[nodiscard]] std::vector<MessageMenuItem> messageMenuItems() const;
	[[nodiscard]] bool invokeMessageMenuItem(
		QString id,
		MessageEvent message);
	[[nodiscard]] bool requestSettings(SettingsCallback done);
	[[nodiscard]] bool requestPlugins(PluginsCallback done);
	[[nodiscard]] bool setPluginEnabled(
		QString pluginId,
		bool enabled,
		PluginEnabledCallback done = nullptr);
	[[nodiscard]] bool pluginsEnabled() const;
	[[nodiscard]] bool managePlugin(
		QString pluginId, bool value, PluginAction action,
		PluginEnabledCallback done);
	[[nodiscard]] bool updateSetting(
		QString pluginId,
		QString key,
		SettingValue value,
		SettingCallback done = nullptr);

private:
	struct Private;
	const std::unique_ptr<Private> _private;

};

[[nodiscard]] std::vector<MessageMenuItem> MessageMenuItems();
[[nodiscard]] bool InvokeMessageMenuItem(QString id, MessageEvent message);
[[nodiscard]] bool RequestPluginSettings(Host::SettingsCallback done);
[[nodiscard]] bool RequestPlugins(Host::PluginsCallback done);
[[nodiscard]] bool SetPluginEnabled(
	QString pluginId,
	bool enabled,
	Host::PluginEnabledCallback done = nullptr);
[[nodiscard]] bool PluginsEnabled();
[[nodiscard]] bool ManagePlugin(
	QString pluginId, bool value, PluginAction action,
	Host::PluginEnabledCallback done);
[[nodiscard]] bool UpdatePluginSetting(
	QString pluginId,
	QString key,
	Host::SettingValue value,
	Host::SettingCallback done = nullptr);

}
