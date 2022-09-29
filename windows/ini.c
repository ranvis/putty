#include "putty.h"
#include <shlobj.h>

static bool use_inifile = false;
char inifile[2 * MAX_PATH + 10];

#define LOCAL_SCOPE

bool get_use_inifile(void)
{
    if (inifile[0] == '\0') {
        char buf[10];
        char *p;
        GetModuleFileName(NULL, inifile, sizeof inifile - 10);
        if((p = strrchr(inifile, '\\'))){
            *p = '\0';
        }
        strcat(inifile, "\\putty.ini");
        GetPrivateProfileString("Generic", "UseIniFile", "", buf, sizeof (buf), inifile);
        use_inifile = buf[0] == '1';
        if (!use_inifile) {
            HMODULE module;
            DECL_WINDOWS_FUNCTION(LOCAL_SCOPE, HRESULT, SHGetFolderPathA, (HWND, int, HANDLE, DWORD, LPSTR));
            module = load_system32_dll("shell32.dll");
            GET_WINDOWS_FUNCTION(module, SHGetFolderPathA);
            if (!p_SHGetFolderPathA) {
                FreeLibrary(module);
                module = load_system32_dll("shfolder.dll");
                GET_WINDOWS_FUNCTION(module, SHGetFolderPathA);
            }
            if (p_SHGetFolderPathA && SUCCEEDED(p_SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, inifile))) {
                strcat(inifile, "\\PuTTY\\putty.ini");
                GetPrivateProfileString("Generic", "UseIniFile", "", buf, sizeof (buf), inifile);
                use_inifile = buf[0] == '1';
            }
            FreeLibrary(module);
        }
    }
    return use_inifile;
}

static bool change_ini_path_core(const char *new_path)
{
    char *file_part;
    GetFullPathName(new_path, sizeof inifile, inifile, &file_part);
    DWORD attributes = GetFileAttributes(inifile);
    if (attributes == 0xFFFFFFFF || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }
    HANDLE handle = CreateFile(inifile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(handle);
    use_inifile = true;
    return true;
}

char *change_ini_path(const char *new_path)
{
    if (!new_path || !*new_path) {
        return l10n_dupstr("-ini must be followed by a filename");
    }
    if (!change_ini_path_core(new_path)) {
        return l10n_dupprintf("cannot use \"%s\" as ini file", new_path);
    }
    return NULL;
}
