#include <Python.h>

#include "plugins/plugin_host.h"

#include "base/basic_types.h"
#include "runtime_source.h"

#include <QtCore/QDir>
#include <QtCore/QThread>
#include <QtCore/QObject>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <variant>

namespace MoeGram::Plugins {
namespace {

Host *ActiveHost = nullptr;
Host::Logger *RuntimeLogger = nullptr;
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
	using Job = std::variant<UpdateEvent, SendRequest, RequestJob, RequestResultEvent>;
	Logger logger;
	QThread *owner = QThread::currentThread();
	PyObject *runtime = nullptr;
	PyObject *manager = nullptr;
	PyThreadState *threadState = nullptr;
	bool initialized = false;
	int loaded = 0;
	std::mutex mutex;
	std::condition_variable ready;
	std::deque<Job> queue;
	std::unique_ptr<QObject> delivery;
	std::thread worker;
	bool accepting = false;
};

Host::Host(Logger logger)
: _private(std::make_unique<Private>()) {
	_private->logger = std::move(logger);
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
		const auto globals = _private->runtime
			? PyModule_GetDict(_private->runtime)
			: nullptr;
		const auto registered = globals && bridge
			&& (PyDict_SetItemString(globals, "_write_log", bridge.get()) == 0)
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
		_private->delivery = std::make_unique<QObject>();
		_private->accepting = true;
		_private->worker = std::thread([this] {
			while (true) {
				auto job = Private::Job(UpdateEvent());
				{
					auto lock = std::unique_lock(_private->mutex);
					_private->ready.wait(lock, [this] {
						return !_private->accepting || !_private->queue.empty();
					});
					if (!_private->accepting) {
						break;
					}
					job = std::move(_private->queue.front());
					_private->queue.pop_front();
				}
				const auto gil = PyGILState_Ensure();
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
	{
		const auto lock = std::lock_guard(_private->mutex);
		_private->accepting = false;
		_private->queue.clear();
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
