#include <Python.h>

#include "plugins/plugin_host.h"

#include "base/basic_types.h"
#include "runtime_source.h"

#include <QtCore/QDir>
#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QThread>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>
#include <variant>

namespace MoeGram::Plugins {
namespace {

Host *ActiveHost = nullptr;
Host::Logger *RuntimeLogger = nullptr;
std::function<void(QString)> *RuntimeToast = nullptr;
thread_local int NativeEventDepth = 0;

struct PythonDeleter {
	void operator()(PyObject *object) const {
		Py_DECREF(object);
	}
};

using PythonObject = std::unique_ptr<PyObject, PythonDeleter>;

QString TakePythonError() {
	PyObject *type = nullptr;
	PyObject *value = nullptr;
	PyObject *trace = nullptr;
	PyErr_Fetch(&type, &value, &trace);
	PyErr_NormalizeException(&type, &value, &trace);
	const auto ownedType = PythonObject(type);
	const auto ownedValue = PythonObject(value);
	const auto ownedTrace = PythonObject(trace);
	PyErr_Clear();

	const auto traceback = PythonObject(PyImport_ImportModule("traceback"));
	const auto lines = traceback
		? PythonObject(PyObject_CallMethod(
			traceback.get(),
			"format_exception",
			"OOO",
			type ? type : Py_None,
			value ? value : Py_None,
			trace ? trace : Py_None))
		: PythonObject();
	const auto separator = PythonObject(PyUnicode_FromString(""));
	const auto formatted = (lines && separator)
		? PythonObject(PyUnicode_Join(separator.get(), lines.get()))
		: PythonObject();
	const auto utf8 = formatted ? PyUnicode_AsUTF8(formatted.get()) : nullptr;
	const auto result = utf8
		? QString::fromUtf8(utf8)
		: u"Python exception (traceback unavailable)"_q;
	PyErr_Clear();
	return result;
}

PyObject *WriteLog(PyObject *, PyObject *args) {
	const char *message = nullptr;
	if (!PyArg_ParseTuple(args, "s", &message)) {
		return nullptr;
	}
	const auto logger = RuntimeLogger;
	if (!logger) {
		PyErr_SetString(PyExc_RuntimeError, "Plugin logging is stopped");
		return nullptr;
	}
	try {
		(*logger)(QString::fromUtf8(message));
	} catch (...) {
		PyErr_SetString(PyExc_RuntimeError, "Client log callback failed");
		return nullptr;
	}
	Py_RETURN_NONE;
}

PyMethodDef LogMethod = {
	"_write_log",
	WriteLog,
	METH_VARARGS,
	nullptr,
};

PyObject *ShowToast(PyObject *, PyObject *args) {
	PyObject *text = nullptr;
	if (!PyArg_ParseTuple(args, "U", &text)) {
		return nullptr;
	}
	const auto callback = RuntimeToast;
	if (!callback) {
		PyErr_SetString(PyExc_RuntimeError, "Plugin UI is stopped");
		return nullptr;
	}
	auto length = Py_ssize_t();
	const auto value = PyUnicode_AsUTF8AndSize(text, &length);
	if (!value) {
		return nullptr;
	}
	try {
		(*callback)(QString::fromUtf8(value, length));
	} catch (...) {
		PyErr_SetString(PyExc_RuntimeError, "Client toast callback failed");
		return nullptr;
	}
	Py_RETURN_NONE;
}

PyMethodDef ToastMethod = {
	"_show_toast",
	ShowToast,
	METH_VARARGS,
	nullptr,
};

std::optional<QString> ReadPythonString(PyObject *object) {
	if (!PyUnicode_Check(object)) {
		return std::nullopt;
	}
	auto length = Py_ssize_t();
	const auto value = PyUnicode_AsUTF8AndSize(object, &length);
	return value
		? std::optional<QString>(QString::fromUtf8(value, length))
		: std::nullopt;
}

PythonObject MessagePayload(const MessageEvent &message) {
	auto payload = PythonObject(PyDict_New());
	const auto put = [&](const char *key, PythonObject value) {
		return payload && value
			&& PyDict_SetItemString(payload.get(), key, value.get()) == 0;
	};
	const auto integer = [](std::optional<qint64> value) {
		return value
			? PythonObject(PyLong_FromLongLong(*value))
			: PythonObject(Py_NewRef(Py_None));
	};
	const auto encoded = message.text.value_or(QString()).toUtf8();
	auto text = message.text
		? PythonObject(PyUnicode_DecodeUTF8(
			encoded.constData(), encoded.size(), "strict"))
		: PythonObject(Py_NewRef(Py_None));
	const auto valid = put("account_id", PythonObject(
		PyLong_FromUnsignedLongLong(message.accountId)))
		&& put("message_id", PythonObject(PyLong_FromLongLong(message.messageId)))
		&& put("chat_id", integer(message.chatId))
		&& put("sender_id", integer(message.senderId))
		&& put("text", std::move(text))
		&& put("outgoing", message.outgoing
			? PythonObject(PyBool_FromLong(*message.outgoing))
			: PythonObject(Py_NewRef(Py_None)));
	return valid ? std::move(payload) : PythonObject();
}

std::optional<std::vector<PluginSettings>> ReadSettingsSnapshot(
		PyObject *object) {
	const auto pages = PythonObject(PySequence_Fast(
		object, "Settings pages must be a sequence"));
	if (!pages) {
		return std::nullopt;
	}
	auto result = std::vector<PluginSettings>();
	const auto count = PySequence_Fast_GET_SIZE(pages.get());
	const auto values = PySequence_Fast_ITEMS(pages.get());
	for (auto i = Py_ssize_t(); i != count; ++i) {
		const auto page = PythonObject(PySequence_Fast(
			values[i], "Plugin settings page must be a sequence"));
		if (!page || PySequence_Fast_GET_SIZE(page.get()) != 3) {
			if (page) {
				PyErr_SetString(PyExc_TypeError, "Invalid plugin settings page");
			}
			return std::nullopt;
		}
		const auto pageValues = PySequence_Fast_ITEMS(page.get());
		const auto id = ReadPythonString(pageValues[0]);
		const auto name = ReadPythonString(pageValues[1]);
		const auto rows = PythonObject(PySequence_Fast(
			pageValues[2], "Plugin settings items must be a sequence"));
		if (!id || !name || !rows) {
			if (!PyErr_Occurred()) {
				PyErr_SetString(PyExc_TypeError, "Invalid plugin settings metadata");
			}
			return std::nullopt;
		}
		auto parsed = PluginSettings{
			.id = *id,
			.name = *name,
		};
		const auto rowCount = PySequence_Fast_GET_SIZE(rows.get());
		const auto rowValues = PySequence_Fast_ITEMS(rows.get());
		for (auto rowIndex = Py_ssize_t(); rowIndex != rowCount; ++rowIndex) {
			const auto row = PythonObject(PySequence_Fast(
				rowValues[rowIndex], "Plugin setting must be a sequence"));
			if (!row || PySequence_Fast_GET_SIZE(row.get()) != 6) {
				if (row) {
					PyErr_SetString(PyExc_TypeError, "Invalid plugin setting row");
				}
				return std::nullopt;
			}
			const auto fields = PySequence_Fast_ITEMS(row.get());
			const auto kind = ReadPythonString(fields[0]);
			const auto key = ReadPythonString(fields[1]);
			const auto text = ReadPythonString(fields[2]);
			const auto subtext = ReadPythonString(fields[3]);
			const auto value = ReadPythonString(fields[4]);
			if (!kind || !key || !text || !subtext || !value) {
				if (!PyErr_Occurred()) {
					PyErr_SetString(PyExc_TypeError, "Invalid plugin setting text field");
				}
				return std::nullopt;
			}
			auto item = SettingItem{
				.key = *key,
				.text = *text,
				.subtext = *subtext,
				.value = *value,
			};
			if (*kind == u"header"_q) {
				item.kind = SettingKind::Header;
			} else if (*kind == u"switch"_q) {
				if (!PyBool_Check(fields[5])) {
					PyErr_SetString(PyExc_TypeError, "Switch setting value must be a bool");
					return std::nullopt;
				}
				item.kind = SettingKind::Switch;
				item.checked = (fields[5] == Py_True);
			} else if (*kind == u"input"_q) {
				item.kind = SettingKind::Input;
			} else if (*kind == u"text"_q) {
				item.kind = SettingKind::Text;
			} else if (*kind == u"divider"_q) {
				item.kind = SettingKind::Divider;
			} else if (*kind == u"error"_q) {
				item.kind = SettingKind::Error;
			} else {
				continue;
			}
			parsed.items.push_back(std::move(item));
		}
		result.push_back(std::move(parsed));
	}
	return result;
}

std::optional<std::vector<PluginInfo>> ReadPluginsSnapshot(PyObject *object) {
	const auto entries = PythonObject(PySequence_Fast(
		object, "Plugin list must be a sequence"));
	if (!entries) {
		return std::nullopt;
	}
	auto result = std::vector<PluginInfo>();
	const auto count = PySequence_Fast_GET_SIZE(entries.get());
	const auto values = PySequence_Fast_ITEMS(entries.get());
	result.reserve(count);
	for (auto i = Py_ssize_t(); i != count; ++i) {
		const auto entry = PythonObject(PySequence_Fast(
			values[i], "Plugin metadata must be a sequence"));
		if (!entry || PySequence_Fast_GET_SIZE(entry.get()) != 5) {
			if (entry) {
				PyErr_SetString(PyExc_TypeError, "Invalid plugin metadata");
			}
			return std::nullopt;
		}
		const auto fields = PySequence_Fast_ITEMS(entry.get());
		const auto id = ReadPythonString(fields[0]);
		const auto name = ReadPythonString(fields[1]);
		const auto version = ReadPythonString(fields[2]);
		const auto error = ReadPythonString(fields[4]);
		if (!id || !name || !version || !error || !PyBool_Check(fields[3])) {
			if (!PyErr_Occurred()) {
				PyErr_SetString(PyExc_TypeError, "Invalid plugin metadata fields");
			}
			return std::nullopt;
		}
		result.push_back({
			.id = *id,
			.name = *name,
			.version = *version,
			.error = *error,
			.enabled = (fields[3] == Py_True),
		});
	}
	return result;
}

std::optional<std::vector<MessageMenuItem>> ReadMessageMenuSnapshot(
		PyObject *object) {
	const auto entries = PythonObject(PySequence_Fast(
		object, "Menu item list must be a sequence"));
	if (!entries) {
		return std::nullopt;
	}
	auto result = std::vector<MessageMenuItem>();
	const auto count = PySequence_Fast_GET_SIZE(entries.get());
	const auto values = PySequence_Fast_ITEMS(entries.get());
	result.reserve(count);
	for (auto i = Py_ssize_t(); i != count; ++i) {
		const auto entry = PythonObject(PySequence_Fast(
			values[i], "Menu item must be a sequence"));
		if (!entry || PySequence_Fast_GET_SIZE(entry.get()) != 3) {
			if (entry) {
				PyErr_SetString(PyExc_TypeError, "Invalid menu item");
			}
			return std::nullopt;
		}
		const auto fields = PySequence_Fast_ITEMS(entry.get());
		const auto id = ReadPythonString(fields[0]);
		const auto text = ReadPythonString(fields[1]);
		const auto priority = PyLong_AsLong(fields[2]);
		if (!id || !text || PyErr_Occurred()
			|| priority < std::numeric_limits<int>::min()
			|| priority > std::numeric_limits<int>::max()) {
			if (!PyErr_Occurred()) {
				PyErr_SetString(PyExc_ValueError, "Invalid menu item fields");
			}
			return std::nullopt;
		}
		result.push_back({ *id, *text, int(priority) });
	}
	return result;
}

}

struct Host::Private {
	struct SendRequest {
		quint64 accountId;
		QString text;
		SendCallback done;
	};
	struct RequestJob {
		RequestEvent event;
		RequestCallback done;
	};
	struct MenuClickRequest {
		QString id;
		MessageEvent message;
	};
	struct SettingsRequest {
		SettingsCallback done;
	};
	struct PluginsRequest {
		PluginsCallback done;
	};
	struct PluginEnabledRequest {
		QString pluginId;
		bool enabled = false;
		PluginEnabledCallback done;
	};
	struct SettingRequest {
		QString pluginId;
		QString key;
		SettingValue value;
		SettingCallback done;
	};
	using Job = std::variant<
		UpdateEvent,
		SendRequest,
		RequestJob,
		RequestResultEvent,
		MenuClickRequest,
		SettingsRequest,
		PluginsRequest,
		PluginEnabledRequest,
		SettingRequest>;
	Logger logger;
	ToastCallback toastCallback;
	QThread *owner = QThread::currentThread();
	PyObject *runtime = nullptr;
	PyObject *manager = nullptr;
	PyThreadState *threadState = nullptr;
	bool initialized = false;
	int loaded = 0;
	std::vector<MessageMenuItem> menuItems;
	std::mutex mutex;
	std::condition_variable ready;
	std::deque<Job> queue;
	std::unique_ptr<QObject> delivery;
	std::function<void(QString)> toastRequest;
	std::thread worker;
	bool accepting = false;
};

Host::Host(Logger logger, ToastCallback toast)
: _private(std::make_unique<Private>()) {
	_private->logger = std::move(logger);
	_private->toastCallback = std::move(toast);
}

Host::~Host() {
	stop();
}

bool Host::start(const QString &directory) {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	if (_private->initialized || Py_IsInitialized()) {
		_private->logger(u"Plugin Host: interpreter is already initialized"_q);
		return false;
	}
	if (!QDir().mkpath(directory)) {
		_private->logger(u"Plugin Host: cannot create directory %1"_q
			.arg(directory));
		return false;
	}
	_private->delivery = std::make_unique<QObject>();
	_private->toastRequest = [this](QString text) {
		if (!_private->delivery) {
			return;
		}
		const auto callback = _private->toastCallback;
		QMetaObject::invokeMethod(
			_private->delivery.get(),
			[callback, text = std::move(text)] {
				if (callback) {
					callback(text);
				}
			},
			Qt::QueuedConnection);
	};
	RuntimeToast = &_private->toastRequest;

	auto config = PyConfig();
	PyConfig_InitIsolatedConfig(&config);
	config.install_signal_handlers = 0;
	config.parse_argv = 0;
	config.write_bytecode = 0;
	config.site_import = 0;
	const auto status = Py_InitializeFromConfig(&config);
	const auto error = PyStatus_Exception(status)
		? QString::fromUtf8(status.err_msg ? status.err_msg : "Initialization failed")
		: QString();
	PyConfig_Clear(&config);
	if (PyStatus_Exception(status)) {
		_private->logger(u"Plugin Host: CPython initialization failed: %1"_q
			.arg(error));
		if (Py_IsInitialized()) {
			Py_FinalizeEx();
		}
		RuntimeToast = nullptr;
		_private->delivery.reset();
		return false;
	}
	_private->initialized = true;
	_private->logger(u"Plugin Host: embedded CPython %1; directory %2"_q
		.arg(QString::fromUtf8(Py_GetVersion()), directory));

	auto success = false;
	{
		_private->runtime = PyModule_New("_moegram_runtime");
		RuntimeLogger = &_private->logger;
		const auto bridge = PythonObject(PyCFunction_New(&LogMethod, nullptr));
		const auto toastBridge = PythonObject(
			PyCFunction_New(&ToastMethod, nullptr));
		const auto globals = _private->runtime
			? PyModule_GetDict(_private->runtime)
			: nullptr;
		const auto registered = globals && bridge && toastBridge
			&& (PyDict_SetItemString(globals, "_write_log", bridge.get()) == 0)
			&& (PyDict_SetItemString(
				globals, "_show_toast", toastBridge.get()) == 0)
			&& (PyDict_SetItemString(
				PyImport_GetModuleDict(), "_moegram_runtime", _private->runtime) == 0);
		const auto executed = registered
			? PythonObject(PyRun_String(
				PluginRuntimeSource, Py_file_input, globals, globals))
			: PythonObject();
		const auto hostClass = executed
			? PyDict_GetItemString(globals, "PluginHost")
			: nullptr;
		_private->manager = hostClass ? PyObject_CallNoArgs(hostClass) : nullptr;
		const auto encoded = QDir(directory).absolutePath().toUtf8();
		const auto path = PythonObject(PyUnicode_DecodeUTF8(
			encoded.constData(), encoded.size(), "strict"));
		const auto result = (_private->manager && path)
			? PythonObject(PyObject_CallMethod(
				_private->manager, "load", "O", path.get()))
			: PythonObject();
		if (result) {
			_private->loaded = int(PyLong_AsLong(result.get()));
			success = !PyErr_Occurred();
		}
		const auto menuItems = success
			? PythonObject(PyObject_CallMethod(
				_private->manager, "message_menu_items", nullptr))
			: PythonObject();
		const auto sequence = menuItems
			? PythonObject(PySequence_Fast(
				menuItems.get(), "Menu item list must be a sequence"))
			: PythonObject();
		if (success && !sequence) {
			success = false;
		}
		if (success) {
			const auto size = PySequence_Fast_GET_SIZE(sequence.get());
			const auto values = PySequence_Fast_ITEMS(sequence.get());
			for (auto i = Py_ssize_t(); i != size; ++i) {
				const auto tuple = values[i];
				if (!PyTuple_Check(tuple) || PyTuple_Size(tuple) != 3) {
					PyErr_SetString(PyExc_TypeError, "Invalid menu item descriptor");
					success = false;
					break;
				}
				const auto id = ReadPythonString(PyTuple_GetItem(tuple, 0));
				const auto text = ReadPythonString(PyTuple_GetItem(tuple, 1));
				const auto priorityValue = PyLong_AsLong(PyTuple_GetItem(tuple, 2));
				if (!id || !text || PyErr_Occurred()
					|| priorityValue < std::numeric_limits<int>::min()
					|| priorityValue > std::numeric_limits<int>::max()) {
					if (!PyErr_Occurred()) {
						PyErr_SetString(PyExc_ValueError, "Invalid menu item descriptor");
					}
					success = false;
					break;
				}
				_private->menuItems.push_back({
					*id,
					*text,
					int(priorityValue),
				});
			}
		}
		if (success) {
			_private->logger(u"Plugin Host: registered %1 message context menu item(s)"_q
				.arg(_private->menuItems.size()));
		}
		if (!success) {
			_private->logger(u"Plugin Host: startup failed: %1"_q
				.arg(TakePythonError()));
		}
	}
	_private->threadState = PyEval_SaveThread();
	if (!success) {
		stop();
	}
	if (success) {
		_private->accepting = true;
		_private->worker = std::thread([this] {
			while (true) {
				auto job = Private::Job(UpdateEvent());
				{
					auto lock = std::unique_lock(_private->mutex);
					_private->ready.wait(lock, [this] {
						return !_private->accepting || !_private->queue.empty();
					});
					if (!_private->accepting && _private->queue.empty()) {
						break;
					}
					job = std::move(_private->queue.front());
					_private->queue.pop_front();
				}
				const auto gil = PyGILState_Ensure();
				if (auto menu = std::get_if<Private::MenuClickRequest>(&job)) {
					{
						const auto idText = menu->id.toUtf8();
						const auto id = PythonObject(PyUnicode_DecodeUTF8(
							idText.constData(), idText.size(), "strict"));
						const auto payload = MessagePayload(menu->message);
						const auto result = (id && payload)
							? PythonObject(PyObject_CallMethod(
								_private->manager,
								"dispatch_menu_item",
								"OO",
								id.get(),
								payload.get()))
							: PythonObject();
						if (!result) {
							_private->logger(u"Plugin Host: menu dispatch failed: %1"_q
								.arg(TakePythonError()));
						}
					}
					PyGILState_Release(gil);
					continue;
				}
				if (auto settings = std::get_if<Private::SettingsRequest>(&job)) {
					auto pages = std::optional<std::vector<PluginSettings>>();
					{
						const auto result = PythonObject(PyObject_CallMethod(
							_private->manager, "settings_snapshot", nullptr));
						if (result) {
							pages = ReadSettingsSnapshot(result.get());
						}
						if (!pages) {
							_private->logger(u"Plugin Host: settings snapshot failed: %1"_q
								.arg(TakePythonError()));
							pages.emplace();
						}
					}
					PyGILState_Release(gil);
					QMetaObject::invokeMethod(_private->delivery.get(),
						[done = std::move(settings->done),
							pages = std::move(*pages)]() mutable {
							if (done) {
								done(std::move(pages));
							}
						}, Qt::QueuedConnection);
					continue;
				}
				if (auto plugins = std::get_if<Private::PluginsRequest>(&job)) {
					auto snapshot = std::optional<std::vector<PluginInfo>>();
					{
						const auto result = PythonObject(PyObject_CallMethod(
							_private->manager, "plugins_snapshot", nullptr));
						if (result) {
							snapshot = ReadPluginsSnapshot(result.get());
						}
						if (!snapshot) {
							_private->logger(u"Plugin Host: plugin list failed: %1"_q
								.arg(TakePythonError()));
							snapshot.emplace();
						}
					}
					PyGILState_Release(gil);
					QMetaObject::invokeMethod(_private->delivery.get(),
						[done = std::move(plugins->done),
							plugins = std::move(*snapshot)]() mutable {
							if (done) {
								done(std::move(plugins));
							}
						}, Qt::QueuedConnection);
					continue;
				}
				if (auto plugin = std::get_if<Private::PluginEnabledRequest>(&job)) {
					auto changed = false;
					auto plugins = std::optional<std::vector<PluginInfo>>();
					auto menuItems = std::optional<std::vector<MessageMenuItem>>();
					{
						const auto idBytes = plugin->pluginId.toUtf8();
						const auto id = PythonObject(PyUnicode_DecodeUTF8(
							idBytes.constData(), idBytes.size(), "strict"));
						const auto enabled = PythonObject(
							PyBool_FromLong(plugin->enabled));
						const auto result = (id && enabled)
							? PythonObject(PyObject_CallMethod(
								_private->manager,
								"set_plugin_enabled",
								"sO",
								idBytes.constData(),
								enabled.get()))
							: PythonObject();
						if (result) {
							const auto truth = PyObject_IsTrue(result.get());
							if (truth >= 0) {
								changed = (truth != 0);
							}
						}
						const auto pluginResult = PythonObject(PyObject_CallMethod(
							_private->manager, "plugins_snapshot", nullptr));
						if (pluginResult) {
							plugins = ReadPluginsSnapshot(pluginResult.get());
						}
						const auto menuResult = PythonObject(PyObject_CallMethod(
							_private->manager, "message_menu_items", nullptr));
						if (menuResult) {
							menuItems = ReadMessageMenuSnapshot(menuResult.get());
						}
						if (PyErr_Occurred()) {
							_private->logger(u"Plugin Host: plugin state update failed: %1"_q
								.arg(TakePythonError()));
						}
					}
					PyGILState_Release(gil);
					QMetaObject::invokeMethod(_private->delivery.get(),
						[this,
							done = std::move(plugin->done),
							changed,
							plugins = std::move(plugins),
							menuItems = std::move(menuItems)]() mutable {
							if (plugins && menuItems) {
								_private->loaded = int(std::ranges::count_if(
									*plugins,
									[](const auto &entry) { return entry.enabled; }));
								_private->menuItems = std::move(*menuItems);
								if (done) {
									done(changed, std::move(*plugins));
								}
							} else if (done) {
								done(false, {});
							}
						}, Qt::QueuedConnection);
					continue;
				}
				if (auto setting = std::get_if<Private::SettingRequest>(&job)) {
					auto reload = false;
					{
						const auto pluginBytes = setting->pluginId.toUtf8();
						const auto keyBytes = setting->key.toUtf8();
						const auto pluginId = PythonObject(PyUnicode_DecodeUTF8(
							pluginBytes.constData(), pluginBytes.size(), "strict"));
						const auto key = PythonObject(PyUnicode_DecodeUTF8(
							keyBytes.constData(), keyBytes.size(), "strict"));
						const auto value = std::visit([](const auto &item) {
							using Type = std::decay_t<decltype(item)>;
							if constexpr (std::is_same_v<Type, QString>) {
								const auto bytes = item.toUtf8();
								return PythonObject(PyUnicode_DecodeUTF8(
									bytes.constData(), bytes.size(), "strict"));
							} else {
								return PythonObject(PyBool_FromLong(item));
							}
						}, setting->value);
						const auto result = (pluginId && key && value)
							? PythonObject(PyObject_CallMethod(
								_private->manager,
								"update_setting",
								"OOO",
								pluginId.get(),
								key.get(),
								value.get()))
							: PythonObject();
						if (result) {
							const auto truth = PyObject_IsTrue(result.get());
							if (truth >= 0) {
								reload = (truth != 0);
							}
						}
						if (PyErr_Occurred()) {
							_private->logger(u"Plugin Host: setting update failed: %1"_q
								.arg(TakePythonError()));
						}
					}
					PyGILState_Release(gil);
					if (setting->done) {
						QMetaObject::invokeMethod(_private->delivery.get(),
							[done = std::move(setting->done), reload] {
								done(reload);
							}, Qt::QueuedConnection);
					}
					continue;
				}
				if (auto request = std::get_if<Private::RequestJob>(&job)) {
					auto filtered = std::optional<RequestEvent>(request->event);
					{
						const auto offline = PythonObject(request->event.offline
							? PyBool_FromLong(*request->event.offline) : Py_NewRef(Py_None));
						const auto name = request->event.name.toUtf8();
						const auto result = offline
							? PythonObject(PyObject_CallMethod(_private->manager,
								"dispatch_request", "sKO", name.constData(),
								static_cast<unsigned long long>(request->event.accountId), offline.get()))
							: PythonObject();
						if (result && result.get() == Py_None) {
							filtered.reset();
						} else if (result && PyTuple_Check(result.get())
							&& PyTuple_Size(result.get()) == 1) {
							const auto value = PyTuple_GetItem(result.get(), 0);
							if (request->event.offline && PyBool_Check(value)) {
								filtered->offline = (value == Py_True);
							} else if (request->event.offline || value != Py_None) {
								PyErr_SetString(PyExc_TypeError, "Invalid request DTO result");
							}
						} else if (result) {
							PyErr_SetString(PyExc_TypeError, "Invalid request dispatcher result");
						}
						if (PyErr_Occurred()) {
							filtered = request->event;
							_private->logger(u"Plugin Host: request dispatch failed: %1"_q
								.arg(TakePythonError()));
						}
					}
					PyGILState_Release(gil);
					QMetaObject::invokeMethod(_private->delivery.get(),
						[done = std::move(request->done), filtered = std::move(filtered)] {
							done(filtered);
						}, Qt::QueuedConnection);
					continue;
				}
				if (auto response = std::get_if<RequestResultEvent>(&job)) {
					{
						const auto value = PythonObject(response->value
							? PyBool_FromLong(*response->value) : Py_NewRef(Py_None));
						const auto code = PythonObject(response->errorCode
							? PyLong_FromLong(*response->errorCode) : Py_NewRef(Py_None));
						const auto encoded = response->errorText.toUtf8();
						const auto text = PythonObject(PyUnicode_DecodeUTF8(
							encoded.constData(), encoded.size(), "strict"));
						const auto name = response->name.toUtf8();
						const auto result = (value && code && text)
							? PythonObject(PyObject_CallMethod(_private->manager,
								"dispatch_response", "sKOOO", name.constData(),
								static_cast<unsigned long long>(response->accountId),
								value.get(), code.get(), text.get()))
							: PythonObject();
						if (!result) {
							_private->logger(u"Plugin Host: response dispatch failed: %1"_q
								.arg(TakePythonError()));
						}
					}
					PyGILState_Release(gil);
					continue;
				}
				if (auto request = std::get_if<Private::SendRequest>(&job)) {
					auto filtered = std::optional<QString>(request->text);
					{
						const auto encoded = request->text.toUtf8();
						const auto text = PythonObject(PyUnicode_DecodeUTF8(
							encoded.constData(), encoded.size(), "strict"));
						const auto result = text
							? PythonObject(PyObject_CallMethod(_private->manager,
								"dispatch_send", "KO",
								static_cast<unsigned long long>(request->accountId), text.get()))
							: PythonObject();
						if (result && result.get() == Py_None) {
							filtered = std::nullopt;
						} else if (result && PyUnicode_Check(result.get())) {
							auto length = Py_ssize_t();
							const auto value = PyUnicode_AsUTF8AndSize(result.get(), &length);
							if (value) {
								filtered = QString::fromUtf8(value, length);
							}
						} else if (result) {
							PyErr_SetString(PyExc_TypeError, "Send dispatcher must return str or None");
						}
						if (PyErr_Occurred()) {
							_private->logger(u"Plugin Host: send dispatch failed: %1"_q
								.arg(TakePythonError()));
						}
					}
					PyGILState_Release(gil);
					QMetaObject::invokeMethod(_private->delivery.get(),
						[done = std::move(request->done), filtered = std::move(filtered)] {
							done(filtered);
						}, Qt::QueuedConnection);
					continue;
				}
				const auto &event = std::get<UpdateEvent>(job);
				{
					const auto payloads = PythonObject(PyList_New(0));
					auto valid = bool(payloads);
					for (const auto &message : event.messages) {
						const auto payload = PythonObject(PyDict_New());
						const auto put = [&](const char *key, PythonObject value) {
							return payload && value
								&& PyDict_SetItemString(payload.get(), key, value.get()) == 0;
						};
						const auto none = [] { return PythonObject(Py_NewRef(Py_None)); };
						const auto integer = [&](std::optional<qint64> value) {
							return value
								? PythonObject(PyLong_FromLongLong(*value)) : none();
						};
						const auto encoded = message.text.value_or(QString()).toUtf8();
						valid = valid
							&& put("account_id", PythonObject(
								PyLong_FromUnsignedLongLong(event.accountId)))
							&& put("message_id", integer(message.messageId))
							&& put("chat_id", integer(message.chatId))
							&& put("sender_id", integer(message.senderId))
							&& put("text", message.text
								? PythonObject(PyUnicode_DecodeUTF8(
									encoded.constData(), encoded.size(), "strict"))
								: none())
							&& put("outgoing", message.outgoing
								? PythonObject(PyBool_FromLong(*message.outgoing)) : none())
							&& PyList_Append(payloads.get(), payload.get()) == 0;
						if (!valid) {
							break;
						}
					}
					const auto pts = event.pts
						? PythonObject(PyLong_FromLong(*event.pts))
						: PythonObject(Py_NewRef(Py_None));
					const auto count = event.ptsCount
						? PythonObject(PyLong_FromLong(*event.ptsCount))
						: PythonObject(Py_NewRef(Py_None));
					const auto name = event.name.toUtf8();
					const auto result = (valid && pts && count)
						? PythonObject(PyObject_CallMethod(_private->manager,
							"dispatch_update", "sOOOi", name.constData(), payloads.get(),
							pts.get(), count.get(), int(event.native)))
						: PythonObject();
					if (!result) {
						_private->logger(u"Plugin Host: event dispatch failed: %1"_q
							.arg(TakePythonError()));
					}
				}
				PyGILState_Release(gil);
			}
		});
		ActiveHost = this;
	}
	return success;
}

void Host::stop() {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	if (!_private->initialized) {
		return;
	}
	if (ActiveHost == this) {
		ActiveHost = nullptr;
	}
	_private->menuItems.clear();
	{
		const auto lock = std::lock_guard(_private->mutex);
		_private->accepting = false;
		auto pendingSettings = std::deque<Private::Job>();
		for (auto &job : _private->queue) {
			if (std::holds_alternative<Private::SettingRequest>(job)) {
				pendingSettings.push_back(std::move(job));
			}
		}
		_private->queue = std::move(pendingSettings);
	}
	_private->ready.notify_one();
	if (_private->worker.joinable()) {
		_private->worker.join();
	}
	_private->delivery.reset();
	PyEval_RestoreThread(_private->threadState);
	_private->threadState = nullptr;
	if (_private->manager) {
		const auto result = PythonObject(PyObject_CallMethod(
			_private->manager, "stop", nullptr));
		if (!result) {
			_private->logger(u"Plugin Host: shutdown failed: %1"_q
				.arg(TakePythonError()));
		}
	}
	RuntimeToast = nullptr;
	Py_CLEAR(_private->manager);
	Py_CLEAR(_private->runtime);
	_private->loaded = 0;
	const auto result = Py_FinalizeEx();
	RuntimeLogger = nullptr;
	_private->initialized = false;
	_private->logger((result == 0)
		? u"Plugin Host: CPython stopped"_q
		: u"Plugin Host: CPython stopped with a finalization error"_q);
}

namespace {

UpdateEvent ToUpdate(MessageEvent event) {
	auto result = UpdateEvent();
	result.accountId = event.accountId;
	const auto channel = event.chatId && *event.chatId <= -1000000000000LL;
	result.name = (event.kind == EventKind::Message)
		? (channel ? u"TL_updateNewChannelMessage"_q : u"TL_updateNewMessage"_q)
		: (event.kind == EventKind::Edit)
		? (channel ? u"TL_updateEditChannelMessage"_q : u"TL_updateEditMessage"_q)
		: (channel ? u"TL_updateDeleteChannelMessages"_q : u"TL_updateDeleteMessages"_q);
	result.messages.push_back(std::move(event));
	return result;
}

}

bool Host::post(MessageEvent event) {
	return post(ToUpdate(std::move(event)));
}

bool Host::post(UpdateEvent event) {
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->accepting || _private->queue.size() >= 4096) {
		return false;
	}
	_private->queue.push_back(std::move(event));
	_private->ready.notify_one();
	return true;
}

