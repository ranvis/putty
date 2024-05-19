#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <string.h>
#include <stdio.h>
#include "putty.h"

#define IN_STR_MAX 448
#define OUT_STR_MAX_A 512
#define OUT_STR_MAX_W 448
#define SHORT_STR_MAX 192

static const WCHAR *lng_path = L"";
static WCHAR lng_section[32];
static WCHAR ofontname[64];
static HFONT hfont;
static WNDPROC orig_wndproc_button, orig_wndproc_static,
               orig_wndproc_systreeview32, orig_wndproc_edit, orig_wndproc_listbox, orig_wndproc_combobox;
static int collect_len = 0;
static INT_PTR CALLBACK defdlgproc(HWND a1, UINT a2, WPARAM a3, LPARAM a4)
{
    return 0;
}
static struct prop {
    bool is_unicode;
    WNDPROC oldproc;
    DLGPROC olddlgproc;
} defaultprop = { TRUE, DefWindowProcW, defdlgproc };
static const char propstr[] = "l10n";
static DLGPROC lastolddlgproc;
static bool getEnabled();

#define WC_HOOK(orig) ("x" WC_##orig)
#define WC_HOOKW(orig) (L"x" WC_##orig##W)

static WCHAR *low_url_escape(const WCHAR *p)
{
    static WCHAR tmp_str[IN_STR_MAX];
    int i;
    for (i = 0; i < lenof(tmp_str) - 1 - 2 - 2 && *p; i++) {
        if (i == 0 && *p == ' ' || (*p > 0 && *p < 32) || *p == '=' || *p == '%') {
            tmp_str[i++] = '%';
            tmp_str[i++] = "0123456789ABCDEF"[(*p >> 4) & 15];
            tmp_str[i] = (*p++ & 15)["0123456789ABCDEF"];
        } else
            tmp_str[i] = *p++;
    }
    if (i > 0 && tmp_str[i - 1] == ' ') {
        tmp_str[i - 1] = '%';
        tmp_str[i++] = '2';
        tmp_str[i++] = '0';
    }
    tmp_str[i] = '\0';
    return tmp_str;
}

