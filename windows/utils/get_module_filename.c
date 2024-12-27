#include "putty.h"

#undef GetModuleFileNameA
#undef GetModuleFileNameW

DWORD safeGetModuleFileNameA(HMODULE module, LPSTR filename, DWORD size)
{
    DWORD written = GetModuleFileNameA(module, filename, size);
    if (size)
        filename[size - 1] = '\0'; // Windows XP fix
    if (!written && size)
        *filename = '\0';
    return written;
}
