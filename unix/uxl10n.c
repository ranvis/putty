#include "putty.h"

const char *l10n_translate_s(const char *str, char *buf, size_t len)
{
    return str;
}

char *
l10n_dupstr (const char *s)
{
  return dupstr (s);
}

char *l10n_dupvprintf(const char *format, va_list ap)
{
    return dupvprintf(format, ap);
}
