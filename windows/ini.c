#include "putty.h"
#include <shlobj.h>
#include "ini.h"

#undef sprintf /* no auto translate */

struct ini_sections {
    const char *ini_path;
    char *buffer;
    const char *p;
};

static bool use_inifile = false;
char inifile[2 * MAX_PATH + 10];

#define LOCAL_SCOPE

bool get_use_inifile(void)
{
    if (inifile[0] == '\0') {
        char buf[10];
        GetModuleFileName(NULL, inifile, sizeof inifile - 10);
        char *p = strrchr(inifile, '\\');
        if (p) {
            strcpy(p, "\\putty.ini");
            GetPrivateProfileString("Generic", "UseIniFile", "", buf, lenof(buf), inifile);
            use_inifile = buf[0] == '1';
        }
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
                GetPrivateProfileString("Generic", "UseIniFile", "", buf, lenof(buf), inifile);
                use_inifile = buf[0] == '1';
            }
            FreeLibrary(module);
        }
        if (!use_inifile) {
            inifile[0] = '-';
            inifile[1] = '\0';
        }
    }
    return use_inifile;
}

static bool change_ini_path_core(const char *new_path)
{
    GetFullPathName(new_path, sizeof inifile, inifile, NULL);
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

static char *change_ini_path(const char *new_path)
{
    if (!new_path || !*new_path) {
        return l10n_dupstr("-ini must be followed by a filename");
    }
    if (!change_ini_path_core(new_path)) {
        return l10n_dupprintf("cannot use \"%s\" as ini file", new_path);
    }
    return NULL;
}

void process_ini_option(sprintf_void_fp error_cb)
{
    const wchar_t *cmdline = GetCommandLineW();
    const wchar_t *ini_arg = wcsstr(cmdline, L"-ini");
    if (!ini_arg || (cmdline != ini_arg
        && (ini_arg[-1] != '-' && ini_arg[-1] != ' ' && ini_arg[-1] != '\t' && ini_arg[-1] != '"'))) return;
    int argc;
    wchar_t **argv;
    split_into_argv_w(cmdline, true, &argc, &argv, NULL);
    if (argc >= 2) {
        if (!wcscmp(argv[1], L"-ini")) {
            char *new_path = argc > 2 ? dup_wc_to_mb(CP_ACP, argv[2], "") : NULL;
            char *error_msg = change_ini_path(new_path);
            sfree(new_path);
            if (error_msg) {
                error_cb(error_msg);
                exit(1);
            }
        } else if (!wcscmp(argv[1], L"-no-ini")) {
            inifile[0] = '-';
            inifile[1] = '\0';
        }
    }
    sfree(argv[0]);
    sfree(argv);
}

size_t skip_ini_option(size_t index, size_t argc, wchar_t **argv)
{
    if (argc > index) {
        if (!wcscmp(argv[index], L"-ini")) {
            index++;
            if (argc > index) index++;
        } else if (!wcscmp(argv[index], L"-no-ini")) {
            index++;
        }
    }
    return index;
}

static void put_ini_section(const char *in, strbuf *out, const char *prefix)
{
    put_dataz(out, prefix);
    escape_ini_section(in, out);
}

char *create_ini_section(const char *name, const char *prefix, const char *ini_file)
{
    strbuf *sb = strbuf_new();
    put_ini_section(name, sb, prefix);
    WritePrivateProfileString(sb->s, "Present", "1", ini_file);
    return strbuf_to_str(sb);
}

char *open_ini_section(const char *name, const char *prefix, const char *ini_file)
{
    strbuf *sb = strbuf_new();
    char temp[3];
    put_ini_section(name, sb, prefix);
    GetPrivateProfileString(sb->s, "Present", "", temp, sizeof temp, ini_file);
    if (temp[0] != '1') {
        strbuf_free(sb);
        return NULL;
    }
    return strbuf_to_str(sb);
}

void delete_ini_section(const char *name, const char *prefix, const char *ini_file)
{
    strbuf *sb = strbuf_new();
    put_ini_section(name, sb, prefix);
    WritePrivateProfileSection(sb->s, NULL, ini_file);
    strbuf_free(sb);
}

ini_sections *enum_ini_sections(const char *ini_path)
{
    ini_sections *sects;
    char *buffer;
    size_t length = 256;
    while (1) {
        buffer = snewn(length, char);
        if (GetPrivateProfileSectionNames(buffer, length, ini_path) < (DWORD) length - 2)
            break;
        sfree(buffer);
        length += 256;
    }
    sects = snew(ini_sections);
    sects->ini_path = ini_path;
    sects->buffer = buffer;
    sects->p = buffer;
    return sects;
}

bool enum_ini_section_next(ini_sections *sects, strbuf *sb, const char *prefix)
{
    size_t prefix_len = strlen(prefix);
    while (*sects->p != '\0') {
        if (!strncmp((char*)sects->p, prefix, prefix_len)) {
            char temp[3];
            GetPrivateProfileString(sects->p, "Present", "", temp, sizeof(temp), sects->ini_path);
            if (temp[0] == '1') {
                unescape_registry_key(sects->p + prefix_len, sb);
                while (*sects->p++ != '\0')
                    ;
                return true;
            }
        }
        while (*sects->p++ != '\0')
            ;
    }
    return false;
}

void enum_ini_section_finish(ini_sections *e)
{
    sfree(e->buffer);
    sfree(e);
}

static size_t strspn_table(char *str, const char *tab)
{
    char *p = str;
    while (*p) {
        if (!tab[*(unsigned char *)p]) {
            break;
        }
        p++;
    }
    return (size_t)(p - str);
}

/*
# the following table is generated with this Perl script:
use 5.030;
use warnings;
my $bareChars = ',-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz';
my @tab = (0) x 256;
$tab[ord($_)] = 1 foreach (split(//, $bareChars));
say '    ', join(', ', splice(@tab, 0, 16)), ',' while (@tab);
*/
static const char ini_nq_table[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

bool put_ini_sz(const char *section, const char *name, const char *value, const char *ini_file)
{
    BOOL result;
    if (value == NULL || value[strspn_table(value, ini_nq_table)] == '\0') {
        result = WritePrivateProfileString(section, name, value, ini_file);
    } else {
        // quote so that Windows will not strip surrounding quotes or spaces on retrieval
        // outmost quotes will be stripped on retrieval
        int length = strlen(value);
        char *buf = snewn(length + 3, char);
        memcpy(buf + 1, value, length);
        buf[0] = buf[length + 1] = '"';
        buf[length + 2] = '\0';
        result = WritePrivateProfileString(section, name, buf, ini_file);
        sfree(buf);
    }
    return result != 0;
}

char *get_ini_sz(const char *section, const char *name, const char *ini_file)
{
    char *value;
    for (DWORD size = 1024;; size *= 2) {
        value = snewn(size, char);
        if (GetPrivateProfileString(section, name, "\n:", value, size, ini_file) != size - 1)
            break; // not resized, but most allocations will not last long
        sfree(value);
    }
    if (value[0] != '\n') // distinguish missing value from empty value
        return value;
    sfree(value);
    return NULL;
}

bool put_ini_int(const char *section, const char *name, int value, const char *ini_file)
{
    char buf[32];
    sprintf(buf, "%d", value);
    BOOL result = WritePrivateProfileString(section, name, buf, ini_file);
    return result != 0;
}

bool get_ini_int(const char *section, const char *name, const char *ini_file, int *value)
{
    char buf[32];
    GetPrivateProfileString(section, name, "", buf, lenof(buf), ini_file);
    if ('0' <= buf[0] && buf[0] <= '9') {
        *value = atoi(buf);
        return true;
    }
    return false;
}

strbuf *get_ini_multi_sz(const char *section, const char *name, const char *ini_file)

{
    char *value = get_ini_sz(section, name, ini_file);
    if (!value)
        return NULL;
    strbuf *values = strbuf_new();
    unescape_registry_key(value, values);
    sfree(value);
    while (strbuf_chomp(values, '\0')) ;
    put_byte(values, '\0');
    return values;
}

bool put_ini_multi_sz(const char *section, const char *name, strbuf *value, const char *ini_file)
{
    strbuf *flat = strbuf_new();
    escape_ini_value_n(value->s, value->len + 1, flat);
    bool result = put_ini_sz(section, name, flat->s, ini_file);
    strbuf_free(flat);
    return result;
}
