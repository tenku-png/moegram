#include "plugins/telegram_request_adapter.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <QtCore/QThread>
#include <QtCore/QTimer>

#include <mutex>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		QTextStream(stderr) << "Request smoke failed: " << message << Qt::endl;
		qFatal("Request smoke failed: %s", message);
	}
}

void Write(const QString &path, const QByteArray &source) {
	auto file = QFile(path);
	Require(file.open(QIODevice::WriteOnly), "open fixture");
	Require(file.write(source) == source.size(), "write fixture");
}

}

int main(int argc, char *argv[]) {
	using namespace MoeGram::Plugins;
	using MTP::details::SerializedRequest;
	auto app = QCoreApplication(argc, argv);
	Require(argc == 2, "original fixture argument");
	auto directory = QTemporaryDir();
	const auto plugins = directory.path() + "/plugins";
	Require(QDir().mkpath(plugins), "plugins directory");
	Require(QFile::copy(QString::fromLocal8Bit(argv[1]), plugins + "/ghost_mode.py"), "original copy");
	Write(plugins + "/00_faulty.py", R"py(
from base_plugin import BasePlugin
import time
__id__='faulty_request'
__name__='Faulty'
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook('TL_account', True, 100)
    def pre_request_hook(self, name, account, request):
        request.offline='corrupted'
        time.sleep(0.05)
        raise SystemExit('intentional pre exception')
    def post_request_hook(self, name, account, response, error):
        raise SystemExit('intentional post exception')
)py");
	Write(plugins + "/zz_probe.py", R"py(
from base_plugin import BasePlugin, HookResult
__id__='request_probe'
__name__='Probe'
class Plugin(BasePlugin):
    def on_plugin_load(self):
        self.add_hook('TL_account', True, -10)
        self.add_hook('TL_account_updateStatus', False, -10)
    def pre_request_hook(self, name, account, request):
        assert account==123 and request.offline is True
        self.log('after ghost')
        return HookResult()
    def post_request_hook(self, name, account, response, error):
        assert account==123
        if error:
            assert response is None and error.code==400
        else:
            assert response.value is True and error is None
            try:
                response.value=False
            except AttributeError:
                pass
            else:
                raise AssertionError('mutable response')
        self.log('post survived')
)py");
	auto logs = QStringList();
	auto mutex = std::mutex();
	auto host = Host([&](const QString &message) {
		const auto lock = std::lock_guard(mutex);
		logs.push_back(message);
		QTextStream(stdout) << message << Qt::endl;
	});
	Require(host.start(plugins), "start");
	Require(host.loadedCount() == 3, "original plus probes loaded");
	auto normal = SerializedRequest::Serialize(MTPhelp_GetNearestDc());
	Require(!RequestSnapshot(123, normal), "normal requests bypass unchanged");
	auto malformed = SerializedRequest::Prepare(1);
	malformed->push_back(mtpc_account_updateStatus);
	Require(!RequestSnapshot(123, malformed), "malformed request rejected");
	auto typing = SerializedRequest::Serialize(MTPmessages_SetTyping(
		MTP_flags(0), MTP_inputPeerSelf(), MTP_int(0), MTP_sendMessageTypingAction()));
	const auto typingSnapshot = RequestSnapshot(123, typing);
	Require(typingSnapshot.has_value(), "typed native typing DTO");
	auto completed = 0;
	auto ticks = 0;
	auto timer = QTimer();
	QObject::connect(&timer, &QTimer::timeout, [&] {
		++ticks;
		const auto lock = std::lock_guard(mutex);
		if (logs.join('\n').count("post survived") == 2) {
			app.quit();
		}
	});
	timer.start(5);
	FilterRequest(*typingSnapshot, [&](std::optional<RequestEvent> filtered) {
		Require(QThread::currentThread() == app.thread(), "Qt owner completion");
		Require(!filtered, "typing cancelled before transport");
		Require(completed++ == 0, "FIFO typing");
	});
	auto status = SerializedRequest::Serialize(MTPaccount_UpdateStatus(MTP_bool(false)));
	status->requestId = 42;
	FilterRequest(*RequestSnapshot(123, status), [&](std::optional<RequestEvent> filtered) {
		Require(filtered && filtered->offline == true, "status modified");
		Require(completed++ == 1, "FIFO status");
		ApplyRequestSnapshot(status, *filtered);
		Require(status->requestId == 42, "metadata preserved");
		auto from = status->constData() + SerializedRequest::kMessageBodyPosition;
		auto parsed = MTPaccount_UpdateStatus();
		Require(parsed.read(from, status->constData() + status->size()), "modified native request readable");
		Require((*status)[SerializedRequest::kMessageBodyPosition + 1] == mtpc_boolTrue, "native serialized offline true");
		auto response = MTP::Response();
		MTPBool(MTP_bool(true)).write(response.reply);
		ObserveRequestResponse(*filtered, response);
		ObserveRequestError(*filtered, MTP::Error(MTP_rpc_error(MTP_int(400), MTP_string("TEST_ERROR"))));
	});
	QTimer::singleShot(5000, &app, &QCoreApplication::quit);
	app.exec();
	Require(completed == 2 && ticks >= 10, "nonblocking Qt loop");
	{
		const auto lock = std::lock_guard(mutex);
		const auto text = logs.join('\n');
		Require(text.count("after ghost") == 1, "one callback despite multiple matches");
		Require(text.count("post survived") == 2, "post success/error isolation");
		Require(text.contains("intentional pre exception"), "pre exception exercised");
		Require(text.contains("intentional post exception"), "post exception exercised");
		Require(!text.contains("failed: request_probe"), "probe assertions passed");
	}
	host.stop();
	Require(!host.filterRequest(*typingSnapshot, [](auto) {}), "reject after unload");
	auto bypass = false;
	FilterRequest(*typingSnapshot, [&](auto filtered) { bypass = filtered.has_value(); });
	Require(bypass, "unload restores typing");
	FilterRequest(*RequestSnapshot(123, SerializedRequest::Serialize(MTPaccount_UpdateStatus(MTP_bool(false)))),
		[&](auto filtered) { Require(filtered && filtered->offline == false, "unload restores online status"); });
	QTextStream(stdout) << "PASS native request serializer, original ghost, worker, responses, unload" << Qt::endl;
}