static int uchex_to_int(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static int mbs_to_ws(const char *str, WCHAR *wstr, int wstr_size)
{
    int result = MultiByteToWideChar(CP_ACP, 0, str, -1, wstr, wstr_size);
    if (!result) {
        wstr[wstr_size - 1] = '\0';
    }
    return result;
}

static int ws_to_mbs(const WCHAR *wstr, int in_size, char *str, int str_size)
{
    int result = WideCharToMultiByte(CP_ACP, 0, wstr, in_size, str, str_size, NULL, NULL);
    if (!result) {
        str[str_size - 1] = '\0';
    }
    return result;
}

static bool load_ini_wsz(const WCHAR *section, const WCHAR *name, WCHAR *dest, DWORD size, const WCHAR *ini_file)
{
    DWORD len = GetPrivateProfileStringW(section, name, L"\n", dest, size, ini_file);
    return len != size - 1 && *dest != L'\n';
}

int strtranslate(const WCHAR *str, WCHAR *out_buf, int out_size)
{
    if (!getEnabled())
        return 0;
    {
        const WCHAR *p = str;
        while (*p != '\0') {
            if (*p > 0x7e)
                return 0;
            p++;
        }
    }
    const WCHAR *str_esc = low_url_escape(str);
    if (!load_ini_wsz(lng_section, str_esc, out_buf, out_size, lng_path)) {
        // N.B. str may be out_buf; you cannot do wcslen(str)
        if (collect_len > 0 && (int)wcslen(str_esc) > collect_len)
            WritePrivateProfileStringW(lng_section, str_esc, str_esc, lng_path);
        return 0;
    }
    WCHAR *out;
    int len = 0;
    for (out = out_buf; (*out = *out_buf); out++, out_buf++, len++) {
        if (*out_buf == '%') {
            int hi, lo;
            if ((hi = uchex_to_int(out_buf[1])) < 0)
                continue;
            if ((lo = uchex_to_int(out_buf[2])) < 0)
                continue;
            *out = hi * 16 + lo;
            out_buf += 2;
        }
    }
    return len;
}

WCHAR *strtranslatefb(const WCHAR *str, WCHAR *out_buf, int out_size)
{
    int result = strtranslate(str, out_buf, out_size);
    return result ? out_buf : str;
}

static int cwstrtranslate(const char *str, WCHAR *out_buf, int out_size)
{
    WCHAR in_str[IN_STR_MAX];
    int i;
    for (i = 0; i < lenof(in_str) - 1 && str[i]; i++) {
        if (IsDBCSLeadByte(str[i]))
            return 0;
        in_str[i] = str[i];
    }
    in_str[i] = 0;
    return strtranslate(in_str, out_buf, out_size);
}

static int cwstrtranslatefb(const char *str, WCHAR *out_buf, int out_size)
{
    int result = cwstrtranslate(str, out_buf, out_size);
    if (!result) {
        result = mbs_to_ws(str, out_buf, out_size);
        if (!result) {
            wcsncpy(out_buf, L"?", out_size);
            out_buf[out_size - 1] = 0;
            result = (int)wcslen(out_buf);
        }
    }
    return result;
}

static int ccstrtranslate(const char *str, char *out_buf, int out_size)
{
    WCHAR w_buf[OUT_STR_MAX_W];
    int r = cwstrtranslate(str, w_buf, lenof(w_buf));
    if (r) {
        r = ws_to_mbs(w_buf, r + 1, out_buf, out_size);
        if (r) {
            r--;
        }
    }
    return r;
}

static void domenu(HMENU menu)
{
    int i, n;
    WCHAR a[SHORT_STR_MAX];
    MENUITEMINFOW b;

    n = GetMenuItemCount(menu);
    for (i = 0; i < n; i++) {
        b.cbSize = sizeof(MENUITEMINFOW);
        b.fMask = MIIM_TYPE;
        b.dwTypeData = a;
        b.cch = lenof(a);
        if (GetMenuItemInfoW(menu, i, 1, &b))
            if (strtranslate(a, a, lenof(a)))
                SetMenuItemInfoW(menu, i, 1, &b);
    }
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WNDPROC proc;
    struct prop *p;

    p = GetProp(hwnd, propstr);
    if (!p)
        p = &defaultprop;
    proc = p->oldproc;
    switch (msg) {
      case WM_DESTROY:
        p = RemoveProp(hwnd, propstr);
        if (p && p->is_unicode)
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)proc);
        else
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)proc);
        LocalFree((HANDLE)p);
        break;
      case WM_INITMENUPOPUP:
        domenu((HMENU)wparam);
        break;
    }
    return p->is_unicode
        ? CallWindowProcW(proc, hwnd, msg, wparam, lparam)
        : CallWindowProcA(proc, hwnd, msg, wparam, lparam);
}

static LRESULT hook_setfont(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    LOGFONTW a;

    if (wparam && GetObjectW((HGDIOBJ)wparam, sizeof(a), &a)) {
        if (!_wcsicmp(a.lfFaceName, ofontname))
            return CallWindowProc(proc, hwnd, msg, (WPARAM)hfont, lparam);
    }
    return CallWindowProc(proc, hwnd, msg, wparam, lparam);
}

static LRESULT hook_create(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WCHAR text[SHORT_STR_MAX], trtext[SHORT_STR_MAX];
    LRESULT result;

    result = CallWindowProc(proc, hwnd, msg, (WPARAM)hfont, lparam);
    if (GetWindowTextW(hwnd, text, lenof(text)))
        if (strtranslate(text, trtext, lenof(trtext)))
            SetWindowTextW(hwnd, trtext);
    return result;
}

static LRESULT hook_tvm_insertitem(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TVINSERTSTRUCTW *p;
    WCHAR *text;
    WCHAR trtext[SHORT_STR_MAX];
    LRESULT result;

    p = (TVINSERTSTRUCTW*)lparam;
    text = p->item.pszText;
    if (strtranslate(text, trtext, lenof(trtext)))
        p->item.pszText = trtext;
    result = CallWindowProc(proc, hwnd, msg, wparam, lparam);
    p->item.pszText = text;
    return result;
}

