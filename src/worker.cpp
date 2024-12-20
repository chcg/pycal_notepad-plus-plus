#include "worker.h"


PyObject* input(PyObject* self, PyObject* args) {

    unique_lock lock(worker->input_mutex);
    worker->input_cv.wait(lock, []{ return !worker->stdin_queue.empty(); });

    string input;
    if (worker->stdin_queue.empty()) {
        return PyUnicode_FromString(input.c_str());
    }

    input = worker->stdin_queue.front();
    worker->stdin_queue.pop();

    return PyUnicode_FromString(input.c_str());
}

void Worker::overrideInput() {

    static PyMethodDef methods[] = {
        {"input", input, METH_VARARGS, ""},
        {nullptr, nullptr, 0, nullptr}
    };

    static struct PyModuleDef module_def = {
        PyModuleDef_HEAD_INIT,
        "override_input",
        "module",
        -1,
        methods
    };

    PyObject* module = PyModule_Create(&module_def);
    if (module == nullptr) {
        return;
    }

    PyObject* input_func = PyCFunction_New(methods, nullptr);
    if (input_func == nullptr) {
        Py_DECREF(module);
        return;
    }

    PyModule_AddObject(module, "input",input_func);

    PyObject* builtins = PyImport_ImportModule("builtins");
    if (builtins == nullptr) {
        Py_DECREF(module);
        return;
    }

    PyObject_SetAttrString(builtins, "input", input_func);
    Py_DECREF(builtins);
    Py_DECREF(module);
}

void Worker::captureStdoutStderr(const string& code) {

    PyObject* io_module = PyImport_ImportModule("io");
    if (io_module == nullptr) {
        return;
    }

    PyObject* string_io = PyObject_GetAttrString(io_module, "StringIO");
    if (string_io == nullptr) {
        Py_DECREF(io_module);
        return;
    }

    PyObject* stdout_io = PyObject_CallObject(string_io, nullptr);
    if (stdout_io == nullptr) {
        Py_DECREF(string_io);
        Py_DECREF(io_module);
        return;
    }

    PyObject* stderr_io = PyObject_CallObject(string_io, nullptr);
    if (stderr_io == nullptr) {
        Py_DECREF(stdout_io);
        Py_DECREF(string_io);
        Py_DECREF(io_module);
        return;
    }

    PySys_SetObject("stdout", stdout_io);
    PySys_SetObject("stderr", stderr_io);

    PyRun_SimpleString(code.c_str());

    PyObject* stdout_value = PyObject_GetAttrString(stdout_io, "getvalue");
    if (stdout_value == nullptr) {
        Py_DECREF(stderr_io);
        Py_DECREF(stdout_io);
        Py_DECREF(string_io);
        Py_DECREF(io_module);
        return;
    }

    PyObject* stderr_value = PyObject_GetAttrString(stderr_io, "getvalue");
    if (stderr_value == nullptr) {
        Py_DECREF(stdout_value);
        Py_DECREF(stderr_io);
        Py_DECREF(stdout_io);
        Py_DECREF(string_io);
        Py_DECREF(io_module);
        return;
    }

    PyObject* stdout_obj = PyObject_CallObject(stdout_value, nullptr);
    if (stdout_obj == nullptr) {
        Py_DECREF(stderr_value);
        Py_DECREF(stdout_value);
        Py_DECREF(stderr_io);
        Py_DECREF(stdout_io);
        Py_DECREF(string_io);
        Py_DECREF(io_module);
        return;
    }

    string stdout_str(PyUnicode_AsUTF8(stdout_obj));

    PyObject* stderr_obj = PyObject_CallObject(stderr_value, nullptr);
    if (stderr_obj == nullptr) {
        Py_DECREF(stdout_obj);
        Py_DECREF(stderr_value);
        Py_DECREF(stdout_value);
        Py_DECREF(stderr_io);
        Py_DECREF(stdout_io);
        Py_DECREF(string_io);
        Py_DECREF(io_module);
        return;
    }
    string stderr_str(PyUnicode_AsUTF8(stderr_obj));

    Py_DECREF(stderr_obj);
    Py_DECREF(stdout_obj);
    Py_DECREF(stderr_value);
    Py_DECREF(stdout_value);
    Py_DECREF(stderr_io);
    Py_DECREF(stdout_io);
    Py_DECREF(string_io);
    Py_DECREF(io_module);

    std::unique_lock lock(worker->output_mutex);
    worker->stdout_queue.push(stdout_str);
    worker->stderr_queue.push(stderr_str);
    worker->heartbeat.reset();
}

void Worker::process(const std::stop_token& stop_token) {

    if (!Py_IsInitialized()) {
        Py_Initialize();
    }

    auto gstate = PyGILState_Ensure();
    auto thread_state = PyThreadState_Get();
    PyInterpreterState* interpreter_state = thread_state->interp;
    py_thread_state = PyThreadState_New(interpreter_state);

    overrideInput();

    string init = R"(
import code
import sys
from contextlib import redirect_stdout
from io import StringIO


stdin = """
import sys
from io import BytesIO, TextIOWrapper


class Stdin(TextIOWrapper):
    def __init__(self, *args, **kwargs):
        super().__init__(BytesIO(), *args, **kwargs)


input = lambda *x: ""
sys.stdin = Stdin()
sys.__stdin__ = Stdin()
"""

help = """
def __help__():
    print('''Welcome to Python 3.8's help utility!

If this is your first time using Python, you should definitely check out
the tutorial on the internet at https://docs.python.org/3.8/tutorial/.''')

help = __help__
"""


def interact():
    global repl

    line = input()
    if not line:
        return

    stdout = StringIO()

    multiline, line = line[0], line[1:].rstrip("\r\n")
    with redirect_stdout(stdout):
        try:
            if multiline == "1":
                repl.resetbuffer()
                repl.runcode(line)
            else:
                repl.push(line)
        except BaseException as e:
            sys.stderr.write(repr(e))
    buffer = stdout.getvalue()
    if buffer != f"{line.strip()}\n":
        print(buffer, end="")


repl = code.InteractiveConsole()
repl.runcode(stdin)
repl.runcode(help)
)";

    captureStdoutStderr(init);

    while (!stop_token.stop_requested()) {
        captureStdoutStderr("interact()");
    }

    PyThreadState_Clear(thread_state);
    PyThreadState_Delete(thread_state);
    PyGILState_Release(gstate);

    Py_Finalize();
}

unique_ptr<chrono::time_point<chrono::steady_clock>> steady_now() {
    return make_unique<chrono::time_point<chrono::steady_clock>>(chrono::steady_clock::now());
}
