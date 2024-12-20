#ifndef LIBRARY_H
#define LIBRARY_H

#include <string>

HWND get_handle();

void on_key_press(const SCNotification *);
void calc_selected();

void CALLBACK print_result(HWND, UINT, UINT_PTR, DWORD);
void execute_python_code(const std::string& code, const bool multiline);

bool is_enabled();
void set_enabled(bool enabled);
void enabled_toggle();

#endif // LIBRARY_H
