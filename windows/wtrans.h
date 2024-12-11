#ifndef PUTTY_WINDOWS_WTRANS_H
#define PUTTY_WINDOWS_WTRANS_H

typedef struct WinGuiSeat WinGuiSeat;

void wtrans_init(HMODULE user32_module);
void wtrans_new(WinGuiSeat *wgs);
void wtrans_set(WinGuiSeat *wgs);
void wtrans_activate(WinGuiSeat *wgs, bool is_active);
void wtrans_destroy(WinGuiSeat *wgs);
void wtrans_begin_preview(WinGuiSeat *wgs);
void wtrans_preview(WinGuiSeat *wgs, int opacity);
void wtrans_end_preview(WinGuiSeat *wgs);

#endif
