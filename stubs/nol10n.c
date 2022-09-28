#include <stdio.h>
#include <stdarg.h>
#include "misc.h"

int get_l10n_setting(const char* keyname, char* buf, int size)
{
    return 0;
}

int l10n_sprintf(char* buffer, const char* format, ...)
{
    int r;
    va_list args;
    va_start(args, format);
    r = vsprintf(buffer, format, args);
    va_end(args);
    return r;
}

#ifndef HAS_VSNPRINTF
#undef _vsnprintf
#define vsnprintf _vsnprintf
#else
#undef vsnprintf
#endif
int l10n_vsnprintf(char* buffer, int size, const char* format, va_list args)
{
  return vsnprintf(buffer, size, format, args);
}

char *l10n_dupvprintf(const char *format, va_list ap)
{
    return dupvprintf(format, ap);
}

char *l10n_dupprintf(const char *format, ...)
{
    char *r;
    va_list args;
    va_start(args, format);
    r = dupvprintf(format, args);
    va_end(args);
    return r;
}

const char *l10n_translate_s(const char *str, char *buf, size_t len)
{
    return str;
}

char *l10n_dupstr (char *s) {
  return dupstr(s);
}
