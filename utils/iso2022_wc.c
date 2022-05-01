#include <string.h>
#include "putty.h"

#ifdef _WINDOWS
int
xMultiByteToWideChar (UINT a1, DWORD a2, LPCSTR a3, int a4, LPWSTR a5, int a6)
{
  static int (WINAPI *f)(UINT, DWORD, LPCSTR, int, LPWSTR, int) = 0;
  int i, j, k;
  unsigned char c;
  unsigned long w;

  if (!f)
    (FARPROC)f = GetProcAddress (GetModuleHandle ("kernel32"), "MultiByteToWideChar");
  if (a1 != CP_UTF8)
    return f (a1, a2, a3, a4, a5, a6);
  if (a4 == -1)
    a4 = lstrlenA (a3);
  k = 0;
  a6--;
  for (i = 0 ; i < a4 ; i++)
    {
      c = a3[i];
      if ((c & 0x80) == 0)
        j = 1, w = c;
      else if ((c & 0xe0) == 0xc0)
        j = 2, w = c & 0x1f;
      else if ((c & 0xf0) == 0xe0)
        j = 3, w = c & 0x0f;
      else if ((c & 0xf8) == 0xf0)
        j = 4, w = c & 0x07;
      else if ((c & 0xfc) == 0xf8)
        j = 5, w = c & 0x03;
      else if ((c & 0xfe) == 0xfc)
        j = 6, w = c & 0x01;
      else
        continue;
      if (i + j <= a4)
        {
          while (--j)
            w = (w << 6) | (a3[++i] & 0x3f);
          if (w >= 0x10000) {
            if (k + 1 >= a6)
              break;
            a5[k++] = (WCHAR)HIGH_SURROGATE_OF(w);
            a5[k] = (WCHAR)LOW_SURROGATE_OF(w);
          } else if (k < a6)
            a5[k] = (WCHAR)w;
          else if (k == a6)
            break;
          k++;
        }
      else
        break;
    }
  if (k <= a6)
    a5[k] = 0;
  return k + 1;
}

int
xWideCharToMultiByte (UINT a1, DWORD a2, LPCWSTR a3, int a4, LPSTR a5, int a6,
                     LPCSTR a7, LPBOOL a8)
{
  static int (WINAPI *f)(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR,
                         LPBOOL) = 0;
  int i, j, k;
  WCHAR w;
  unsigned char b[10];

  if (!f)
    (FARPROC)f = GetProcAddress (GetModuleHandle ("kernel32"), "WideCharToMultiByte");
  if (a1 != CP_UTF8)
    return f (a1, a2, a3, a4, a5, a6, a7, a8);
  if (a4 == -1)
    a4 = lstrlenW (a3);
  k = 0;
  a6--;
  for (i = 0 ; i < a4 ; i++)
    {
      w = a3[i];
      if (w < 0x80)
        j = 1, b[0] = (unsigned char)w;
      else if (w < 0x800)
        j = 2, b[1] = 0x80 | (w & 0x3f), b[0] = 0xc0 | ((w >> 6) & 0x1f);
      else if (!IS_SURROGATE(w))
        j = 3, b[2] = 0x80 | (w & 0x3f),
          b[1] = 0x80 | ((w >> 6) & 0x3f), b[0] = 0xe0 | ((w >> 12) & 0x0f);
      else if (IS_LOW_SURROGATE(w) && i + 1 < a4) {
        unsigned long ww = FROM_SURROGATES(w, a3[i + 1]);
        j = 4, b[3] = 0x80 | (ww & 0x3f), b[2] = 0x80 | ((ww >> 6) & 0x3f),
          b[1] = 0x80 | ((ww >> 12) & 0x3f), b[0] = 0xf0 | ((ww >> 18) & 0x7);
      } else
        j = 1, b[0] = '?';
      if (k + j <= a6 || a6 == -1)
        {
          if (a6 == -1)
            k += j;
          else
            {
              a5[k++] = b[0];
              if (j >= 2)
                a5[k++] = b[1];
              if (j == 3)
                a5[k++] = b[2];
            }
        }
      else
        break;
    }
  if (k <= a6)
    a5[k] = '\0';
  return k + 1;
}
#endif
