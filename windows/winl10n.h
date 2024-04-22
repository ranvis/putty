#ifndef PUTTY_WINL10N_H
#define PUTTY_WINL10N_H

int strtranslate(const WCHAR *str, WCHAR *out_buf, int out_size);
const WCHAR *strtranslatefb(const WCHAR *str, WCHAR *out_buf, int out_size);

BOOL l10nSetDlgItemText(HWND dialog, int id, LPCSTR text);
LRESULT l10nSendDlgItemMessage(HWND dialog, int id, UINT msg, WPARAM wp, LPARAM lp);
BOOL l10nAppendMenu(HMENU menu, UINT flags, UINT_PTR id, LPCSTR text);
BOOL SetDlgItemTextCp1252(HWND dialog, int item, LPCSTR text);

#endif