bool Host::filterOutgoing(quint64 accountId, QString text, SendCallback done) {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->accepting || !_private->loaded || _private->queue.size() >= 4096) {
		return false;
	}
	_private->queue.push_back(Private::SendRequest{
		accountId, std::move(text), std::move(done) });
	_private->ready.notify_one();
	return true;
}

void FilterOutgoing(quint64 accountId, QString text, SendCallback done) {
	if (!ActiveHost || !ActiveHost->filterOutgoing(accountId, text, done)) {
		done(text);
	}
}

bool Host::filterRequest(RequestEvent event, RequestCallback done) {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->accepting || !_private->loaded || _private->queue.size() >= 4096) {
		return false;
	}
	_private->queue.push_back(Private::RequestJob{ std::move(event), std::move(done) });
	_private->ready.notify_one();
	return true;
}

bool Host::post(RequestResultEvent event) {
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->accepting || _private->queue.size() >= 4096) {
		return false;
	}
	_private->queue.push_back(std::move(event));
	_private->ready.notify_one();
	return true;
}

std::vector<MessageMenuItem> Host::messageMenuItems() const {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	return _private->menuItems;
}

bool Host::invokeMessageMenuItem(QString id, MessageEvent message) {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->accepting
		|| !_private->loaded
		|| _private->queue.size() >= 4096
		|| !std::any_of(
			_private->menuItems.begin(),
			_private->menuItems.end(),
			[&](const auto &item) { return item.id == id; })) {
		return false;
	}
	_private->queue.push_back(Private::MenuClickRequest{
		std::move(id),
		std::move(message),
	});
	_private->ready.notify_one();
	return true;
}

