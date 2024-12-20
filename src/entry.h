#ifndef ENTRY_H
#define ENTRY_H

#include "scintilla.h"
#include "npp_msgs.h"


typedef const TCHAR * (__cdecl * PFUNCGETNAME)();

struct NppData {
    HWND npp_handle = nullptr;
    HWND scintilla_main_handle = nullptr;
    HWND scintilla_second_handle = nullptr;
};

typedef void (__cdecl * PFUNCSETINFO)(NppData);
typedef void (__cdecl * PFUNCPLUGINCMD)();
typedef void (__cdecl * PBENOTIFIED)(SCNotification *);
typedef LRESULT (__cdecl * PMESSAGEPROC)(UINT Message, WPARAM wParam, LPARAM lParam);


struct ShortcutKey {
    bool is_ctrl = false;
    bool is_alt = false;
    bool is_shift = false;
    UCHAR key = 0;
};

const int menuItemSize = 64;

struct FuncItem {
    TCHAR item_name[menuItemSize] = { '\0' };
    PFUNCPLUGINCMD func = nullptr;
    int cmd_id = 0;
    bool is_init_check = false;
    ShortcutKey *sh_key = nullptr;
};

typedef FuncItem * (__cdecl * PFUNCGETFUNCSARRAY)(int *);

extern "C" __declspec(dllexport) void setInfo(NppData);
extern "C" __declspec(dllexport) const TCHAR * getName();
extern "C" __declspec(dllexport) FuncItem * getFuncsArray(int *);
extern "C" __declspec(dllexport) void beNotified(const SCNotification *);
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM);

extern "C" __declspec(dllexport) BOOL isUnicode();


#endif // ENTRY_H
