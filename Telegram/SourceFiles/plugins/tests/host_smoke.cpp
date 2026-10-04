#include "plugins/plugin_host.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>

#include <cstdlib>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		qFatal("Plugin smoke test failed: %s", message);
	}
}

void WritePlugin(
		const QString &directory,
		const char *filename,
		const char *id,
		const char *body) {
	auto file = QFile(directory + '/' + QString::fromUtf8(filename));
	Require(file.open(QIODevice::WriteOnly), "create fixture");
	const auto source = QByteArray("from base_plugin import BasePlugin\n__id__ = '")
		+ id + "'\n__name__ = 'Smoke test'\n__version__ = '1.0'\n" + body;
	Require(file.write(source) == source.size(), "write fixture");
}

}

int main(int argc, char *argv[]) {
	auto app = QCoreApplication(argc, argv);
	auto logs = QStringList();
	auto host = MoeGram::Plugins::Host([&](const QString &message) {
		logs.push_back(message);
		QTextStream(stdout) << message << Qt::endl;
	});
	if (argc == 2) {
		if (!host.start(QString::fromLocal8Bit(argv[1]))) {
			return EXIT_FAILURE;
		}
		host.stop();
		return EXIT_SUCCESS;
	}

	auto directory = QTemporaryDir();
	Require(directory.isValid(), "temporary directory");
	WritePlugin(directory.path(), "00_import.py", "import_error",
		"raise RuntimeError('intentional import exception')\n");
	WritePlugin(directory.path(), "10_constructor.py", "constructor_error",
		"class Plugin(BasePlugin):\n"
		"    def __init__(self):\n"
		"        raise RuntimeError('intentional constructor exception')\n");
	WritePlugin(directory.path(), "20_load.py", "load_error",
		"class Plugin(BasePlugin):\n"
		"    def on_plugin_load(self):\n"
		"        raise RuntimeError('intentional load exception')\n"
		"    def on_plugin_unload(self):\n"
		"        self.log('failed load cleanup')\n");
	WritePlugin(directory.path(), "30_exit.py", "system_exit",
		"raise SystemExit('intentional SystemExit')\n");
	WritePlugin(directory.path(), "40_metadata.py", "../bad_id",
		"raise RuntimeError('metadata validation executed plugin')\n");
	WritePlugin(directory.path(), "50_healthy.py", "healthy",
		"class Plugin(BasePlugin):\n"
		"    def on_plugin_load(self):\n"
		"        import sys\n"
		"        assert sys.flags.isolated and 'site' not in sys.modules\n"
		"        assert sys.modules[type(self).__module__].__file__\n"
		"        assert self.id == 'healthy' and self.name == 'Smoke test'\n"
		"        self.logger.info('healthy load %s', self.version)\n"
		"        try:\n"
		"            raise RuntimeError('handled logging exception')\n"
		"        except RuntimeError:\n"
		"            self.logger.exception('caught by plugin')\n"
		"    def on_plugin_unload(self):\n"
		"        self.logger.info('healthy unload')\n");
	WritePlugin(directory.path(), "60_duplicate.py", "healthy",
		"raise RuntimeError('duplicate validation executed plugin')\n");
	WritePlugin(directory.path(), "70_unload.py", "unload_error",
		"class Plugin(BasePlugin):\n"
		"    def on_plugin_load(self):\n"
		"        self.log('unload fixture loaded')\n"
		"    def on_plugin_unload(self):\n"
		"        raise RuntimeError('intentional unload exception')\n");
	WritePlugin(directory.path(), "80_syntax.py", "syntax_error", "def (\n");
	WritePlugin(directory.path(), "90_no_class.py", "no_class", "pass\n");
	WritePlugin(directory.path(), "_ignored.py", "ignored",
		"raise RuntimeError('ignored file executed')\n");
	Require(host.start(directory.path()), "interpreter startup");
	Require(host.loadedCount() == 2, "only healthy plugins loaded");
	Require(!host.start(directory.path()), "reject repeated start");
	auto competing = MoeGram::Plugins::Host([](const QString &) {});
	Require(!competing.start(directory.path()), "reject competing interpreter owner");
	host.stop();
	host.stop();
	Require(host.loadedCount() == 0, "stop resets loaded count");
	const auto combined = logs.join('\n');
	for (const auto *expected : {
		"intentional import exception",
		"intentional constructor exception",
		"intentional load exception",
		"failed load cleanup",
		"intentional SystemExit",
		"healthy load 1.0",
		"caught by plugin",
		"handled logging exception",
		"healthy unload",
		"Duplicate plugin id: healthy",
		"intentional unload exception",
		"SyntaxError",
		"Define exactly one BasePlugin subclass",
		"CPython stopped",
	}) {
		Require(combined.contains(QString::fromUtf8(expected)), expected);
	}
	Require(!combined.contains(QString::fromUtf8("validation executed plugin")),
		"invalid metadata and duplicate IDs rejected before execution");
	Require(!combined.contains(QString::fromUtf8("ignored file executed")),
		"underscore files ignored");
	Require(combined.indexOf(QString::fromUtf8("unloaded unload_error"))
		< combined.indexOf(QString::fromUtf8("healthy unload")),
		"unload runs in reverse order and continues after failure");
	Require(host.start(directory.path()), "interpreter can restart after stop");
	Require(host.loadedCount() == 2, "restart loads fresh instances");
	host.stop();
	QTextStream(stdout) << "PASS: embedded host lifecycle and exception recovery"
		<< Qt::endl;
	return EXIT_SUCCESS;
}
