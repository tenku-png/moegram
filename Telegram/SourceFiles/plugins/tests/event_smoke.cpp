#include "plugins/plugin_host.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		qFatal("Event smoke test failed: %s", message);
	}
}

void Write(const QString &path, const QByteArray &source) {
	auto file = QFile(path);
	Require(file.open(QIODevice::WriteOnly), "create plugin");
	Require(file.write(source) == source.size(), "write plugin");
}

}

int main(int argc, char *argv[]) {
	auto app = QCoreApplication(argc, argv);
	auto directory = QTemporaryDir();
	Require(directory.isValid(), "temporary directory");
	Write(directory.path() + "/00_error.py", R"py(
from base_plugin import BasePlugin
__id__ = 'faulty'
__name__ = 'Faulty'
class Plugin(BasePlugin):
    def on_message(self, message):
        self.log('faulty create')
        raise RuntimeError('intentional create exception')
    def on_message_edit(self, message):
        self.log('faulty edit')
        raise SystemExit('intentional edit exception')
    def on_message_delete(self, message):
        self.log('faulty delete')
        raise KeyboardInterrupt('intentional delete exception')
)py");
	Write(directory.path() + "/10_healthy.py", R"py(
from base_plugin import BasePlugin, API_VERSION, SDK_VERSION
from dataclasses import FrozenInstanceError
__id__ = 'healthy'
__name__ = 'Healthy'
__moegram_api_version__ = '1'
class Plugin(BasePlugin):
    def on_message(self, message):
        assert API_VERSION == 1 and SDK_VERSION == '0.6.0'
        assert message.account_id == 123 and message.chat_id == -1000000000456
        assert message.sender_id == 789 and message.outgoing is False
        assert message.id == message.getId() == 42
        assert message.getDialogId() == message.chat_id and message.isOut() is False
        assert message.messageText == 'Привет\x00🌍'
        try:
            message.text = 'mutated'
            raise AssertionError('mutable DTO')
        except FrozenInstanceError:
            pass
        assert not hasattr(message, '__dict__')
        self.saved = message
        self.log('healthy create')
    def on_message_edit(self, message):
        assert self.saved.text == 'Привет\x00🌍'
        assert message.text == 'edited' and message.message_id == self.saved.message_id
        self.log('healthy edit')
    def on_message_delete(self, message):
        if message.message_id == 42:
            assert message.text == 'edited'
            assert self.saved.text == 'Привет\x00🌍'
            self.log('healthy delete')
        else:
            assert message.chat_id is None and message.sender_id is None
            assert message.text is None and message.outgoing is None
            self.log('healthy unknown delete')
    def on_plugin_unload(self):
        self.log('healthy unloaded')
)py");
	Write(directory.path() + "/20_incompatible.py", R"py(
__id__ = 'incompatible'
__name__ = 'Incompatible'
__moegram_api_version__ = '2'
raise RuntimeError('incompatible plugin executed')
)py");
	if (argc == 2) {
		Require(QFile::copy(QString::fromLocal8Bit(argv[1]),
			directory.path() + "/30_event_logger.py"), "copy real event logger");
	}
	auto mutex = std::mutex();
	auto ready = std::condition_variable();
	auto logs = QStringList();
	auto host = MoeGram::Plugins::Host([&](const QString &message) {
		const auto lock = std::lock_guard(mutex);
		logs.push_back(message);
		QTextStream(stdout) << message << Qt::endl;
		ready.notify_all();
	});
	const auto waitFor = [&](const QString &marker) {
		auto lock = std::unique_lock(mutex);
		Require(ready.wait_for(lock, std::chrono::seconds(5), [&] {
			return logs.join('\n').contains(marker);
		}), "callback timeout");
	};
	Require(host.start(directory.path()), "start");
	Require(host.loadedCount() == (argc == 2 ? 3 : 2), "incompatible SDK rejected before exec");
	auto event = MoeGram::Plugins::MessageEvent();
	event.accountId = 123;
	event.messageId = 42;
	event.chatId = -1000000000456LL;
	event.senderId = 789;
	event.text = QString::fromUtf8("Привет\0🌍", 17);
	event.outgoing = false;
	Require(host.post(event), "post create");
	event.kind = MoeGram::Plugins::EventKind::Edit;
	event.text = "edited";
	Require(host.post(event), "post edit");
	event.kind = MoeGram::Plugins::EventKind::Delete;
	Require(host.post(event), "post delete");
	event = {};
	event.kind = MoeGram::Plugins::EventKind::Delete;
	event.messageId = 999;
	Require(host.post(event), "post unknown delete");
	waitFor("healthy unknown delete");
	host.stop();
	Require(!host.post(event), "events rejected after unload");
	const auto joined = logs.join('\n');
	Require(joined.contains("intentional create exception"), "create exception caught");
	Require(joined.contains("intentional edit exception"), "edit SystemExit caught");
	Require(joined.contains("intentional delete exception"), "delete KeyboardInterrupt caught");
	if (argc == 2) {
		Require(joined.contains("plugin.event_logger [INFO] create"), "real logger create");
		Require(joined.contains("plugin.event_logger [INFO] edit"), "real logger edit");
		Require(joined.contains("plugin.event_logger [INFO] delete"), "real logger delete");
	}
	Require(!joined.contains("incompatible plugin executed"), "API version checked before execution");
	Require(!joined.contains("on_message failed: healthy"), "healthy create passed");
	Require(!joined.contains("on_message_edit failed: healthy"), "healthy edit passed");
	Require(!joined.contains("on_message_delete failed: healthy"), "healthy delete passed");
	Require(joined.indexOf("faulty create") < joined.indexOf("healthy create"), "plugin order");
	Require(joined.indexOf("healthy create") < joined.indexOf("healthy edit"), "create before edit");
	Require(joined.indexOf("healthy edit") < joined.indexOf("healthy delete"), "edit before delete");
	Require(joined.indexOf("healthy delete") < joined.indexOf("healthy unloaded"), "unload after callbacks");

	auto slow = QTemporaryDir();
	Write(slow.path() + "/slow.py", R"py(
from base_plugin import BasePlugin
import time
__id__ = 'slow'
__name__ = 'Slow'
class Plugin(BasePlugin):
    def on_message(self, message):
        self.log('slow started')
        time.sleep(0.3)
        self.log('slow finished')
    def on_message_edit(self, message):
        self.log('cancelled event executed')
    def on_plugin_unload(self):
        self.log('slow unloaded')
)py");
	Require(host.start(slow.path()), "restart");
	event.kind = MoeGram::Plugins::EventKind::Message;
	event.outgoing = false;
	Require(host.post(event), "slow callback");
	waitFor("slow started");
	auto loop = QEventLoop();
	auto timerFired = false;
	QTimer::singleShot(10, &loop, [&] {
		timerFired = true;
		loop.quit();
	});
	loop.exec();
	{
		const auto lock = std::lock_guard(mutex);
		Require(timerFired && !logs.join('\n').contains("slow finished"), "Qt loop continues during callback");
	}
	event.kind = MoeGram::Plugins::EventKind::Edit;
	for (auto i = 0; i != 4096; ++i) {
		Require(host.post(event), "bounded pending queue");
	}
	Require(!host.post(event), "queue overflow rejected");
	host.stop();
	Require(!logs.join('\n').contains("cancelled event executed"), "pending events discarded on unload");
	Require(logs.join('\n').contains("slow unloaded"), "slow unload");
	QTextStream(stdout) << "PASS: create/edit/delete, DTO, versioning, ordering, exceptions, Qt loop and unload" << Qt::endl;
}
