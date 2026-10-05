#include "plugins/plugin_host.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		qFatal("Compatibility smoke failed: %s", message);
	}
}

void Write(const QString &path, const QByteArray &source) {
	auto file = QFile(path);
	Require(file.open(QIODevice::WriteOnly), "create fixture");
	Require(file.write(source) == source.size(), "write fixture");
}

}

int main(int argc, char *argv[]) {
	using namespace MoeGram::Plugins;
	auto app = QCoreApplication(argc, argv);
	auto directory = QTemporaryDir();
	Require(directory.isValid(), "temporary directory");
	Write(directory.path() + "/00_faulty.py", R"py(
from base_plugin import BasePlugin, HookResult, HookStrategy
__id__ = 'faulty_compat'
__name__ = 'Faulty'
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook('TL_update', match_substring=True, priority=100)
    def on_update_hook(self, name, account, update):
        import builtins
        builtins._canonical_update = update
        self.log('priority-first ' + str(update.sequence))
        raise SystemExit('intentional compatibility exception')
)py");
	Write(directory.path() + "/10_both.py", R"py(
from base_plugin import BasePlugin, HookResult, HookStrategy
from dataclasses import FrozenInstanceError
__id__ = 'both'
__name__ = 'Both'
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook('TL_updateNewMessage')
        self.add_hook('TL_update', match_substring=True, priority=5)
        import _moegram_runtime
        assert _moegram_runtime._write_log.__self__ is None
        self.calls = 0
        self.incoming = 0
        self.edits = 0
        self.deletes = 0
    def on_update_hook(self, name, account, update):
        import builtins
        assert update is builtins._canonical_update
        self.calls += 1
        assert update.sequence == self.calls
        assert account == update.account_id == 123
        assert update.source == 'telegram'
        assert update.pts_count == 1
        try:
            update.pts = 99
            raise AssertionError('mutable Update')
        except FrozenInstanceError:
            pass
        self.last = update
        if 'Delete' in name:
            assert isinstance(update.messages, tuple)
            assert not hasattr(update, 'message')
            if name == 'TL_updateDeleteChannelMessages':
                assert update.channel_id == 456
                assert [m.outgoing for m in update.message_snapshots] == [False, True]
            else:
                assert not hasattr(update, 'channel_id')
            self.pending = list(update.message_snapshots)
        else:
            assert not hasattr(update, 'messages') and not hasattr(update, 'channel_id')
            message = update.message
            assert message.id == message.message_id
            assert message.message == message.text
            assert message.out == message.outgoing
            assert message.from_id.user_id == 789
            if message.chat_id == -1000000000456:
                assert name.endswith('ChannelMessage')
                assert message.peer_id.channel_id == 456
            elif message.chat_id == -555:
                assert message.peer_id.chat_id == 555
            else:
                assert message.peer_id.user_id == 777
        self.log('compat ' + str(self.calls))
        return HookResult(strategy=HookStrategy.CANCEL) if self.calls == 1 else HookResult()
    def on_message(self, message):
        assert message is self.last.message and message.outgoing is False
        self.incoming += 1
        self.log('specialized incoming')
    def on_message_edit(self, message):
        assert message is self.last.message
        self.edits += 1
        self.log('specialized edit')
    def on_message_delete(self, message):
        assert message is self.pending.pop(0)
        self.deletes += 1
        self.log('specialized delete')
    def on_plugin_unload(self):
        assert (self.calls, self.incoming, self.edits, self.deletes) == (12, 3, 4, 5)
        self.log('compatibility counts passed')
)py");
	Write(directory.path() + "/20_remove.py", R"py(
from base_plugin import BasePlugin, HookResult
__id__ = 'removed_hook'
__name__ = 'Removed'
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook('TL_updateNewMessage')
    def on_update_hook(self, name, account, update):
        self.log('removed hook once')
        self.remove_hook('TL_updateNewMessage')
        return HookResult()
)py");
	if (argc == 2) {
		Require(QFile::copy(QString::fromLocal8Bit(argv[1]),
			directory.path() + "/30_compatibility_logger.py"), "copy real plugin");
	}
	auto logs = QStringList();
	auto mutex = std::mutex();
	auto ready = std::condition_variable();
	auto host = Host([&](const QString &message) {
		const auto lock = std::lock_guard(mutex);
		logs.push_back(message);
		QTextStream(stdout) << message << Qt::endl;
		ready.notify_all();
	});
	Require(host.start(directory.path()), "start");
	const auto send = [&](const char *name, int pts, qint64 chat,
			EventKind kind, bool outgoing, std::vector<qint64> ids) {
		auto event = UpdateEvent();
		event.accountId = 123;
		event.name = QString::fromLatin1(name);
		event.native = true;
		event.pts = pts;
		event.ptsCount = 1;
		for (const auto id : ids) {
			auto message = MessageEvent();
			message.kind = kind;
			message.messageId = id;
			message.chatId = chat;
			message.senderId = 789;
			message.outgoing = (kind == EventKind::Delete
				&& chat == -1000000000456LL && id == 47) ? true : outgoing;
			message.text = QString::number(pts);
			event.messages.push_back(std::move(message));
		}
		Require(host.post(event), "post update");
		Require(host.post(event), "post duplicate update");
	};
	send("TL_updateNewMessage", 10, 777, EventKind::Message, false, {42});
	send("TL_updateNewMessage", 11, 777, EventKind::Message, true, {43});
	send("TL_updateEditMessage", 12, 777, EventKind::Edit, false, {42});
	send("TL_updateEditMessage", 13, 777, EventKind::Edit, true, {43});
	send("TL_updateDeleteMessages", 14, 777, EventKind::Delete, false, {42});
	send("TL_updateDeleteMessages", 15, 777, EventKind::Delete, true, {43, 44});
	send("TL_updateNewMessage", 16, -555, EventKind::Message, false, {45});
	send("TL_updateNewChannelMessage", 17, -1000000000456LL, EventKind::Message, false, {46});
	send("TL_updateNewChannelMessage", 18, -1000000000456LL, EventKind::Message, true, {47});
	{
		const auto scope = NativeEventScope(true);
		auto suppressed = MessageEvent();
		suppressed.accountId = 123;
		suppressed.messageId = 9999;
		suppressed.chatId = 777;
		suppressed.outgoing = false;
		Publish(suppressed);
		Require(!ModelEventsEnabled(), "native scope suppresses model bridge");
	}
	Require(ModelEventsEnabled(), "native scope restores model bridge");
	auto repeated = MessageEvent();
	repeated.accountId = 123;
	repeated.messageId = 42;
	repeated.chatId = 777;
	repeated.outgoing = false;
	Require(host.post(repeated), "model echo of native creation");
	repeated.kind = EventKind::Delete;
	repeated.chatId = std::nullopt;
	Require(host.post(repeated), "model echo of non-channel deletion");
	send("TL_updateEditChannelMessage", 19, -1000000000456LL, EventKind::Edit, false, {46});
	send("TL_updateEditChannelMessage", 20, -1000000000456LL, EventKind::Edit, true, {47});
	send("TL_updateDeleteChannelMessages", 21, -1000000000456LL, EventKind::Delete, false, {46, 47});
	{
		auto lock = std::unique_lock(mutex);
		Require(ready.wait_for(lock, std::chrono::seconds(5), [&] {
			return logs.join('\n').contains("compat 12");
		}), "wait for delivery");
	}
	host.stop();
	const auto text = logs.join('\n');
	Require(text.contains("compatibility counts passed"), "outgoing, edit, delete and dedup counts");
	Require(!text.contains("failed: both"), "all DTO assertions passed");
	Require(text.contains("intentional compatibility exception"), "hook exception isolated");
	Require(text.contains("unsupported for read-only"), "unsupported strategy reported");
	Require(text.count("removed hook once") == 1, "remove_hook effective");
	Require(text.indexOf("priority-first 1") < text.indexOf("compat 1"), "priority order");
	if (argc == 2) {
		Require(text.count("COMPAT {") == 12, "real plugin sees one callback per update");
	}
	Require(!host.post(UpdateEvent()), "no updates after unload");
	QTextStream(stdout) << "PASS: canonical shared DTO, hooks, batching, outgoing, priority, dedup and exceptions" << Qt::endl;
}
