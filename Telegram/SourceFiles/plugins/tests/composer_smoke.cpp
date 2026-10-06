#include "plugins/plugin_host.h"
#include "base/basic_types.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		qFatal("Composer smoke failed: %s", message);
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
	using namespace MoeGram::Plugins;
	auto app = QCoreApplication(argc, argv);
	Require(argc == 2, "provide typing_fx.py");
	auto directory = QTemporaryDir();
	Require(directory.isValid(), "temporary directory");
	const auto plugins = directory.path() + u"/plugins"_q;
	Require(QDir().mkpath(plugins), "plugins directory");
	Require(QFile::copy(QString::fromLocal8Bit(argv[1]),
		plugins + u"/typing_fx.py"_q), "copy plugin");
	auto latest = std::optional<ComposerConfig>();
	auto delivered = 0;
	auto host = Host([](const QString &) {}, nullptr,
		[&](QString id, std::optional<ComposerConfig> config) {
			Require(QThread::currentThread() == app.thread(), "owner thread delivery");
			Require(id == u"typing_fx"_q, "stable plugin id");
			latest = config;
			++delivered;
		});
	Require(host.start(plugins), "host startup");
	auto done = false;
	Require(host.setPluginEnabled(u"typing_fx"_q, false,
		[&](bool changed, std::vector<PluginInfo>) {
			Require(changed, "disable succeeded");
			done = true;
		}), "queue disable");
	Require(delivered == 1 && !latest, "disable removes config synchronously");
	Wait([&] { return done; });
	Require(!latest, "queued startup config cannot resurrect disabled plugin");
	done = false;
	Require(host.setPluginEnabled(u"typing_fx"_q, true,
		[&](bool changed, std::vector<PluginInfo>) {
			Require(changed, "enable succeeded");
			done = true;
		}), "queue enable");
	Wait([&] { return done; });
	Require(latest && latest->insertion && latest->deletion && latest->caret
		&& latest->send && latest->edit, "native flags match immutable config");
	done = false;
	Require(host.requestSettings([&](std::vector<PluginSettings> pages) {
		Require(pages.size() == 1, "settings exposed");
		auto selectors = 0;
		for (const auto &item : pages.front().items) {
			if (item.kind == SettingKind::Selector) {
				Require(item.choices.size() == 3, "three immutable selector options");
				Require(item.selected == 1, "default selector index");
				++selectors;
			}
		}
		Require(selectors == 3, "style/speed/intensity selectors");
		done = true;
	}), "request settings");
	Wait([&] { return done; });
	done = false;
	Require(host.updateSetting(u"typing_fx"_q, u"speed_choice"_q, 2,
		[&](bool) { done = true; }), "update speed");
	Wait([&] { return done; });
	Require(latest && latest->speed == 1.6, "setting republished to native controller");
	host.stop();
	Require(!latest, "stop removes config");
	Require(host.start(plugins), "host restart");
	Wait([&] { return latest.has_value(); });
	Require(latest->speed == 1.6, "native config restored from persistent settings");
	host.stop();
	Require(!latest, "restart unload removes config");
	QTextStream(stdout) << "PASS: embedded composer bridge, Qt delivery, stale suppression, settings, restart, unload" << Qt::endl;
}
