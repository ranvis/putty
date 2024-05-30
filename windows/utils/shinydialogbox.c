/*
 * PuTTY's own reimplementation of DialogBox() and EndDialog() which
 * provide extra capabilities.
 *
 * Originally introduced in 2003 to work around a problem with our
 * message loops, in which keystrokes pressed in the 'Change Settings'
 * dialog in mid-session would _also_ be delivered to the main
 * terminal window.
 *
 * But once we had our own wrapper it was convenient to put further
 * things into it. In particular, this system allows you to provide a
 * context pointer at setup time that's easy to retrieve from the
 * window procedure.
 */

#include "putty.h"

struct ShinyDialogBoxState {
    bool ended;
    int result;
    ShinyDlgProc proc;
    void *ctx;
};

static HWND CreateDialogWithFontSize(HINSTANCE hinst, LPCTSTR tmpl, HWND hwndparent, DLGPROC lpDialogFunc);

/*
 * For use in re-entrant calls to the dialog procedure from
 * CreateDialog itself, temporarily store the state pointer that we
 * won't store in the usual window-memory slot until later.
 *
 * I don't _intend_ that this system will need to be used in multiple
 * threads at all, let alone concurrently, but just in case, declaring
 * sdb_tempstate as thread-local will protect against that possibility.
 */
static THREADLOCAL struct ShinyDialogBoxState *sdb_tempstate;

static inline struct ShinyDialogBoxState *ShinyDialogGetState(HWND hwnd)
{
    if (sdb_tempstate)
        return sdb_tempstate;
    return (struct ShinyDialogBoxState *)GetWindowLongPtr(
        hwnd, DLGWINDOWEXTRA);
}

static INT_PTR CALLBACK ShinyRealDlgProc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam)
{
    struct ShinyDialogBoxState *state = ShinyDialogGetState(hwnd);
    return state->proc(hwnd, msg, wParam, lParam, state->ctx);
}

int ShinyDialogBox(HINSTANCE hinst, LPCTSTR tmpl, const char *winclass,
                   HWND hwndparent, ShinyDlgProc proc, void *ctx)
{
    /*
     * Register the window class ourselves in such a way that we
     * allocate an extra word of window memory to store the state
     * pointer.
     *
     * It would be nice in principle to load the dialog template
     * resource and dig the class name out of it. But DLGTEMPLATEEX is
     * a nasty variable-layout structure not declared conveniently in
     * a header file, so I think that's too much effort. Callers of
     * this function will just have to provide the right window class
     * name to match their template resource via the 'winclass'
     * parameter.
     */
    WNDCLASS wc;
    wc.style = CS_DBLCLKS | CS_SAVEBITS | CS_BYTEALIGNWINDOW;
    wc.lpfnWndProc = DefDlgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = DLGWINDOWEXTRA + sizeof(LONG_PTR);
    wc.hInstance = hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_BACKGROUND +1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = winclass;
    RegisterClass(&wc);

    struct ShinyDialogBoxState state[1];
    state->ended = false;
    state->proc = proc;
    state->ctx = ctx;

    sdb_tempstate = state;
    HWND hwnd = CreateDialogWithFontSize(hinst, tmpl, hwndparent, ShinyRealDlgProc);
    l10n_created_window(hwnd);
    SetWindowLongPtr(hwnd, DLGWINDOWEXTRA, (LONG_PTR)state);
    sdb_tempstate = NULL;

    MSG msg;
    int gm;
    while ((gm = GetMessage(&msg, NULL, 0, 0)) > 0) {
        if (!IsWindow(hwnd) || !IsDialogMessage(hwnd, &msg))
            DispatchMessage(&msg);
        if (state->ended)
            break;
    }

    if (gm == 0)
        PostQuitMessage(msg.wParam); /* We got a WM_QUIT, pass it on */

    DestroyWindow(hwnd);
    return state->result;
}

void ShinyEndDialog(HWND hwnd, int ret)
{
    struct ShinyDialogBoxState *state = ShinyDialogGetState(hwnd);
    state->result = ret;
    state->ended = true;
}

static WORD *skip_dialog_string(WORD *ptr)
{
    while (*ptr++) {
        (void)0;
    }
    return ptr;
}

static WORD *skip_dialog_x_array(WORD *ptr)
{
    if (*ptr == 0xffff) {
        return ptr + 2; // ordinal value of a menu resource
    }
    return skip_dialog_string(ptr);
}

// https://learn.microsoft.com/en-us/windows/win32/dlgbox/dlgtemplateex
#pragma pack(push, 2)
typedef struct DLGTEMPLATEEX_t {
    WORD dlgVer;
    WORD signature;
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    WORD cDlgItems;
    short x;
    short y;
    short cx;
    short cy;
} DLGTEMPLATEEX;
#pragma pack(pop)

static HGLOBAL get_dialog_with_font_size(HINSTANCE instance, LPCTSTR tmpl)
{
    int font_size = app_conf_get_int("DialogFontSize", 0, 0, 10);
    if (!font_size)
        return NULL;
    HRSRC dialogRes = FindResource(instance, tmpl, RT_DIALOG);
    DWORD resSize = SizeofResource(instance, dialogRes);
    HGLOBAL mem_dialog = GlobalAlloc(GMEM_FIXED, resSize);
    if (!mem_dialog)
        goto bail;
    HGLOBAL dialog = LoadResource(instance, dialogRes);
    const VOID *dialog_p = LockResource(dialog);
    if (!dialog_p)
        goto bail;
    DLGTEMPLATEEX *dialog_tmpl = (DLGTEMPLATEEX *)mem_dialog; // On Win32, GMEM_FIXED memory can be used as a pointer directly
    memcpy(dialog_tmpl, dialog_p, resSize);
    UnlockResource(dialog);
    if (dialog_tmpl->signature == 0xffff && dialog_tmpl->style & DS_SETFONT) { // is EX and has font size and typeface
        WORD *ptr = (WORD *)(dialog_tmpl + 1);
        ptr = skip_dialog_x_array(ptr); // menu
        ptr = skip_dialog_x_array(ptr); // class
        ptr = skip_dialog_string(ptr); // title
        *ptr = max(5, min(32, *ptr + font_size)); // font size
    }
    return mem_dialog;
bail:
    if (mem_dialog)
        GlobalFree(mem_dialog);
    return NULL;
}

static HWND CreateDialogWithFontSize(HINSTANCE hinst, LPCTSTR tmpl, HWND hwndparent, DLGPROC lpDialogFunc)
{
    HWND hwnd;
    HGLOBAL dialog = get_dialog_with_font_size(hinst, tmpl);
    if (dialog) {
        hwnd = CreateDialogIndirect(hinst, dialog, hwndparent, lpDialogFunc);
        GlobalFree(dialog);
    } else {
        hwnd = CreateDialog(hinst, tmpl, hwndparent, lpDialogFunc);
    }
    return hwnd;
}
