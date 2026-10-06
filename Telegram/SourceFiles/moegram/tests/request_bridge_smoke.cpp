#include "moegram/bridge/requests.h"
#include "moegram/bridge/outgoing.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>
#include <atomic>
#include <mutex>
#include <set>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		QTextStream(stderr) << "Bridge smoke failed: " << message << Qt::endl;
		std::abort();
	}
}

void Wait(std::function<bool()> ready) {
	auto loop = QEventLoop();
	auto timer = QTimer();
	QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
		if (ready()) {
			loop.quit();
		}
	});
	QTimer::singleShot(5000, &loop, &QEventLoop::quit);
	timer.start(5);
	loop.exec();
	Require(ready(), "callback timeout");
}

}

int main(int argc, char *argv[]) {
	using namespace MoeGram;
	using MTP::details::SerializedRequest;
	auto app = QCoreApplication(argc, argv);
	Require(argc == 3, "original ghost and hello fixture arguments");
	auto directory = QTemporaryDir();
	Require(directory.isValid(), "temporary workdir");
	const auto plugins = directory.path() + u"/plugins"_q;
	Require(QDir().mkpath(plugins), "plugins directory");
	Require(QFile::copy(QString::fromLocal8Bit(argv[1]),
		plugins + u"/ghost_mode.py"_q), "copy original ghost");
	Require(QFile::copy(QString::fromLocal8Bit(argv[2]),
		plugins + u"/hello_command.py"_q), "copy original hello");
	auto faulty = QFile(plugins + u"/00_faulty.py"_q);
	Require(faulty.open(QIODevice::WriteOnly), "faulty fixture");
	faulty.write(R"py(from base_plugin import BasePlugin
import time
__id__='faulty_bridge'
__name__='Faulty bridge'
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook('TL_account', True, 100)
        self.add_on_send_message_hook(priority=100)
    def pre_request_hook(self, name, account, request):
        self.log('request in flight')
        time.sleep(0.05)
        request.offline='invalid'
        raise SystemExit('bridge pre exception')
    def post_request_hook(self, name, account, response, error):
        raise SystemExit('bridge post exception')
    def on_send_message_hook(self, account, params):
        time.sleep(0.05)
        params.message='invalid'
        raise SystemExit('bridge send exception')
)py");
	faulty.close();
	auto logs = QStringList();
	auto mutex = std::mutex();
	auto started = std::atomic<int>(0);
	auto host = Plugins::Host([&](const QString &message) {
		const auto lock = std::lock_guard(mutex);
		logs.push_back(message);
		if (message.contains(u"request in flight"_q)) {
			++started;
		}
	});
	Require(host.start(plugins) && host.loadedCount() == 3, "original plugins loaded");
	auto owner = std::make_unique<QObject>();
	const auto weak = QPointer<QObject>(owner.get());
	auto pending = std::set<mtpRequestId>();
	auto sent = 0;
	auto cancelled = 0;
	auto done = 0;
	auto failed = 0;
	const auto makeCallbacks = [&] {
		return MTP::ResponseHandler{
			.done = [&](const MTP::Response &) {
				++done;
				return false;
			},
			.fail = [&](const MTP::Error &, const MTP::Response &) {
				++failed;
				return false;
			},
		};
	};
	auto callbacks = makeCallbacks();
	auto normal = SerializedRequest::Serialize(MTPhelp_GetNearestDc());
	const auto normalAlias = normal;
	Require(!Bridge::PrepareRequestHooks(Plugins::RequestSnapshot(123, normal),
		normal, callbacks), "ordinary request bypass");
	Require(normal.operator->() == normalAlias.operator->(), "bypass preserves ownership");
	auto typing = SerializedRequest::Serialize(MTPmessages_SetTyping(
		MTP_flags(0), MTP_inputPeerSelf(), MTP_int(0), MTP_sendMessageTypingAction()));
	auto typingCallbacks = makeCallbacks();
	const auto typingSnapshot = Bridge::PrepareRequestHooks(
		Plugins::RequestSnapshot(123, typing), typing, typingCallbacks);
	typing->requestId = 41;
	pending.insert(41);
	Bridge::DispatchRequest(*typingSnapshot, typing,
		[&] { return weak && pending.contains(41); },
		[&](const auto &) { ++sent; },
		[&](const MTP::Response &response) {
			Require(response.requestId == 41, "cancel correlates request id");
			const auto error = MTP::Error(response.reply);
			Require(error.code() == 400 && error.type() == u"PLUGIN_REQUEST_CANCELLED"_q,
				"same native cancellation error");
			Require(!typingCallbacks.fail(error, response), "original fail result preserved");
			pending.erase(41);
			++cancelled;
		});
	Wait([&] { return cancelled == 1; });
	Require(sent == 0 && failed == 1, "typing cancelled before transport");
	auto status = SerializedRequest::Serialize(MTPaccount_UpdateStatus(MTP_bool(false)));
	status->requestId = 42;
	status->lastSentTime = 12345;
	status->needsLayer = true;
	status->forceSendInContainer = true;
	status->after = normal;
	status.setMsgId(123456);
	status.setSeqNo(7);
	const auto alias = status;
	auto statusCallbacks = makeCallbacks();
	const auto statusSnapshot = Bridge::PrepareRequestHooks(
		Plugins::RequestSnapshot(123, status), status, statusCallbacks);
	Require(status.operator->() != alias.operator->(), "hooked request detached before storage");
	Require(*status == *alias && status->requestId == 42
		&& status->lastSentTime == 12345 && status->needsLayer
		&& status->forceSendInContainer
		&& status->after.operator->() == normal.operator->(), "all metadata and dependency preserved");
	const auto stored = status;
	pending.insert(42);
	Bridge::DispatchRequest(*statusSnapshot, status,
		[&] { return weak && pending.contains(42); },
		[&](const auto &prepared) {
			Require(prepared.operator->() == stored.operator->(), "transport and retry store share payload");
			const auto offset = SerializedRequest::kMessageBodyPosition + 1;
			Require((*prepared)[offset] == mtpc_boolTrue, "MODIFY offline applied");
			Require((*alias)[offset] == mtpc_boolFalse, "external alias not mutated");
			++sent;
		}, [&](const auto &) { Require(false, "status must not cancel"); });
	Wait([&] { return sent == 1; });
	auto response = MTP::Response();
	MTPBool(MTP_bool(true)).write(response.reply);
	Require(!statusCallbacks.done(response), "original done result preserved");
	Require(!statusCallbacks.fail(MTP::Error::Local(u"TEST_ERROR"_q, u"probe"_q), response),
		"original error result preserved");
	pending.erase(42);
	Bridge::DispatchRequest(*typingSnapshot, typing,
		[&] { return weak && pending.contains(41); },
		[&](const auto &) { Require(false, "removed request not sent"); },
		[&](const auto &) { Require(false, "removed request not cancelled twice"); });
	Bridge::DispatchRequest(*statusSnapshot, status,
		[weak] { return bool(weak); },
		[&](const auto &) { Require(false, "destroyed owner not sent"); },
		[&](const auto &) { Require(false, "destroyed owner not cancelled"); });
	owner.reset();
	auto filtered = 0;
	Bridge::FilterOutgoingText(123, u".hello Alice"_q, [] { return true; },
		[&](QString text) {
			Require(text == u"Hello, Alice!"_q, "original hello through bridge");
			++filtered;
		});
	Bridge::FilterOutgoingText(123, u"plain"_q, [] { return true; },
		[&](QString text) {
			Require(text == u"plain"_q, "ordinary send unchanged");
			++filtered;
		});
	Bridge::FilterOutgoingText(123, u"stale"_q, [] { return false; },
		[&](QString) { Require(false, "expired outgoing session not resumed"); });
	Wait([&] { return filtered == 2; });
	auto completed = false;
	auto unloaded = false;
	Bridge::DispatchRequest(*statusSnapshot, status, [] { return true; },
		[&](const auto &) { completed = true; },
		[&](const auto &) { Require(false, "outstanding status cancelled"); });
	Require(host.setPluginEnabled(u"ghost_mode"_q, false,
		[&](bool changed, auto) { Require(changed, "unload ghost"); unloaded = true; }),
		"queue unload around pending request");
	Wait([&] { return completed && unloaded; });
	Bridge::DispatchRequest(*typingSnapshot, typing, [] { return true; },
		[&](const auto &) { ++sent; },
		[&](const auto &) { Require(false, "typing restored after unload"); });
	Wait([&] { return sent == 2; });
	Require(!statusCallbacks.done(response), "late native response after plugin unload");
	const auto before = started.load();
	Bridge::DispatchRequest(*statusSnapshot, status, [] { return true; },
		[&](const auto &) { Require(false, "shutdown drops outstanding delivery"); },
		[&](const auto &) { Require(false, "shutdown drops outstanding cancel"); });
	Wait([&] { return started > before; });
	host.stop();
	QCoreApplication::processEvents();
	Require(!statusCallbacks.done(response), "late response after host stop");
	Require(done == 3, "native done survives plugin and host unload");
	{
		const auto lock = std::lock_guard(mutex);
		const auto text = logs.join('\n');
		Require(text.contains(u"bridge pre exception"_q)
			&& text.contains(u"bridge post exception"_q)
			&& text.contains(u"bridge send exception"_q), "exceptions exercised");
	}
	QTextStream(stdout) << "PASS: bridge CANCEL/MODIFY, detached payload/metadata/retries, native callbacks, expired ownership, original hello, exceptions, outstanding unload/shutdown" << Qt::endl;
}
