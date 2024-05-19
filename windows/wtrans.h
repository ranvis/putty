#ifndef PUTTY_WINDOWS_WTRANS_H
#define PUTTY_WINDOWS_WTRANS_H

void wtrans_init(HMODULE user32_module);
void wtrans_set(Conf *conf, HWND hwnd);
void wtrans_activate(Conf *conf, HWND hwnd, bool is_active);
void wtrans_destroy(HWND hwnd);
void wtrans_begin_preview(HWND hwnd);
void wtrans_preview(HWND hwnd, int opacity);
void wtrans_end_preview(Conf *conf, HWND hwnd);

#endif
