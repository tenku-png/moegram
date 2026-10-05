#include "plugins/plugin_host.h"

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
		qFatal("Send smoke failed: %s", message);
	}
}

void Write(const QString &path, const QByteArray &source) {
	auto file = QFile(path);
	Require(file.open(QIODevice::WriteOnly), "open fixture");
	Require(file.write(source) == source.size(), "write fixture");
}

}

int main(int argc, char *argv[]) {
	auto app = QCoreApplication(argc, argv);
	Require(argc == 2, "published hello_command path required");
	auto source = QFile(QString::fromLocal8Bit(argv[1]));
	Require(source.open(QIODevice::ReadOnly), "read original plugin");
	const auto original = source.readAll();
	auto directory = QTemporaryDir();
	Require(directory.isValid(), "temporary workdir");
	const auto plugins = directory.path() + "/plugins";
	Require(QDir().mkpath(plugins), "plugin directory");
	Write(plugins + "/hello_command.py", original);
	Write(plugins + "/00_faulty.py",
		"from base_plugin import BasePlugin\nimport time\n"
		"__id__='faulty_send'\n__name__='Faulty Send'\n"
		"class Plugin(BasePlugin):\n"
		"    def on_plugin_load(self):\n"
		"        self.add_on_send_message_hook(priority=10)\n"
		"    def on_send_message_hook(self, account, params):\n"
		"        assert account==123\n"
		"        params.message='corrupted'\n"
		"        time.sleep(0.05)\n"
		"        raise SystemExit('intentional send exception')\n");
	Write(plugins + "/zz_probe.py",
		"from base_plugin import BasePlugin, HookResult\nimport sys\n"
		"__id__='send_probe'\n__name__='Send Probe'\n"
		"class Plugin(BasePlugin):\n"
		"    def on_plugin_load(self):\n"
		"        original=sys.modules['_moegram_plugin_hello_world'].HelloWorldPlugin()\n"
		"        assert [type(row).__name__ for row in original.create_settings()]==['Header','Input','Text']\n"
		"        self.add_on_send_message_hook(priority=-1)\n"
		"    def on_send_message_hook(self, account, params):\n"
		"        assert type(params).__name__=='SendParams'\n"
		"        assert account==123\n"
		"        self.log('probe received '+params.message)\n"
		"        return HookResult()\n");
	auto logs = QStringList();
	auto mutex = std::mutex();
	auto host = MoeGram::Plugins::Host([&](const QString &line) {
		const auto lock = std::lock_guard(mutex);
		logs.push_back(line);
		QTextStream(stdout) << line << Qt::endl;
	});
	Require(host.start(plugins), "start host");
	Require(host.loadedCount() == 3, "original and probe plugins loaded");
	const auto inputs = QStringList{
		".hello Alice", ".hello", ".hello Alice Smith", "plain", ".hello Алиса" };
	const auto expected = QStringList{
		"Hello, Alice!", "Usage: .hello <name>", "Hello, Alice Smith!", "plain", "Hello, Алиса!" };
	auto received = 0;
	auto ticks = 0;
	auto timer = QTimer();
	timer.setInterval(10);
	QObject::connect(&timer, &QTimer::timeout, [&] { ++ticks; });
	timer.start();
	for (auto index = 0; index != inputs.size(); ++index) {
		MoeGram::Plugins::FilterOutgoing(123, inputs[index], [&, index](auto result) {
			Require(QThread::currentThread() == app.thread(), "delivery on Qt owner thread");
			Require(index == received, "FIFO send completion");
			Require(result && *result == expected[index], "original plugin text rewrite");
			if (++received == inputs.size()) {
				app.quit();
			}
		});
	}
	QTimer::singleShot(10000, &app, [] { Require(false, "send completion timeout"); });
	app.exec();
	Require(ticks >= 5, "Python callbacks did not block Qt event loop");
	host.stop();
	const auto combined = logs.join('\n');
	Require(combined.contains("intentional send exception"), "exception exercised");
	Require(!combined.contains("failed: send_probe"), "probe and settings assertions passed");
	Require(combined.count("probe received ") == inputs.size(), "one hook invocation per send");
	auto bypass = false;
	MoeGram::Plugins::FilterOutgoing(123, ".hello Alice", [&](auto result) {
		bypass = result && *result == ".hello Alice";
	});
	Require(bypass, "no Python send hook after unload");
	auto copied = QFile(plugins + "/hello_command.py");
	Require(copied.open(QIODevice::ReadOnly), "read copied plugin");
	Require(copied.readAll() == original, "original plugin source unchanged");
	QTextStream(stdout) << "PASS: original hello_command, detached params, FIFO, exceptions, Qt loop and unload" << Qt::endl;
}
