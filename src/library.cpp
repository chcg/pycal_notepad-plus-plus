#include "entry.h"
#include "library.h"
#include "worker.h"
#include "resource.h"

#include <codecvt>
#include <locale>
#include <regex>
#include <string>


constexpr TCHAR PLUGIN_NAME[] = TEXT("pycalc");
constexpr int NP_FUNC = 2;

constexpr int CMD_ENABLED = 0;
constexpr int CMD_SELECTED = 1;

constexpr WCHAR KEY_PYCALC[] = L"Software\\pycalc";
constexpr WCHAR KEY_ENABLED[] = L"enabled";

constexpr int TIMER_PRINT = 0x02;
constexpr int TIMER_CLOSE = 0x0F;
constexpr int TIMER_FOCUS = 0xFF;


NppData npp_data;
FuncItem func_item[NP_FUNC];
HINSTANCE h_module;

bool pycalc_enabled = false;
unique_ptr<jthread> worker_thread = nullptr;


BOOL APIENTRY DllMain(HINSTANCE hmodule, DWORD reason, LPVOID) {

    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            h_module = hmodule;

            func_item[CMD_ENABLED].func = enabled_toggle;
            func_item[CMD_SELECTED].func = calc_selected;

            lstrcpy(func_item[CMD_ENABLED].item_name, TEXT("enabled"));
            lstrcpy(func_item[CMD_SELECTED].item_name, TEXT("selected"));

            func_item[CMD_ENABLED].is_init_check = is_enabled();
            func_item[CMD_SELECTED].is_init_check = false;

            func_item[CMD_ENABLED].sh_key = nullptr;
            func_item[CMD_SELECTED].sh_key = nullptr;
        }
        case DLL_PROCESS_DETACH: {
            KillTimer(npp_data.npp_handle, TIMER_PRINT);
        }
        default: {
            break;
        }
    }
    return TRUE;
}

extern "C" __declspec(dllexport) const TCHAR* getName() {

    return PLUGIN_NAME;
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbF) {

    *nbF = NP_FUNC;
    return func_item;
}

extern "C" __declspec(dllexport) void setInfo(NppData data) {

    npp_data = data;

    worker_thread = make_unique<jthread>(Worker::process);
    worker_thread->detach();

    SetTimer(npp_data.npp_handle, TIMER_PRINT, 50, print_result);
}

extern "C" __declspec(dllexport) void beNotified(const SCNotification* scn) {

    switch (scn->nmhdr.code) {
        case SCN_CHARADDED: {
            on_key_press(scn);
        }
        default:
            break;
    }
}

BOOL CALLBACK ToastProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {

    switch (message) {
        case WM_INITDIALOG: {
            SetDlgItemText(hwnd, IDC_EDIT, reinterpret_cast<LPCTSTR>(l_param));
            SetTimer(hwnd, TIMER_CLOSE, 7000, nullptr);
            SetTimer(hwnd, TIMER_FOCUS, 50, nullptr);

            HICON h_error_icon = LoadIcon(nullptr, IDI_ERROR);
            HWND h_icon = GetDlgItem(hwnd, IDC_ERROR_ICON);
            SendMessage(h_icon, STM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(h_error_icon));

            RECT rect;
            SystemParametersInfo(SPI_GETWORKAREA, 0, &rect, 0);
            const auto screen_width = rect.right;
            const auto screen_height = rect.bottom;

            RECT dialog_rect;
            GetWindowRect(hwnd, &dialog_rect);
            const auto dialog_width = dialog_rect.right - dialog_rect.left;
            const auto dialog_height = dialog_rect.bottom - dialog_rect.top;

            const auto x = screen_width - dialog_width;
            const auto y = screen_height - dialog_height;

            SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            return FALSE;
        }
        case WM_TIMER: {
            if (w_param == TIMER_CLOSE) {
                KillTimer(hwnd, TIMER_CLOSE);
                EndDialog(hwnd, 0);
            } else if (w_param == TIMER_FOCUS) {
                SetFocus(get_handle());
                KillTimer(hwnd, TIMER_FOCUS);
            }

            break;
        }
        case WM_COMMAND: {
            const HWND h_edit = GetDlgItem(hwnd, IDC_EDIT);
            HideCaret(h_edit);
            break;
        }
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            break;
        default:
            break;
    }

    return FALSE;
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) {

    return TRUE;
}

#ifdef UNICODE
extern "C" __declspec(dllexport) BOOL isUnicode() {

    return TRUE;
}
#endif

HWND get_handle() {

    int handle;
    ::SendMessage(npp_data.npp_handle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&handle));
    return (handle == 0) ? npp_data.scintilla_main_handle : npp_data.scintilla_second_handle;
}

LRESULT send_message(const UINT msg, WPARAM w_param = 0, LPARAM l_param = 0) {

    return ::SendMessage(get_handle(), msg, w_param, l_param);
}

string get_eol() {

    const auto mode = send_message(SCI_GETEOLMODE);
    if (mode == SC_EOL_CRLF) {
        return "\r\n";
    }
    if (mode == SC_EOL_CR) {
        return "\r";
    }
    if (mode == SC_EOL_LF) {
        return "\n";
    }

    return "\n";
}

void calc_selected() {

    const auto length = send_message(SCI_GETSELTEXT);
    if (length < 1) {
        return;
    }

    string code;
    code.resize(length);
    send_message(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(code.c_str()));

    const auto sel_end = send_message(SCI_GETSELECTIONEND);
    send_message(SCI_SETEMPTYSELECTION, sel_end);

    if (!code.ends_with("\n") && !code.ends_with("\r")) {
        auto eol = get_eol();
        send_message(SCI_ADDTEXT, eol.length(), reinterpret_cast<LPARAM>(eol.c_str()));
    }

    execute_python_code(code, true);
}

