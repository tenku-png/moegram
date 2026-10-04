#include <Python.h>

#include "plugins/plugin_host.h"

#include "base/basic_types.h"
#include "runtime_source.h"

#include <QtCore/QDir>
#include <QtCore/QThread>

namespace MoeGram::Plugins {
namespace {

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

PyObject *WriteLog(PyObject *self, PyObject *args) {
	const char *message = nullptr;
	if (!PyArg_ParseTuple(args, "s", &message)) {
		return nullptr;
	}
	const auto logger = static_cast<Host::Logger*>(
		PyCapsule_GetPointer(self, "moegram.logger"));
	if (!logger) {
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
	Logger logger;
	QThread *owner = QThread::currentThread();
	PyObject *runtime = nullptr;
	PyObject *manager = nullptr;
	PyThreadState *threadState = nullptr;
	bool initialized = false;
	int loaded = 0;
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
		const auto capsule = PythonObject(PyCapsule_New(
			&_private->logger, "moegram.logger", nullptr));
		const auto bridge = capsule
			? PythonObject(PyCFunction_New(&LogMethod, capsule.get()))
			: PythonObject();
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
	return success;
}

void Host::stop() {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	if (!_private->initialized) {
		return;
	}
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
	_private->initialized = false;
	_private->logger((result == 0)
		? u"Plugin Host: CPython stopped"_q
		: u"Plugin Host: CPython stopped with a finalization error"_q);
}

int Host::loadedCount() const {
	Q_ASSERT(QThread::currentThread() == _private->owner);
	return _private->loaded;
}

}