bool Host::requestSettings(SettingsCallback done) {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->accepting || !_private->loaded || _private->queue.size() >= 4096) {
		return false;
	}
	_private->queue.push_back(Private::SettingsRequest{ std::move(done) });
	_private->ready.notify_one();
	return true;
}

bool Host::requestPlugins(PluginsCallback done) {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->accepting || _private->queue.size() >= 4096) {
		return false;
	}
	_private->queue.push_back(Private::PluginsRequest{ std::move(done) });
	_private->ready.notify_one();
	return true;
}

bool Host::setPluginEnabled(
		QString pluginId,
		bool enabled,
		PluginEnabledCallback done) {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->accepting || _private->queue.size() >= 4096) {
		return false;
	}
	_private->queue.push_back(Private::PluginEnabledRequest{
		std::move(pluginId),
		enabled,
		std::move(done),
	});
	_private->ready.notify_one();
	return true;
}

bool Host::updateSetting(
		QString pluginId,
		QString key,
		SettingValue value,
		SettingCallback done) {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->accepting || !_private->loaded || _private->queue.size() >= 4096) {
		return false;
	}
	_private->queue.push_back(Private::SettingRequest{
		std::move(pluginId),
		std::move(key),
		std::move(value),
		std::move(done),
	});
	_private->ready.notify_one();
	return true;
}

