#include "plugins/plugin_host.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <QtCore/QThread>
#include <QtCore/QTimer>

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		qFatal("Menu smoke test failed: %s", message);
	}
}

void Write(const QString &path, const QByteArray &source) {
	auto file = QFile(path);
	Require(file.open(QIODevice::WriteOnly), "create plugin");
	Require(file.write(source) == source.size(), "write plugin");
}

}

int main(int argc, char *argv[]) {
	using namespace MoeGram::Plugins;
	auto app = QCoreApplication(argc, argv);
	auto directory = QTemporaryDir();
	Require(directory.isValid(), "temporary directory");
	Write(directory.path() + "/00_faulty.py", R"py(
from base_plugin import BasePlugin, MenuItemData, MenuItemType
from dataclasses import FrozenInstanceError
__id__ = 'faulty_menu'
__name__ = 'Faulty Menu'
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_menu_item(MenuItemData(
            menu_type=MenuItemType.MESSAGE_CONTEXT_MENU,
            item_id='first', text='First', priority=10,
            on_click=self.clicked))
    def clicked(self, context):
        message = context['message']
        assert message.message_id == 51 and message.chat_id == -333
        assert message.sender_id == 777 and message.outgoing is True
        assert message.text == 'snapshot text'
        assert context['dialog_id'] == -333 and context['account'] == 123
        assert not hasattr(message, '__dict__')
        try:
            message.text = 'changed'
            raise AssertionError('mutable message snapshot')
        except FrozenInstanceError:
            pass
        self.show_toast(f'ID {message.message_id}, chat {message.chat_id}')
        self.log('first callback')
        raise RuntimeError('intentional menu exception')
)py");
	Write(directory.path() + "/10_second.py", R"py(
from base_plugin import BasePlugin, MenuItemData, MenuItemType
__id__ = 'second_menu'
__name__ = 'Second Menu'
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_menu_item(MenuItemData(
            menu_type=MenuItemType.MESSAGE_CONTEXT_MENU,
            item_id='second', text='Second', priority=10,
            on_click=self.clicked))
    def clicked(self, context):
        assert context['message'].message_id == 51
        self.log('second callback')
)py");
	Write(directory.path() + "/20_low.py", R"py(
from base_plugin import BasePlugin, MenuItemData, MenuItemType
__id__ = 'low_menu'
__name__ = 'Low Menu'
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_menu_item(MenuItemData(
            menu_type=MenuItemType.MESSAGE_CONTEXT_MENU,
            item_id='low', text='Low', priority=-1,
            on_click=self.clicked))
    def clicked(self, context):
        self.log('low callback')
)py");
	if (argc == 2) {
		Require(QFile::copy(QString::fromLocal8Bit(argv[1]),
			directory.path() + "/30_message_info.py"), "copy message_info plugin");
	}
	auto logs = QStringList();
	auto mutex = std::mutex();
	auto ready = std::condition_variable();
	auto toasts = QStringList();
	auto toastThread = static_cast<QThread*>(nullptr);
	auto host = Host(
		[&](const QString &message) {
			const auto lock = std::lock_guard(mutex);
			logs.push_back(message);
			QTextStream(stdout) << message << Qt::endl;
			ready.notify_all();
		},
		[&](const QString &message) {
			toastThread = QThread::currentThread();
			toasts.push_back(message);
			ready.notify_all();
		});
	Require(host.start(directory.path()), "start host");
	auto items = host.messageMenuItems();
	Require(MessageMenuItems().size() == items.size(),
		"UI registry exposes the loaded menu entries");
	Require(items.size() == (argc == 2 ? 4 : 3), "all menu items registered");
	Require(items[0].text == "First" && items[1].text == "Second",
		"priority ties preserve plugin load order");
	Require(items.back().text == "Low", "lower priority sorts after higher priority");
	if (argc == 2) {
		Require(items[2].text == "Show message info", "sample plugin item registered");
	}
	auto message = MessageEvent();
	message.accountId = 123;
	message.messageId = 51;
	message.chatId = -333;
	message.senderId = 777;
	message.text = "snapshot text";
	message.outgoing = true;
	Require(InvokeMessageMenuItem(items[0].id, message), "queue first callback");
	Require(InvokeMessageMenuItem(items[1].id, message), "queue second callback");
	if (argc == 2) {
		Require(InvokeMessageMenuItem(items[2].id, message), "queue sample callback");
	}
	Require(InvokeMessageMenuItem(items.back().id, message), "queue low callback");
	{
		auto lock = std::unique_lock(mutex);
		Require(ready.wait_for(lock, std::chrono::seconds(5), [&] {
			return logs.join('\n').contains("low callback");
		}), "wait for callbacks and toast delivery");
	}
	auto eventLoop = QEventLoop();
	auto poll = QTimer();
	QObject::connect(&poll, &QTimer::timeout, &eventLoop, [&] {
		if (toasts.size() >= (argc == 2 ? 2 : 1)) {
			eventLoop.quit();
		}
	});
	QTimer::singleShot(5000, &eventLoop, &QEventLoop::quit);
	poll.start(5);
	eventLoop.exec();
	poll.stop();
	Require(toasts.size() >= (argc == 2 ? 2 : 1), "wait for toast delivery");
	Require(toastThread == app.thread(), "toast callback runs on Qt owner thread");
	Require(toasts.contains("ID 51, chat -333"), "toast received stable message IDs");
	if (argc == 2) {
		Require(toasts.contains("Message ID: 51, Chat ID: -333"),
			"real plugin toast includes stable message IDs");
	}
	host.stop();
	Require(host.messageMenuItems().empty(), "menu entries removed after unload");
	Require(!InvokeMessageMenuItem(items[0].id, message),
		"callback rejected after unload");
	const auto text = logs.join('\n');
	Require(text.indexOf("first callback") < text.indexOf("intentional menu exception"),
		"callback exception reported after callback entry");
	Require(text.indexOf("intentional menu exception") < text.indexOf("second callback"),
		"callback exception does not stop later menu actions");
	Require(text.contains("Plugin Host: unloaded faulty_menu"), "plugin unload completed");
	QTextStream(stdout) << "PASS: immutable context DTO, deterministic order, toast UI delivery, exception isolation and unload" << Qt::endl;
}