bool is_press_enter(const int ch) {

    const auto mode = send_message(SCI_GETEOLMODE);

    return (ch == SCK_RETURN && (mode == SC_EOL_CRLF || mode == SC_EOL_CR)) ||
        (ch == SCK_LINEFEED && mode == SC_EOL_LF);
}

void on_key_press(const SCNotification* scn) {

    if (!pycalc_enabled || !is_press_enter(scn->ch)) {
        return;
    }

    const auto pos = send_message(SCI_GETCURRENTPOS);
    const auto line = send_message(SCI_LINEFROMPOSITION, pos) - 1;
    const auto length = send_message(SCI_LINELENGTH, line);

    if (length < 1) {
        return;
    }

    string code(length, '\0');
    send_message(SCI_GETLINE, line, reinterpret_cast<LPARAM>(code.c_str()));

    execute_python_code(code, false);
}

string to_utf8(const string& str) {

    int length = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, nullptr, 0);
    wstring wstr(length, L'\0');
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wstr[0], wstr.size());

    length = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string ustr(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &ustr[0], ustr.size(), nullptr, nullptr);

    return ustr;
}

string to_ansi(const string& str) {

    int length = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], wstr.size());

    length = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string astr(length, '\0');
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &astr[0], astr.size(), nullptr, nullptr);
    astr.resize(astr.size() - 1);

    return astr;
}

void CALLBACK print_result(HWND, UINT, UINT_PTR, DWORD) {

    unique_lock lock(worker->output_mutex);

    const auto now = chrono::steady_clock::now();
    if (worker->heartbeat && now - *worker->heartbeat > chrono::seconds(30)) {
        worker->heartbeat = steady_now();

        const auto result = MessageBox(
            npp_data.npp_handle,
            L"The Python code has been running for a long time. Do you want to terminate it?",
            L"Warning", MB_ICONWARNING | MB_YESNO
        );
        if (result == IDYES) {
            worker->heartbeat.reset();
            worker_thread->request_stop();

            auto gstate = PyGILState_Ensure();
            PyThreadState_SetAsyncExc(py_thread_state->thread_id, PyExc_KeyboardInterrupt);
            PyGILState_Release(gstate);

            lock_guard lock(worker->input_mutex);
            worker->stdout_queue = queue<string>();
            worker->stderr_queue = queue<string>();
            worker->stdin_queue = queue<string>();

            worker_thread = make_unique<jthread>(Worker::process);
            worker_thread->detach();
        }
    }

    while (!worker->stdout_queue.empty()) {
        auto value = worker->stdout_queue.front();
        worker->stdout_queue.pop();
        if (value.empty()) {
            continue;
        }

        if (send_message(SCI_GETCODEPAGE) != SC_CP_UTF8) {
            value = to_ansi(value);
        }

        send_message(SCI_ADDTEXT, value.length(), reinterpret_cast<LPARAM>(value.c_str()));
    }

    while (!worker->stderr_queue.empty()) {
        auto value = worker->stderr_queue.front();
        worker->stderr_queue.pop();
        if (value.empty()) {
            continue;
        }

        std::regex re("\n");
        value = std::regex_replace(value, re, "\r\n");

        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::wstring result = converter.from_bytes(value);

        const auto message = reinterpret_cast<LPARAM>(result.c_str());
        auto hdlg = CreateDialogParam(
            h_module, MAKEINTRESOURCE(IDD_TOAST), nullptr, reinterpret_cast<DLGPROC>(ToastProc), message
        );
        ShowWindow(hdlg, SW_SHOW);
        SetFocus(get_handle());
    }
}

void execute_python_code(const string& code, const bool multiline) {

    string pycode = code;

    if (send_message(SCI_GETCODEPAGE) != SC_CP_UTF8) {
        pycode = to_utf8(code);
    }

    if (multiline) {
        pycode = "1" + pycode;
    } else {
        pycode = "0" + pycode;
    }

    {
        lock_guard lock(worker->input_mutex);
        worker->stdin_queue.push(pycode);
    }
    worker->input_cv.notify_all();

    if (!worker->heartbeat) {
        worker->heartbeat = steady_now();
    }
}

bool is_enabled() {

    pycalc_enabled = true;

    HKEY hkey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, KEY_PYCALC, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
        return pycalc_enabled;
    }

    DWORD type = REG_DWORD;
    DWORD result = 0;
    DWORD size = sizeof(result);
    if (RegQueryValueEx(hkey, KEY_ENABLED, nullptr, &type, reinterpret_cast<LPBYTE>(&result), &size) != ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return pycalc_enabled;
    }
    RegCloseKey(hkey);

    pycalc_enabled = result == 1;
    return pycalc_enabled;
}

void set_enabled(const bool enabled) {

    HKEY hkey;
    DWORD disp;

    if (RegCreateKeyEx(HKEY_CURRENT_USER, KEY_PYCALC, 0, nullptr, REG_OPTION_VOLATILE, KEY_WRITE, nullptr, &hkey, &disp) != ERROR_SUCCESS) {
        return;
    }

    pycalc_enabled = enabled;
    DWORD value = enabled ? 1 : 0;
    constexpr DWORD size = sizeof(value);
    RegSetValueEx(hkey, KEY_ENABLED, 0, REG_DWORD, reinterpret_cast<LPBYTE>(&value), size);
    RegCloseKey(hkey);
}

void enabled_toggle() {

    const bool enabled = !is_enabled();
    set_enabled(enabled);

    const int value = enabled ? 1 : 0;
    SendMessage(npp_data.npp_handle, NPPM_SETMENUITEMCHECK, func_item[CMD_ENABLED].cmd_id, value);
}