std::vector<MessageMenuItem> MessageMenuItems() {
	return ActiveHost ? ActiveHost->messageMenuItems()
		: std::vector<MessageMenuItem>();
}

bool InvokeMessageMenuItem(QString id, MessageEvent message) {
	return ActiveHost
		&& ActiveHost->invokeMessageMenuItem(std::move(id), std::move(message));
}

bool RequestPluginSettings(Host::SettingsCallback done) {
	return ActiveHost && ActiveHost->requestSettings(std::move(done));
}

bool RequestPlugins(Host::PluginsCallback done) {
	return ActiveHost && ActiveHost->requestPlugins(std::move(done));
}

bool SetPluginEnabled(
		QString pluginId,
		bool enabled,
		Host::PluginEnabledCallback done) {
	return ActiveHost && ActiveHost->setPluginEnabled(
		std::move(pluginId),
		enabled,
		std::move(done));
}

bool UpdatePluginSetting(
		QString pluginId,
		QString key,
		Host::SettingValue value,
		Host::SettingCallback done) {
	return ActiveHost && ActiveHost->updateSetting(
		std::move(pluginId),
		std::move(key),
		std::move(value),
		std::move(done));
}

bool RequestHooksEnabled() {
	return ActiveHost != nullptr;
}

void FilterRequest(RequestEvent event, RequestCallback done) {
	if (!ActiveHost || !ActiveHost->filterRequest(event, done)) {
		done(event);
	}
}

void Publish(RequestResultEvent event) {
	if (ActiveHost && !ActiveHost->post(std::move(event))) {
		qWarning("Plugin Host: event queue full; response dropped");
	}
}

NativeEventScope::NativeEventScope(bool active) : _active(active) {
	if (_active) {
		++NativeEventDepth;
	}
}

NativeEventScope::~NativeEventScope() {
	if (_active) {
		--NativeEventDepth;
	}
}

bool ModelEventsEnabled() {
	return ActiveHost && !NativeEventDepth;
}

void Publish(MessageEvent event) {
	Publish(ToUpdate(std::move(event)));
}

void Publish(UpdateEvent event) {
	if (NativeEventDepth) {
		return;
	}
	if (ActiveHost && !ActiveHost->post(std::move(event))) {
		qWarning("Plugin Host: event queue full; update dropped");
	}
}

int Host::loadedCount() const {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	return _private->loaded;
}

}
