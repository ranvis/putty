#include <string.h>
#include "putty.h"

// This patch may be duplicate under LEGACY_WINDOWS since 0.78.

#ifdef _WINDOWS
int xMultiByteToWideChar(UINT cp, DWORD flags, LPCSTR mbs, int mblen, LPWSTR wcs, int wcsize)
{
    static int (WINAPI *native_call)(UINT, DWORD, LPCSTR, int, LPWSTR, int) = 0;
    if (!native_call)
        (FARPROC)native_call = GetProcAddress(GetModuleHandle("kernel32"), "MultiByteToWideChar");
    if (cp != CP_UTF8)
        return native_call(cp, flags, mbs, mblen, wcs, wcsize);
    if (mblen == -1)
        mblen = lstrlenA(mbs) + 1; // include NUL if input length is -1
    int outlen = 0;
    for (int i = 0; i < mblen; i++) {
        unsigned long min_w, w;
        unsigned char c = mbs[i];
        int clen;
        if ((c & 0x80) == 0)
            clen = 1, min_w = 0x0000, w = c;
        else if ((c & 0xe0) == 0xc0)
            clen = 2, min_w = 0x0080, w = c & 0x1f;
        else if ((c & 0xf0) == 0xe0)
            clen = 3, min_w = 0x0800, w = c & 0x0f;
        else if ((c & 0xf8) == 0xf0)
            clen = 4, min_w = 0x10000, w = c & 0x07;
        else  /* invalid */
            continue;
        if (i + clen <= mblen) {
            while (--clen)
                w = (w << 6) | (mbs[++i] & 0x3f);
            if (w < min_w || (w >= 0xd800 && w <= 0xdfff))  /* ill-fromed */
                continue;
            if (w >= 0x10000) {
                if (outlen + 1 >= wcsize)
                    break;
                wcs[outlen++] = (WCHAR)HIGH_SURROGATE_OF(w);
                wcs[outlen] = (WCHAR)LOW_SURROGATE_OF(w);
            } else if (outlen < wcsize)
                wcs[outlen] = (WCHAR)w;
            else if (outlen == wcsize)
                break;
            outlen++;
        } else
            break;
    }
    return outlen;
}

int xWideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR wcs, int wclen, LPSTR mbs, int mbsize, LPCSTR fbchar, LPBOOL fbused)
{
    static int (WINAPI *native_call)(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL) = 0;
    if (!native_call)
        (FARPROC)native_call = GetProcAddress(GetModuleHandle("kernel32"), "WideCharToMultiByte");
    if (cp != CP_UTF8)
        return native_call(cp, flags, wcs, wclen, mbs, mbsize, fbchar, fbused);
    if (wclen == -1)
        wclen = lstrlenW(wcs) + 1; // include NUL if input length is -1
    int outlen = 0;
    for (int i = 0; i < wclen; i++) {
        WCHAR w = wcs[i];
        unsigned char b[10];
        int clen;
        if (w < 0x80)
            clen = 1, b[0] = (unsigned char)w;
        else if (w < 0x800)
            clen = 2, b[1] = 0x80 | (w & 0x3f), b[0] = 0xc0 | ((w >> 6) & 0x1f);
        else if (!IS_SURROGATE(w))
            clen = 3, b[2] = 0x80 | (w & 0x3f),
            b[1] = 0x80 | ((w >> 6) & 0x3f), b[0] = 0xe0 | ((w >> 12) & 0x0f);
        else if (IS_LOW_SURROGATE(w) && i + 1 < wclen) {
            unsigned long ww = FROM_SURROGATES(w, wcs[i + 1]);
            clen = 4, b[3] = 0x80 | (ww & 0x3f), b[2] = 0x80 | ((ww >> 6) & 0x3f),
                b[1] = 0x80 | ((ww >> 12) & 0x3f), b[0] = 0xf0 | ((ww >> 18) & 0x7);
        } else
            clen = 1, b[0] = '?';
        if (outlen + clen <= mbsize || !mbsize) {
            if (!mbsize)
                outlen += clen;
            else {
                mbs[outlen++] = b[0];
                if (clen >= 2)
                    mbs[outlen++] = b[1];
                if (clen == 3)
                    mbs[outlen++] = b[2];
            }
        } else
            break;
    }
    return outlen;
}
#endif
