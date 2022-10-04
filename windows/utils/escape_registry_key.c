/*
 * Escaping/unescaping functions to translate between a saved session
 * name, and the key name used to represent it in the Registry area
 * where we store saved sessions.
 *
 * The basic technique is to %-escape characters we can't use in
 * Registry keys.
 */

#include "putty.h"

static const char hex[16] = "0123456789ABCDEF";

void escape_registry_key(const char *in, strbuf *out)
{
    bool candot = false;

    while (*in) {
        if (*in == ' ' || *in == '\\' || *in == '*' || *in == '?' ||
            *in == '%' || *in < ' ' || *in > '~' || (*in == '.'
                                                     && !candot)) {
            put_byte(out, '%');
            put_byte(out, hex[((unsigned char) *in) >> 4]);
            put_byte(out, hex[((unsigned char) *in) & 15]);
        } else
            put_byte(out, *in);
        in++;
        candot = true;
    }
}

void escape_ini_section(const char *in, strbuf *out)
{
    bool candot = false;

    // while retaining compatibility with the previous versions which used escape_registry_key(), ']' is escaped for safety
    while (*in) {
        if (*in == ' ' || *in == '\\' || *in == '*' || *in == '?' ||
            *in == ']' ||
            *in == '%' || *in < ' ' || *in > '~' || (*in == '.'
                                                     && !candot)) {
            put_byte(out, '%');
            put_byte(out, hex[((unsigned char) *in) >> 4]);
            put_byte(out, hex[((unsigned char) *in) & 15]);
        } else
            put_byte(out, *in);
        in++;
        candot = true;
    }
}

void escape_ini_value_n(const char *in, size_t n, strbuf *out)
{
    while (n-- > 0) {
        if (*in == '\\' || *in == '%' || *in < ' ' || *in > '~') {
            put_byte(out, '%');
            put_byte(out, hex[((unsigned char) *in) >> 4]);
            put_byte(out, hex[((unsigned char) *in) & 15]);
        } else
            put_byte(out, *in);
        in++;
    }
}

void unescape_registry_key(const char *in, strbuf *out)
{
    while (*in) {
        if (*in == '%' && in[1] && in[2]) {
            int i, j;

            i = in[1] - '0';
            i -= (i > 9 ? 7 : 0);
            j = in[2] - '0';
            j -= (j > 9 ? 7 : 0);

            put_byte(out, (i << 4) + j);
            in += 3;
        } else {
            put_byte(out, *in++);
        }
    }
}