static LRESULT CALLBACK wndproc_button(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WNDPROC proc;

    proc = orig_wndproc_button;
    switch (msg) {
      case WM_SETFONT: return hook_setfont(proc, hwnd, msg, wparam, lparam);
      case WM_CREATE: return hook_create(proc, hwnd, msg, wparam, lparam);
    }
    return CallWindowProc(proc, hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK wndproc_static(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WNDPROC proc;

    proc = orig_wndproc_static;
    switch (msg) {
      case WM_SETFONT: return hook_setfont(proc, hwnd, msg, wparam, lparam);
      case WM_CREATE: return hook_create(proc, hwnd, msg, wparam, lparam);
    }
    return CallWindowProc(proc, hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK wndproc_systreeview32(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WNDPROC proc;

    proc = orig_wndproc_systreeview32;
    switch (msg) {
      case WM_SETFONT: return hook_setfont(proc, hwnd, msg, wparam, lparam);
      case TVM_INSERTITEM: return hook_tvm_insertitem(proc, hwnd, msg, wparam, lparam);
      /* case TVM_GETITEM: counter-translate text, if caller needs one */
    }
    return CallWindowProc(proc, hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK wndproc_edit(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WNDPROC proc;

    proc = orig_wndproc_edit;
    switch (msg) {
      case WM_SETFONT: return hook_setfont(proc, hwnd, msg, wparam, lparam);
    }
    return CallWindowProc(proc, hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK wndproc_listbox(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WNDPROC proc;

    proc = orig_wndproc_listbox;
    switch (msg) {
      case WM_SETFONT: return hook_setfont(proc, hwnd, msg, wparam, lparam);
      case LB_ADDSTRING: {
        WCHAR buffer[SHORT_STR_MAX];
        WCHAR *text = (WCHAR *)lparam;
        if (strtranslate(text, buffer, lenof(buffer)))
            text = buffer;
        return CallWindowProc(proc, hwnd, msg, wparam, (LPARAM)text);
    }
    }
    return CallWindowProc(proc, hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK wndproc_combobox(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WNDPROC proc;

    proc = orig_wndproc_combobox;
    switch (msg) {
      case WM_SETFONT: return hook_setfont(proc, hwnd, msg, wparam, lparam);
      case CB_ADDSTRING: {
        WCHAR buffer[SHORT_STR_MAX];
        WCHAR *text = (WCHAR *)lparam;
        if (strtranslate(text, buffer, lenof(buffer)))
            text = buffer;
        return CallWindowProc(proc, hwnd, msg, wparam, (LPARAM)text);
    }
    }
    return CallWindowProc(proc, hwnd, msg, wparam, lparam);
}

DECL_WINDOWS_FUNCTION(static, BOOL, GetUserPreferredUILanguages, (DWORD dwFlags, PULONG pulNumLanguages, PZZWSTR pwszLanguagesBuffer, PULONG pcchLanguagesBuffer));

static char *try_lng_path(const char *fmt, ...)
{
    char *path;
    va_list ap;
    va_start(ap, fmt);
    path = dupvprintf(fmt, ap);
    va_end(ap);
    if (GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES)
        return path;
    sfree(path);
    return NULL;
}

static char *get_lng_file_path()
{
    char exe_path[MAX_PATH];
    char *dot_pos, *exe_name, *path;
    LANGID lang_id;
    if (!GetModuleFileName(NULL, exe_path, MAX_PATH))
        return NULL;
    dot_pos = strrchr(exe_path, '.');
    exe_name = strrchr(exe_path, '\\');
    if (!dot_pos || !exe_name)
        return NULL;
    *dot_pos = 0;
    *exe_name++ = 0;
    if ((path = try_lng_path("%s\\%s.lng", exe_path, exe_name)) != NULL)
        return path;
    if (!p_GetUserPreferredUILanguages) {
        GET_WINDOWS_FUNCTION(GetModuleHandle("kernel32.dll"), GetUserPreferredUILanguages);
    }
    if (p_GetUserPreferredUILanguages) {
        ULONG num_langs, lang_size = 0;
        WCHAR *langs, *lang_w;
        if (p_GetUserPreferredUILanguages(MUI_LANGUAGE_ID, &num_langs, NULL, &lang_size)) {
            langs = lang_w = snewn(lang_size, WCHAR);
            if (p_GetUserPreferredUILanguages(MUI_LANGUAGE_ID, &num_langs, langs, &lang_size)) {
                while (num_langs--) {
                    char lang[8];
                    int lang_len = ws_to_mbs(lang_w, -1, lang, sizeof lang);
                    if ((path = try_lng_path("%s\\lang\\%s\\%s.lng", exe_path, lang, exe_name)) != NULL) {
                        sfree(langs);
                        return path;
                    }
                    lang_w += wcslen(lang_w) + 1;
                }
            }
            sfree(langs);
        }
    } else {
        lang_id = GetUserDefaultUILanguage();
        if ((path = try_lng_path("%s\\lang\\%04x\\%s.lng", exe_path, lang_id, exe_name)) != NULL)
            return path;
    }
    return NULL;
}

static WCHAR *strdup_to_wc(const char *str, int cp)
{
    int w_len = MultiByteToWideChar(cp, 0, str, -1, NULL, 0);
    WCHAR *str_w = snewn(w_len, WCHAR);
    MultiByteToWideChar(cp, 0, str, -1, str_w, w_len);
    return str_w;
}

static WCHAR *get_lng_file_path_w()
{
    char *lng_path_a = get_lng_file_path();
    if (!lng_path_a)
        return NULL;
    WCHAR *lng_path_w = strdup_to_wc(lng_path_a, CP_ACP);
    sfree(lng_path_a);
    return lng_path_w;
}

static WCHAR *resolve_cname_path(const WCHAR *cname, const WCHAR *base)
{
    WCHAR *base_sep = wcsrchr(base, L'\\');
    if (!base_sep || wcschr(cname, L'\\') || wcschr(cname, L'/')) {
        return NULL;
    }
    base_sep++;
    size_t base_dir_len = base_sep - base;
    size_t cname_len = wcslen(cname);
    WCHAR *path = snewn(base_dir_len + cname_len + 1, WCHAR);
    memcpy(path, base, base_dir_len * sizeof(WCHAR));
    memcpy(path + base_dir_len, cname, (cname_len + 1) * sizeof(WCHAR));
    return path;
}

static bool getEnabled()
{
    static char enabled = -1;
    if (enabled == -1) {
        int i;
        int fontsize;
        struct {
            WCHAR *classname;
            WCHAR *newclassname;
            WNDPROC *orig_wndproc;
            WNDPROC new_wndproc;
        } b[] = {
            { WC_BUTTONW, WC_HOOKW(BUTTON), &orig_wndproc_button, wndproc_button },
            { WC_STATICW, WC_HOOKW(STATIC), &orig_wndproc_static, wndproc_static },
            { WC_TREEVIEWW, WC_HOOKW(TREEVIEW), &orig_wndproc_systreeview32, wndproc_systreeview32 },
            { WC_EDITW, WC_HOOKW(EDIT), &orig_wndproc_edit, wndproc_edit },
            { WC_LISTBOXW, WC_HOOKW(LISTBOX), &orig_wndproc_listbox, wndproc_listbox },
            { WC_COMBOBOXW, WC_HOOKW(COMBOBOX), &orig_wndproc_combobox, wndproc_combobox },
            { NULL }
        };

        enabled = false;
        lng_path = get_lng_file_path_w();
        if (lng_path) {
            if (!load_ini_wsz(L"Default", L"Language", lng_section, lenof(lng_section), lng_path) || *lng_section == L'\0') {
                WCHAR cname[32];
                WCHAR *cname_path;
                if (!load_ini_wsz(L"Default", L"CanonicalName", cname, lenof(cname), lng_path)
                        || *lng_section == L'\0'
                        || !(cname_path = resolve_cname_path(cname, lng_path))) {
                    sfree(lng_path);
                    lng_path = NULL;
                } else {
                    sfree(lng_path);
                    lng_path = cname_path;
                    if (!load_ini_wsz(L"Default", L"Language", lng_section, lenof(lng_section), lng_path) || *lng_section == L'\0') {
                        sfree(lng_path);
                        lng_path = NULL;
                    }
                }
            }
            if (lng_path) {
                HINSTANCE hinst = GetModuleHandle(NULL);
                WCHAR fontname[128];
                enabled = true;
                GetPrivateProfileStringW(lng_section, L"_FONTNAME_", L"System", fontname, lenof(fontname),
                    lng_path);
                GetPrivateProfileStringW(lng_section, L"_OFONTNAME_", L"MS Sans Serif",
                    ofontname, lenof(ofontname), lng_path);
                fontsize = GetPrivateProfileIntW(lng_section, L"_FONTSIZE_", 10, lng_path);
                hfont = CreateFontW(fontsize, 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET,
                    0, 0, 0, 0, fontname);
                for (i = 0; b[i].classname != NULL; i++) {
                    WNDCLASSW wndclass;

                    GetClassInfoW(hinst, b[i].classname, &wndclass);
                    wndclass.hInstance = hinst;
                    wndclass.lpszClassName = b[i].newclassname;
                    *b[i].orig_wndproc = wndclass.lpfnWndProc;
                    wndclass.lpfnWndProc = b[i].new_wndproc;
                    RegisterClassW(&wndclass);
                }
                collect_len = GetPrivateProfileIntW(lng_section, L"_COLLECT_", 0, lng_path);
            }
        }
        if (!enabled)
            lng_path = L"";
    }
    return enabled;
}

int get_l10n_setting(const char *keyname, char *buf, int size)
{
    if (getEnabled()) {
        WCHAR w_key[256], w_buf[256];
        mbs_to_ws(keyname, w_key, lenof(w_key));
        GetPrivateProfileStringW(lng_section, w_key, L"\n:", w_buf, size, lng_path);
        if (*w_buf != L'\n') {
            ws_to_mbs(w_buf, -1, buf, size);
            return 1;
        }
    }
    *buf = '\0';
    return 0;
}

static HWND override_wndproc(HWND r)
{
    char classname[256];
    DWORD style;
    struct prop *qq;
    enum { TYPE_OFF, TYPE_MAIN, TYPE_OTHER } type;

    if (!r)
        return r;
    GetClassName(r, classname, lenof(classname));
    style = GetWindowLong(r, GWL_STYLE);
    type = TYPE_OFF;
    if (!(style & WS_CHILD)) {
        if (!strcmp(classname, "PuTTY") || !strcmp(classname, "PuTTYtel") || !strcmp(classname, "pterm"))
            type = TYPE_MAIN;
        else
            type = TYPE_OTHER;
    }
    if (type == TYPE_OFF)
        return r;
    if (type == TYPE_OTHER) {
        WCHAR buf[SHORT_STR_MAX];
        if (GetWindowTextW(r, buf, lenof(buf)))
            if (strtranslate(buf, buf, lenof(buf)))
                SetWindowTextW(r, buf);
    }
    if (type == TYPE_MAIN) {
        do {
            qq = (struct prop *)LocalAlloc(0, sizeof *qq);
            if (!qq)
                break;
            if (!SetProp(r, propstr, (HANDLE)qq)) {
                LocalFree((HANDLE)qq);
                break;
            }
            *qq = defaultprop;
            qq->is_unicode = IsWindowUnicode(r);
            qq->oldproc = qq->is_unicode
                ? (WNDPROC)SetWindowLongPtrW(r, GWLP_WNDPROC, (LONG_PTR)wndproc)
                : (WNDPROC)SetWindowLongPtrA(r, GWLP_WNDPROC, (LONG_PTR)wndproc);
        } while (0);
    }
    return r;
}

static void translate_children(HWND hwnd)
{
    HWND a;
    LOGFONTW l;

    a = GetWindow(hwnd, GW_CHILD);
    while (a) {
        translate_children(a);
        WCHAR buf[SHORT_STR_MAX];
        if (GetWindowTextW(a, buf, lenof(buf)))
            if (strtranslate(buf, buf, lenof(buf)))
                SetWindowTextW(a, buf);
        if (GetObjectW((HGDIOBJ)SendMessage(a, WM_GETFONT, 0, 0),
            sizeof(l), &l)) {
            if (!_wcsicmp(l.lfFaceName, ofontname))
                SendMessage(a, WM_SETFONT, (WPARAM)hfont,
                    MAKELPARAM(TRUE, 0));
        }
        a = GetWindow(a, GW_HWNDNEXT);
    }
}

static INT_PTR CALLBACK dlgproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    DLGPROC proc;
    struct prop *p;

    if (message == WM_INITDIALOG) {
        override_wndproc(hwnd);
        translate_children(hwnd);
        p = (struct prop *)LocalAlloc(0, sizeof(*p));
        if (p && !SetProp(hwnd, propstr, (HANDLE)p))
            LocalFree((HANDLE)p);
        else if (p)
            *p = defaultprop;
    }
    p = GetProp(hwnd, propstr);
    if (!p)
        p = &defaultprop;
    if (message == WM_INITDIALOG)
        p->olddlgproc = lastolddlgproc;
    proc = p->olddlgproc;
    return proc(hwnd, message, wparam, lparam);
}

#undef MessageBoxA
int l10nMessageBoxA(HWND hWnd, LPCSTR text, LPCSTR caption, UINT type)
{
    if (!getEnabled())
        return MessageBoxA(hWnd, text, caption, type);
    WCHAR textw[OUT_STR_MAX_W], captionw[SHORT_STR_MAX];
    cwstrtranslatefb(text, textw, lenof(textw));
    cwstrtranslatefb(caption, captionw, lenof(captionw));
    return MessageBoxW(hWnd, textw, captionw, type);
}

static bool is_ptr_resource_id(void *ptr)
{
    return ptr <= (void *)0xffff;
}

#undef CreateWindowExA
HWND l10nCreateWindowExA(DWORD a1, LPCSTR a2, LPCSTR a3, DWORD a4, int a5, int a6,
    int a7, int a8, HWND a9, HMENU a10, HINSTANCE a11, LPVOID a12)
{
    HWND r;

    if (getEnabled() && !is_ptr_resource_id(a2) && !IsBadStringPtr(a2, 100)) {
        if (stricmp(a2, WC_TREEVIEW) == 0) a2 = WC_HOOK(TREEVIEW);
        else if (stricmp(a2, WC_BUTTON) == 0) a2 = WC_HOOK(BUTTON);
        else if (stricmp(a2, WC_STATIC) == 0) a2 = WC_HOOK(STATIC);
        else if (stricmp(a2, WC_EDIT) == 0) a2 = WC_HOOK(EDIT);
        else if (stricmp(a2, WC_LISTBOX) == 0) a2 = WC_HOOK(LISTBOX);
        else if (stricmp(a2, WC_COMBOBOX) == 0) a2 = WC_HOOK(COMBOBOX);
    }
    r = CreateWindowExA(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
    if (!getEnabled())
        return r;
    return override_wndproc(r);
}

#undef CreateWindowExW
HWND l10nCreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle,
    int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance,
    LPVOID lpParam)
{
    HWND window;

    if (getEnabled() && !is_ptr_resource_id(lpClassName) && !IsBadStringPtrW(lpClassName, 100)) {
        if (_wcsicmp(lpClassName, WC_TREEVIEWW) == 0) lpClassName = WC_HOOKW(TREEVIEW);
        else if (_wcsicmp(lpClassName, WC_BUTTONW) == 0) lpClassName = WC_HOOKW(BUTTON);
        else if (_wcsicmp(lpClassName, WC_STATICW) == 0) lpClassName = WC_HOOKW(STATIC);
        else if (_wcsicmp(lpClassName, WC_EDITW) == 0) lpClassName = WC_HOOKW(EDIT);
        else if (_wcsicmp(lpClassName, WC_LISTBOXW) == 0) lpClassName = WC_HOOKW(LISTBOX);
        else if (_wcsicmp(lpClassName, WC_COMBOBOXW) == 0) lpClassName = WC_HOOKW(COMBOBOX);
    }
    window = CreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight,
        hWndParent, hMenu, hInstance, lpParam);
    if (!getEnabled())
        return window;
    return override_wndproc(window);
}

#undef DialogBoxParamA
INT_PTR l10nDialogBoxParamA(HINSTANCE a1, LPCSTR a2, HWND a3, DLGPROC a4, LPARAM a5)
{
    if (!getEnabled())
        return DialogBoxParamA(a1, a2, a3, a4, a5);
    lastolddlgproc = a4;
    return DialogBoxParamA(a1, a2, a3, dlgproc, a5);
}

#undef CreateDialogParamA
HWND l10nCreateDialogParamA(HINSTANCE a1, LPCSTR a2, HWND a3, DLGPROC a4, LPARAM a5)
{
    HWND r;

    r = CreateDialogParamA(a1, a2, a3, a4, a5);
    if (getEnabled()) {
        override_wndproc(r);
        translate_children(r);
    }
    return r;
}

const char *l10n_translate_s(const char *str, char *buf, size_t len)
{
    const char *ptr = str;
    if (getEnabled() && ccstrtranslate(str, buf, len))
        ptr = buf;
    return ptr;
}

char *l10n_dupstr(const char *str)
{
    char buf[OUT_STR_MAX_A];
    return dupstr(l10n_translate(str, buf));
}

void l10n_created_window(HWND hwnd)
{
    if (getEnabled())
        translate_children(hwnd);
}

HFONT l10n_getfont(HFONT f)
{
    LOGFONTW l;

    if (getEnabled() && GetObjectW((HGDIOBJ)f, sizeof(l), &l)) {
        if (!_wcsicmp(l.lfFaceName, ofontname))
            return hfont;
    }
    return f;
}

int l10n_sprintf(char *buffer, const char *format, ...)
{
    int r;
    char format2[OUT_STR_MAX_A];
    va_list args;
    va_start(args, format);
    if (getEnabled()) {
        if (ccstrtranslate(format, format2, lenof(format2)))
            format = format2;
    }
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
int l10n_vsnprintf(char *buffer, int size, const char *format, va_list args)
{
    char format2[OUT_STR_MAX_A];
    if (getEnabled()) {
        if (ccstrtranslate(format, format2, lenof(format2)))
            format = format2;
    }
    return vsnprintf(buffer, size, format, args);
}

#undef dupvprintf
char *l10n_dupvprintf(const char *format, va_list ap)
{
    char format2[OUT_STR_MAX_A];
    if (getEnabled()) {
        if (ccstrtranslate(format, format2, lenof(format2)))
            format = format2;
    }
    return dupvprintf(format, ap);
}

char *l10n_dupprintf(const char *format, ...)
{
    char *r;
    char format2[OUT_STR_MAX_A];
    va_list args;
    va_start(args, format);
    if (getEnabled()) {
        if (ccstrtranslate(format, format2, lenof(format2)))
            format = format2;
    }
    r = dupvprintf(format, args);
    va_end(args);
    return r;
}

static int getOpenSaveFilename(OPENFILENAME *ofn, int (WINAPI *f)(OPENFILENAME *))
{
    char title[SHORT_STR_MAX];
    char file_title[SHORT_STR_MAX];
    char filter[SHORT_STR_MAX];
    if (getEnabled()) {
        if (ofn->lpstrTitle != NULL && ccstrtranslate(ofn->lpstrTitle, title, lenof(title)))
            ofn->lpstrTitle = title;
        if (ofn->lpstrFileTitle != NULL && ccstrtranslate(ofn->lpstrFileTitle, file_title, lenof(file_title)))
            ofn->lpstrFileTitle = file_title;
        if (ofn->lpstrFilter != NULL && ccstrtranslate(ofn->lpstrFilter, filter, lenof(filter)))
            ofn->lpstrFilter = filter;
    }
    return f(ofn);
}

#undef GetOpenFileNameA
int l10nGetOpenFileNameA(OPENFILENAMEA *ofn)
{
    return getOpenSaveFilename(ofn, GetOpenFileNameA);
}

#undef GetSaveFileNameA
int l10nGetSaveFileNameA(OPENFILENAMEA *ofn)
{
    return getOpenSaveFilename(ofn, GetSaveFileNameA);
}

BOOL l10nSetDlgItemText(HWND dialog, int id, LPCSTR text)
{
    WCHAR buf[SHORT_STR_MAX];
    if (getEnabled() && cwstrtranslate(text, buf, lenof(buf)))
        return SetDlgItemTextW(dialog, id, buf);
    return SetDlgItemText(dialog, id, text);
}

LRESULT l10nSendDlgItemMessage(HWND dialog, int id, UINT msg, WPARAM wp, LPARAM lp)
{
    WCHAR buf[SHORT_STR_MAX];
    if (getEnabled() && cwstrtranslate((const char *)lp, buf, lenof(buf)))
        return SendDlgItemMessageW(dialog, id, msg, wp, (LPARAM)buf);
    return SendDlgItemMessage(dialog, id, msg, wp, lp);
}

BOOL l10nAppendMenu(HMENU menu, UINT flags, UINT_PTR id, LPCSTR text)
{
    WCHAR buf[SHORT_STR_MAX];
    if (getEnabled() && flags != MF_SEPARATOR && cwstrtranslate(text, buf, lenof(buf)))
        return AppendMenuW(menu, flags, id, buf);
    return AppendMenu(menu, flags, id, text);
}

BOOL SetDlgItemTextCp1252(HWND dialog, int item, LPCSTR text)
{
    WCHAR *text_w = strdup_to_wc(text, 1252);
    BOOL result = SetDlgItemTextW(dialog, item, text_w);
    sfree(text_w);
    return result;
}
