#ifndef PUTTY_WINL10N_H
#define PUTTY_WINL10N_H

int strtranslate(const WCHAR *str, WCHAR *out_buf, int out_size);
const WCHAR *strtranslatefb(const WCHAR *str, WCHAR *out_buf, int out_size);

#endif
