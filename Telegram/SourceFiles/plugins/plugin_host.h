#pragma once

#include <QtCore/QString>

#include <functional>
#include <memory>
#include <optional>
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

class Host final {
public:
	using Logger = std::function<void(const QString &)>;
	using ToastCallback = std::function<void(const QString &)>;

	explicit Host(Logger logger, ToastCallback toast = nullptr);
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

private:
	struct Private;
	const std::unique_ptr<Private> _private;

};

[[nodiscard]] std::vector<MessageMenuItem> MessageMenuItems();
[[nodiscard]] bool InvokeMessageMenuItem(QString id, MessageEvent message);

}
