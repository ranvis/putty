/*
 * Pageant: the PuTTY Authentication Agent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include <assert.h>
#include <tchar.h>
#include <malloc.h>

#include "putty.h"
#include "ssh.h"
#include "misc.h"
#include "tree234.h"
#include "winsecur.h"
#include "wincapi.h"
#include "pageant.h"
#include "licence.h"
#include "pageant-rc.h"

#include <shellapi.h>
#include <shlobj.h>

#ifndef NO_SECURITY
#include <aclapi.h>
#ifdef DEBUG_IPC
#define _WIN32_WINNT 0x0500            /* for ConvertSidToStringSid */
#include <sddl.h>
#endif
#endif

#define WM_SYSTRAY   (WM_APP + 6)
#define WM_SYSTRAY2  (WM_APP + 7)
#define TID_ADDSYSTRAY  1
#define TID_DEFERRED_REENCRYPT  500
#define TID_REENCRYPT_IDLE      501
#define TID_REENCRYPT_TIMEOUT   502

#define AGENT_COPYDATA_ID 0x804e50ba   /* random goop */

#define APPNAME "Pageant"

/* Titles and class names for invisible windows. IPCWINTITLE and
 * IPCCLASSNAME are critical to backwards compatibility: WM_COPYDATA
 * based Pageant clients will call FindWindow with those parameters
 * and expect to find the Pageant IPC receiver. */
#define TRAYWINTITLE  "Pageant"
#define TRAYCLASSNAME "PageantSysTray"
#define IPCWINTITLE   "Pageant"
#define IPCCLASSNAME  "Pageant"

static HWND traywindow;
static HWND keylist;
static HWND aboutbox;
static HMENU systray_menu, session_menu;
static bool already_running;
static FingerprintType fptype = SSH_FPTYPE_DEFAULT;

static int confirm_any_request = 0;
static int defer_reencrypt_state = 0;
static bool deferred_reencrypt;
static struct {
    bool on_suspend, on_display_off, on_away, on_lid_close;
    int idle_ms, timeout_ms;
} reencrypt_settings;
static char *putty_path;
static bool restrict_putty_acl = false;

/* CWD for "add key" file requester. */
static filereq *keypath = NULL;

/* From MSDN: In the WM_SYSCOMMAND message, the four low-order bits of
 * wParam are used by Windows, and should be masked off, so we shouldn't
 * attempt to store information in them. Hence all these identifiers have
 * the low 4 bits clear. Also, identifiers should < 0xF000. */

#define IDM_CLOSE              0x0010
#define IDM_VIEWKEYS           0x0020
#define IDM_ADDKEY             0x0030
#define IDM_ADDKEY_ENCRYPTED   0x0040
#define IDM_REMOVE_ALL         0x0050
#define IDM_REENCRYPT_ALL      0x0060
#define IDM_HELP               0x0070
#define IDM_ABOUT              0x0080
#define IDM_PUTTY              0x0090
#define IDM_CONFIRM_REQUEST    0x0210
#define IDM_SESSIONS_BASE      0x1000
#define IDM_SESSIONS_MAX       0x2000
#define PUTTY_REGKEY      "Software\\SimonTatham\\PuTTY\\Sessions"
#define PUTTY_DEFAULT     "Default%20Settings"
static int initial_menuitems_count;

static int use_inifile;
static char inifile[2 * MAX_PATH + 10] = {'\0'};
static const char HEADER[] = "Session:";

#undef AppendMenu
#define AppendMenu(m, f, i, t) l10nAppendMenu(m, f, i, t)

#define pageant_reencrypt_all() USE_gui_reencrypt_all_INSTEAD()
static void gui_reencrypt_all(void);

#define LOCAL_SCOPE

static int get_use_inifile(void)
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

/*
 * Print a modal (Really Bad) message box and perform a fatal exit.
 */
void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    char *buf;

    va_start(ap, fmt);
    buf = dupvprintf(fmt, ap);
    va_end(ap);
    MessageBox(traywindow, buf, "Pageant Fatal Error",
               MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
    sfree(buf);
    exit(1);
}

static bool has_security;

struct PassphraseProcStruct {
    bool modal;
    const char *help_topic;
    PageantClientDialogId *dlgid;
    char *passphrase;
    const char *comment;
};

/*
 * Dialog-box function for the Licence box.
 */
static INT_PTR CALLBACK LicenceProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG:
        SetDlgItemText(hwnd, IDC_LICENCE_TEXTBOX, LICENCE_TEXT("\r\n\r\n"));
        return 1;
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
          case IDCANCEL:
            EndDialog(hwnd, 1);
            return 0;
        }
        return 0;
      case WM_CLOSE:
        EndDialog(hwnd, 1);
        return 0;
    }
    return 0;
}

/*
 * Dialog-box function for the About box.
 */
static INT_PTR CALLBACK AboutProc(HWND hwnd, UINT msg,
                              WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG: {
        char *buildinfo_text = buildinfo("\r\n");
        char *text = dupprintf
            ("Pageant\r\n\r\n%s\r\n\r\n%s\r\n\r\n%s",
             ver, buildinfo_text,
             "\251 " SHORT_COPYRIGHT_DETAILS ". All rights reserved.");
        sfree(buildinfo_text);
        SetDlgItemText(hwnd, IDC_ABOUT_TEXTBOX, text);
        MakeDlgItemBorderless(hwnd, IDC_ABOUT_TEXTBOX);
        sfree(text);
        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
          case IDCANCEL:
            aboutbox = NULL;
            DestroyWindow(hwnd);
            return 0;
          case IDC_ABOUT_LICENCE:
            EnableWindow(hwnd, 0);
            DialogBox(hinst, MAKEINTRESOURCE(IDD_LICENCE), hwnd, LicenceProc);
            EnableWindow(hwnd, 1);
            SetActiveWindow(hwnd);
            return 0;
          case IDC_ABOUT_WEBSITE:
            /* Load web browser */
            ShellExecute(hwnd, "open",
                         "https://www.ranvis.com/putty",
                         0, 0, SW_SHOWDEFAULT);
            return 0;
        }
        return 0;
      case WM_CLOSE:
        aboutbox = NULL;
        DestroyWindow(hwnd);
        return 0;
    }
    return 0;
}

static HWND modal_passphrase_hwnd = NULL;
static HWND nonmodal_passphrase_hwnd = NULL;

static void end_passphrase_dialog(HWND hwnd, INT_PTR result)
{
    struct PassphraseProcStruct *p = (struct PassphraseProcStruct *)
        GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (reencrypt_settings.timeout_ms) {
        SetTimer(traywindow, TID_REENCRYPT_TIMEOUT, reencrypt_settings.timeout_ms, NULL);
    }
    if (p->modal) {
        EndDialog(hwnd, result);
    } else {
        /*
         * Destroy this passphrase dialog box before passing the
         * results back to pageant.c, to avoid re-entrancy issues.
         *
         * If we successfully got a passphrase from the user, but it
         * was _wrong_, then pageant_passphrase_request_success will
         * respond by calling back - synchronously - to our
         * ask_passphrase() implementation, which will expect the
         * previous value of nonmodal_passphrase_hwnd to have already
         * been cleaned up.
         */
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) NULL);
        DestroyWindow(hwnd);
        nonmodal_passphrase_hwnd = NULL;

        if (result)
            pageant_passphrase_request_success(
                p->dlgid, ptrlen_from_asciz(p->passphrase));
        else
            pageant_passphrase_request_refused(p->dlgid);

        burnstr(p->passphrase);
        sfree(p);
    }
}

/*
 * Dialog-box function for the passphrase box.
 */
static INT_PTR CALLBACK PassphraseProc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam)
{
    struct PassphraseProcStruct *p;

    if (msg == WM_INITDIALOG) {
        if (reencrypt_settings.timeout_ms) {
            KillTimer(traywindow, TID_REENCRYPT_TIMEOUT);
        }
        p = (struct PassphraseProcStruct *) lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) p);
    } else {
        p = (struct PassphraseProcStruct *)
            GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    switch (msg) {
      case WM_INITDIALOG: {
        if (p->modal)
            modal_passphrase_hwnd = hwnd;

        /*
         * Centre the window.
         */
        RECT rs, rd;
        HWND hw;

        hw = GetDesktopWindow();
        if (GetWindowRect(hw, &rs) && GetWindowRect(hwnd, &rd))
            MoveWindow(hwnd,
                       (rs.right + rs.left + rd.left - rd.right) / 2,
                       (rs.bottom + rs.top + rd.top - rd.bottom) / 2,
                       rd.right - rd.left, rd.bottom - rd.top, true);

        SetForegroundWindow(hwnd);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        if (!p->modal)
            SetActiveWindow(hwnd); /* this won't have happened automatically */
        if (p->comment)
            SetDlgItemText(hwnd, IDC_PASSPHRASE_FINGERPRINT, p->comment);
        burnstr(p->passphrase);
        p->passphrase = dupstr("");
        SetDlgItemText(hwnd, IDC_PASSPHRASE_EDITBOX, p->passphrase);
        if (!p->help_topic || !has_help()) {
            HWND item = GetDlgItem(hwnd, IDHELP);
            if (item)
                DestroyWindow(item);
        }
        return 0;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
            if (p->passphrase)
                end_passphrase_dialog(hwnd, 1);
            else
                MessageBeep(0);
            return 0;
          case IDCANCEL:
            end_passphrase_dialog(hwnd, 0);
            return 0;
          case IDHELP:
            if (p->help_topic)
                launch_help(hwnd, p->help_topic);
            return 0;
          case IDC_PASSPHRASE_EDITBOX:
            if ((HIWORD(wParam) == EN_CHANGE) && p->passphrase) {
                burnstr(p->passphrase);
                p->passphrase = GetDlgItemText_alloc(
                    hwnd, IDC_PASSPHRASE_EDITBOX);
            }
            return 0;
        }
        return 0;
      case WM_CLOSE:
        end_passphrase_dialog(hwnd, 0);
        return 0;
    }
    return 0;
}

/*
 * Warn about the obsolescent key file format.
 */
void old_keyfile_warning(void)
{
    static const char mbtitle[] = "PuTTY Key File Warning";
    static const char message[] =
        "You are loading an SSH-2 private key which has an\n"
        "old version of the file format. This means your key\n"
        "file is not fully tamperproof. Future versions of\n"
        "PuTTY may stop supporting this private key format,\n"
        "so we recommend you convert your key to the new\n"
        "format.\n"
        "\n"
        "You can perform this conversion by loading the key\n"
        "into PuTTYgen and then saving it again.";

    MessageBox(traywindow, message, mbtitle, MB_OK);
}

struct keylist_update_ctx {
    bool enable_remove_controls;
    bool enable_reencrypt_controls;
};

static void keylist_update_callback(
    void *vctx, char **fingerprints, const char *comment, uint32_t ext_flags,
    struct pageant_pubkey *key)
{
    struct keylist_update_ctx *ctx = (struct keylist_update_ctx *)vctx;
    FingerprintType this_type = ssh2_pick_fingerprint(fingerprints, fptype);
    const char *fingerprint = fingerprints[this_type];
    strbuf *listentry = strbuf_new();

    /* There is at least one key, so the controls for removing keys
     * should be enabled */
    ctx->enable_remove_controls = true;

    switch (key->ssh_version) {
      case 1: {
        strbuf_catf(listentry, "ssh1\t%s\t%s", fingerprint, comment);

        /*
         * Replace the space in the fingerprint (between bit count and
         * hash) with a tab, for nice alignment in the box.
         */
        char *p = strchr(listentry->s, ' ');
        if (p)
            *p = '\t';
        break;
      }

      case 2: {
        /*
         * For nice alignment in the list box, we would ideally want
         * every entry to align to the tab stop settings, and have a
         * column for algorithm name, one for bit count, one for hex
         * fingerprint, and one for key comment.
         *
         * Unfortunately, some of the algorithm names are so long that
         * they overflow into the bit-count field. Fortunately, at the
         * moment, those are _precisely_ the algorithm names that
         * don't need a bit count displayed anyway (because for
         * NIST-style ECDSA the bit count is mentioned in the
         * algorithm name, and for ssh-ed25519 there is only one
         * possible value anyway). So we fudge this by simply omitting
         * the bit count field in that situation.
         *
         * This is fragile not only in the face of further key types
         * that don't follow this pattern, but also in the face of
         * font metrics changes - the Windows semantics for list box
         * tab stops is that \t aligns to the next one you haven't
         * already exceeded, so I have to guess when the key type will
         * overflow past the bit-count tab stop and leave out a tab
         * character. Urgh.
         */
        BinarySource src[1];
        BinarySource_BARE_INIT_PL(src, ptrlen_from_strbuf(key->blob));
        ptrlen algname = get_string(src);
        const ssh_keyalg *alg = find_pubkey_alg_len(algname);

        bool include_bit_count = (alg == &ssh_dss || alg == &ssh_rsa);

        int wordnumber = 0;
        for (const char *p = fingerprint; *p; p++) {
            char c = *p;
            if (c == ' ') {
                if (wordnumber < 2)
                    c = '\t';
                wordnumber++;
            }
            if (include_bit_count || wordnumber != 1)
                put_byte(listentry, c);
        }

        strbuf_catf(listentry, "\t%s", comment);
        break;
      }
    }

    if (ext_flags & LIST_EXTENDED_FLAG_HAS_NO_CLEARTEXT_KEY) {
        char *text = l10n_dupstr("\t(encrypted)");
        strbuf_catf(listentry, text);
        sfree(text);
    } else if (ext_flags & LIST_EXTENDED_FLAG_HAS_ENCRYPTED_KEY_FILE) {
        char *text = l10n_dupstr("\t(re-encryptable)");
        strbuf_catf(listentry, text);
        sfree(text);

        /* At least one key can be re-encrypted */
        ctx->enable_reencrypt_controls = true;
    }

    SendDlgItemMessage(keylist, IDC_KEYLIST_LISTBOX,
                       LB_ADDSTRING, 0, (LPARAM)listentry->s);
    strbuf_free(listentry);
}

/*
 * Update the visible key list.
 */
void keylist_update(void)
{
    if (keylist) {
        SendDlgItemMessage(keylist, IDC_KEYLIST_LISTBOX,
                           LB_RESETCONTENT, 0, 0);

        char *errmsg;
        struct keylist_update_ctx ctx[1];
        ctx->enable_remove_controls = false;
        ctx->enable_reencrypt_controls = false;
        int status = pageant_enum_keys(keylist_update_callback, ctx, &errmsg);
        assert(status == PAGEANT_ACTION_OK);
        assert(!errmsg);

        SendDlgItemMessage(keylist, IDC_KEYLIST_LISTBOX,
                           LB_SETCURSEL, (WPARAM) - 1, 0);

        EnableWindow(GetDlgItem(keylist, IDC_KEYLIST_REMOVE),
                     ctx->enable_remove_controls);
        EnableWindow(GetDlgItem(keylist, IDC_KEYLIST_REENCRYPT),
                     ctx->enable_reencrypt_controls);
    }
}

static void win_add_keyfile(Filename *filename, bool encrypted)
{
    char *err;
    int ret;

    /*
     * Try loading the key without a passphrase. (Or rather, without a
     * _new_ passphrase; pageant_add_keyfile will take care of trying
     * all the passphrases we've already stored.)
     */
    ret = pageant_add_keyfile(filename, NULL, &err, encrypted);
    if (ret == PAGEANT_ACTION_OK) {
        goto done;
    } else if (ret == PAGEANT_ACTION_FAILURE) {
        goto error;
    }

    /*
     * OK, a passphrase is needed, and we've been given the key
     * comment to use in the passphrase prompt.
     */
    while (1) {
        INT_PTR dlgret;
        struct PassphraseProcStruct pps;
        pps.modal = true;
        pps.help_topic = NULL;         /* this dialog has no help button */
        pps.dlgid = NULL;
        pps.passphrase = NULL;
        pps.comment = err;
        dlgret = DialogBoxParam(
            hinst, MAKEINTRESOURCE(IDD_LOAD_PASSPHRASE),
            NULL, PassphraseProc, (LPARAM) &pps);
        modal_passphrase_hwnd = NULL;

        if (!dlgret) {
            burnstr(pps.passphrase);
            goto done;                 /* operation cancelled */
        }

        sfree(err);

        assert(pps.passphrase != NULL);

        ret = pageant_add_keyfile(filename, pps.passphrase, &err, false);
        burnstr(pps.passphrase);

        if (ret == PAGEANT_ACTION_OK) {
            goto done;
        } else if (ret == PAGEANT_ACTION_FAILURE) {
            goto error;
        }
    }

  error:
    message_box(traywindow, err, APPNAME, MB_OK | MB_ICONERROR,
                HELPCTXID(errors_cantloadkey));
  done:
    sfree(err);
    return;
}

/*
 * Prompt for a key file to add, and add it.
 */
static void prompt_add_keyfile(bool encrypted)
{
    OPENFILENAME of;
    char *filelist = snewn(8192, char);

    if (!keypath) keypath = filereq_new();
    memset(&of, 0, sizeof(of));
    of.hwndOwner = traywindow;
    of.lpstrFilter = FILTER_KEY_FILES;
    of.lpstrCustomFilter = NULL;
    of.nFilterIndex = 1;
    of.lpstrFile = filelist;
    *filelist = '\0';
    of.nMaxFile = 8192;
    of.lpstrFileTitle = NULL;
    of.lpstrTitle = "Select Private Key File";
    of.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (request_file(keypath, &of, true, false)) {
        if(strlen(filelist) > of.nFileOffset) {
            /* Only one filename returned? */
            Filename *fn = filename_from_str(filelist);
            win_add_keyfile(fn, encrypted);
            filename_free(fn);
        } else {
            /* we are returned a bunch of strings, end to
             * end. first string is the directory, the
             * rest the filenames. terminated with an
             * empty string.
             */
            char *dir = filelist;
            char *filewalker = filelist + strlen(dir) + 1;
            while (*filewalker != '\0') {
                char *filename = dupcat(dir, "\\", filewalker);
                Filename *fn = filename_from_str(filename);
                win_add_keyfile(fn, encrypted);
                filename_free(fn);
                sfree(filename);
                filewalker += strlen(filewalker) + 1;
            }
        }

        keylist_update();
        pageant_forget_passphrases();
    }
    sfree(filelist);
}

static int getConfValueInt(const char *name, int def, int min, int max)
{
    int value = def;
    if (get_use_inifile()) {
        value = GetPrivateProfileInt(APPNAME, name, def, inifile);
    } else {
        HKEY hkey;
        if (RegOpenKey(HKEY_CURRENT_USER, "Software\\SimonTatham\\PuTTY\\" APPNAME, &hkey) == ERROR_SUCCESS) {
            DWORD type;
            DWORD size = sizeof value;
            if (RegQueryValueEx(hkey, name, 0, &type, (LPBYTE)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
                value = def;
            RegCloseKey(hkey);
        }
    }
    if (value < min || max < value) {
        value = def;
    }
    return value;
}

struct ConfirmAcceptAgentProcStruct {
    int type;
    const char* fingerprint;
    int dont_ask_again;
    DWORD tickCount;
    int delay, timeout;
};

#define DELAY_TIMER_ID 1
#define TIMEOUT_TIMER_ID 2

INT_PTR CALLBACK ConfirmAcceptAgentProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    BOOL l10nSetDlgItemText(HWND dialog, int id, LPCSTR text);

    switch (message) {
    case WM_INITDIALOG:
        {
            struct ConfirmAcceptAgentProcStruct* info = (struct ConfirmAcceptAgentProcStruct*) lParam;
            const char* s;
            SetWindowLongPtr(dialog, DWLP_USER, lParam);
            switch (info->type) {
            case SSH1_AGENTC_RSA_CHALLENGE:
            case SSH2_AGENTC_SIGN_REQUEST:
                s = "Accept query of the following key?";
                break;
            case SSH1_AGENTC_ADD_RSA_IDENTITY:
            case SSH2_AGENTC_ADD_IDENTITY:
                s = "Accept addition of the following key?";
                break;
            case SSH1_AGENTC_REMOVE_RSA_IDENTITY:
            case SSH2_AGENTC_REMOVE_IDENTITY:
                s = "Accept deletion of the following key?";
                break;
            case SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES:
                s = "Accept deletion of all SSH1 identities?";
                break;
            case SSH2_AGENTC_REMOVE_ALL_IDENTITIES:
                s = "Accept deletion of all SSH2 identities?";
                break;
            default:
                EndDialog(dialog, IDNO);
                return FALSE;
            }
            l10nSetDlgItemText(dialog, 100, s);
            if (info->fingerprint != NULL)
                SetDlgItemText(dialog, 101, info->fingerprint);

            SendDlgItemMessage(dialog, 102, BM_SETCHECK, info->dont_ask_again >= 0 ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessage(dialog, WM_NEXTDLGCTL, (WPARAM) GetDlgItem(dialog, info->dont_ask_again > 0 ? IDYES : IDNO), TRUE);

            info->delay = getConfValueInt("ConfirmDelayMs", 400, 0, 10000);
            if (info->delay) {
                SetTimer(dialog, DELAY_TIMER_ID, info->delay, NULL);
                EnableWindow(GetDlgItem(dialog, IDYES), FALSE);
            }
            info->timeout = getConfValueInt("ConfirmTimeout", 30, 0, 3600) * 1000;
            if (info->timeout) {
                info->tickCount = GetTickCount();
                SetTimer(dialog, TIMEOUT_TIMER_ID, 1000, NULL);
            }
        }
        return FALSE;

    case WM_TIMER:
        if (wParam == TIMEOUT_TIMER_ID) {
            struct ConfirmAcceptAgentProcStruct* info = (struct ConfirmAcceptAgentProcStruct*) GetWindowLongPtr(dialog, DWLP_USER);
            if ((GetTickCount() - info->tickCount) > info->timeout) {
                SendDlgItemMessage(dialog, 102, BM_SETCHECK, BST_UNCHECKED, 0);
                SendMessage(dialog, WM_COMMAND, MAKEWPARAM(info->dont_ask_again > 0 ? IDYES : IDNO, BN_CLICKED), 0);
            }
            return TRUE;
        } else if (wParam == DELAY_TIMER_ID) {
            EnableWindow(GetDlgItem(dialog, IDYES), TRUE);
            KillTimer(dialog, wParam);
        }
        break;

    case WM_COMMAND:
        switch (wParam) {
        case MAKEWPARAM(IDYES, BN_CLICKED):
        case MAKEWPARAM(IDNO, BN_CLICKED):
            {
                struct ConfirmAcceptAgentProcStruct* info = (struct ConfirmAcceptAgentProcStruct*) GetWindowLongPtr(dialog, DWLP_USER);
                info->dont_ask_again = SendDlgItemMessage(dialog, 102, BM_GETCHECK, 0, 0) == BST_CHECKED;
                EndDialog(dialog, LOWORD(wParam));
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int do_accept_agent_request(int type, const RSAKey *rsaKey, const ssh2_userkey *ssh2UserKey)
{
    static const char VALUE_ACCEPT[] = "accept";
    static const char VALUE_REFUSE[] = "refuse";

    const char* keyname;
    char* fingerprint = NULL;
    const size_t fingerprint_length = 512;
    int accept = -1;

    switch (type) {
    case SSH1_AGENTC_RSA_CHALLENGE:
    case SSH2_AGENTC_SIGN_REQUEST:
        keyname = "QUERY:";
        break;
    case SSH1_AGENTC_ADD_RSA_IDENTITY:
        keyname = "ADD:";
        break;
    case SSH2_AGENTC_ADD_IDENTITY:
        keyname = "ADD:";
        break;
    case SSH1_AGENTC_REMOVE_RSA_IDENTITY:
        keyname = "REMOVE:";
        break;
    case SSH2_AGENTC_REMOVE_IDENTITY:
        keyname = "REMOVE:";
        break;
    case SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES:
        keyname = "SSH1_REMOVE_ALL";
        break;
    case SSH2_AGENTC_REMOVE_ALL_IDENTITIES:
        keyname = "SSH2_REMOVE_ALL";
        break;
    default:
        return 0;
    }

    if (type != SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES && type != SSH2_AGENTC_REMOVE_ALL_IDENTITIES) {
        if (rsaKey == NULL && ssh2UserKey == NULL) {
            return 0;
        } else {
            size_t keyname_length = strlen(keyname);
            char* buffer = (char*) alloca(keyname_length + fingerprint_length);
            strcpy(buffer, keyname);
            fingerprint = buffer + keyname_length;
            if (rsaKey != NULL) {
                strcpy(fingerprint, "ssh1:");
                char *rsa_fingerprint = rsa_ssh1_fingerprint((RSAKey *)rsaKey);
                strncat(fingerprint, rsa_fingerprint, keyname_length + fingerprint_length - strlen(fingerprint) - 1);
            } else {
                size_t fp_length;
                char* fp = ssh2_fingerprint(ssh2UserKey->key, SSH_FPTYPE_DEFAULT);
                strncpy(fingerprint, fp, fingerprint_length);
                fp_length = strlen(fingerprint);
                if (fp_length < fingerprint_length - 2) {
                    fingerprint[fp_length] = ' ';
                    strncpy(fingerprint + fp_length + 1, ssh2UserKey->comment,
                            fingerprint_length - fp_length - 1);
                }
            }
            keyname = buffer;
        }
    }

    if (keyname != NULL) {
        char value[8];
        if (get_use_inifile()) {
            char null = '\0';
            GetPrivateProfileString(APPNAME, keyname, &null, value, sizeof value, inifile);
        }else{
            HKEY hkey;
            if (RegOpenKey(HKEY_CURRENT_USER, "Software\\SimonTatham\\PuTTY\\" APPNAME, &hkey) == ERROR_SUCCESS) {
                DWORD type;
                DWORD size = sizeof value;
                if (RegQueryValueEx(hkey, keyname, 0, &type, (LPBYTE) value, &size) != ERROR_SUCCESS || type != REG_SZ)
                    value[0] = '\0';
                RegCloseKey(hkey);
            }
        }
        if (strcmp(value, VALUE_ACCEPT) == 0)
            accept = 1;
        else if (strcmp(value, VALUE_REFUSE) == 0)
            accept = 0;
        else
            accept = -1;
        if (!confirm_any_request && accept >= 0)
            return accept;
    }

    {
        struct ConfirmAcceptAgentProcStruct info = {
            type,
            fingerprint,
            accept,
        };
        HWND focus = GetForegroundWindow();
        DWORD focusThreadId = focus != NULL ? GetWindowThreadProcessId(focus, NULL) : 0;
        DWORD currentThreadId = GetCurrentThreadId();
        if (focusThreadId != 0 && focusThreadId != currentThreadId)
            AttachThreadInput(currentThreadId, focusThreadId, TRUE);
        SetForegroundWindow(traywindow);
        if (focusThreadId != 0 && focusThreadId != currentThreadId)
            AttachThreadInput(currentThreadId, focusThreadId, FALSE);
        accept = DialogBoxParam(hinst, MAKEINTRESOURCE(300), traywindow, ConfirmAcceptAgentProc, (LPARAM) &info) == IDYES ? 1 : 0;
        if (focus != NULL)
            SetForegroundWindow(focus);

        if (keyname != NULL) {
            if (get_use_inifile()) {
                WritePrivateProfileString(APPNAME, keyname, info.dont_ask_again ? accept ? VALUE_ACCEPT : VALUE_REFUSE : NULL, inifile);
            }else{
                HKEY hkey;
                if (info.dont_ask_again) {
                    if (RegCreateKey(HKEY_CURRENT_USER, "Software\\SimonTatham\\PuTTY\\" APPNAME, &hkey) == ERROR_SUCCESS) {
                        RegSetValueEx(hkey, keyname, 0, REG_SZ, (LPBYTE) (accept ? VALUE_ACCEPT : VALUE_REFUSE), accept ? sizeof VALUE_ACCEPT : sizeof VALUE_REFUSE);
                        RegCloseKey(hkey);
                    }
                }else{
                    if (RegOpenKey(HKEY_CURRENT_USER, "Software\\SimonTatham\\PuTTY\\" APPNAME, &hkey) == ERROR_SUCCESS) {
                        RegDeleteValue(hkey, keyname);
                        RegCloseKey(hkey);
                    }
                }
            }
        }
        return accept;
    }
}

void notify_agent_make_op(int type)
{
    if (reencrypt_settings.idle_ms) {
        SetTimer(traywindow, TID_REENCRYPT_IDLE, reencrypt_settings.idle_ms, NULL);
    }
}

static void defer_reencrypt(bool flag)
{
    if (flag) {
        defer_reencrypt_state++;
    } else {
        assert(defer_reencrypt_state);
        if (!--defer_reencrypt_state && deferred_reencrypt) {
            deferred_reencrypt = false;
            SetTimer(traywindow, TID_DEFERRED_REENCRYPT, 3000, NULL);
        }
    }
}

int accept_agent_request(int type, const RSAKey *rsaKey, const ssh2_userkey *ssh2UserKey)
{
    int result;
    defer_reencrypt(1);
    result = do_accept_agent_request(type, rsaKey, ssh2UserKey);
    defer_reencrypt(0);
    return result;
}

#if _WIN32_WINNT < 0x0602 /* not exactly sure from which version it is defined */
const GUID GUID_SESSION_DISPLAY_STATUS = { 0x2b84c20e, 0xad23, 0x4ddf,{ 0x93, 0xdb, 0x5, 0xff, 0xbd, 0x7e, 0xfc, 0xa5 } };
#endif

static char *parse_reencrypt_settings(const char *_cfg)
{
    char *error = NULL;
    char *cfg = dupstr(_cfg);
    char *p = cfg;
    while (*p) {
        char *pTail = strchr(p, ',');
        bool negate = *p == '-';
        char *pFlag = negate ? p + 1 : p;
        if (pTail) {
            *pTail = 0;
        }
        if (!*p) {
            /* empty */
        } else if (!strcmp(p, "any_event")) {
            reencrypt_settings.on_suspend =
                reencrypt_settings.on_display_off =
                reencrypt_settings.on_away =
                reencrypt_settings.on_lid_close = true;
        } else if (!strcmp(pFlag, "suspend")) {
            reencrypt_settings.on_suspend = !negate;
        } else if (!strcmp(pFlag, "display_off")) {
            reencrypt_settings.on_display_off = !negate;
        } else if (!strcmp(pFlag, "away")) {
            reencrypt_settings.on_away = !negate;
        } else if (!strcmp(pFlag, "lid_close")) {
            reencrypt_settings.on_lid_close = !negate;
        } else {
            int *dest = NULL;
            char *pVal, *pError;
            double value;
            if (!strncmp(p, "idle:", 5)) {
                dest = &reencrypt_settings.idle_ms;
                pVal = p + 5;
            } else if (!strncmp(p, "timeout:", 8)) {
                dest = &reencrypt_settings.timeout_ms;
                pVal = p + 8;
            } else {
                error = dupprintf("unrecognised re-encrypt option\n'%s'", p);
                goto cleanup;
            }
            value = strtod(pVal, &pError);
            if (pError != NULL) {
                if (pVal == pError) {
                    value = 0;
                } else if (!strcmp(pError, "s")) {
                    value /= 60;
                } else if (!strcmp(pError, "m")) {
                    /* nothing */
                } else if (!strcmp(pError, "h")) {
                    value *= 60;
                } else if (!strcmp(pError, "d")) {
                    value *= 60 * 24;
                } else {
                    value = 0;
                }
            }
            if (value >= 1 && value <= 14400) {
                *dest = value * 60 * 1000;
            } else {
                error = dupprintf("re-encrypt time in minutes must be between %d and %d\n'%s'", 1, 14400, p);
                goto cleanup;
            }
        }
        if (!pTail) {
            break;
        }
        p = pTail + 1;
    }
    cleanup:
    sfree(cfg);
    return error;
}

static void register_power_notifications(void)
{
    struct {
        bool enabled;
        const GUID *guid;
    } subscribers[] = {
        { reencrypt_settings.on_display_off, &GUID_CONSOLE_DISPLAY_STATE },
        { reencrypt_settings.on_away, &GUID_SYSTEM_AWAYMODE },
        { reencrypt_settings.on_lid_close, &GUID_LIDSWITCH_STATE_CHANGE },
    };
    bool someFailed = false;
    for (size_t i = 0; i < lenof(subscribers); i++) {
        if (subscribers[i].enabled) {
            HPOWERNOTIFY h = RegisterPowerSettingNotification(traywindow, subscribers[i].guid, DEVICE_NOTIFY_WINDOW_HANDLE);
            if (h == NULL) {
                someFailed = true;
            }
        }
    }
    if (someFailed) {
        MessageBox(NULL, "some of the reencrypt event will not be notified from Windows.",
            "Pageant error", MB_ICONERROR | MB_OK);
    }
}

/*
 * Dialog-box function for the key list box.
 */
static INT_PTR CALLBACK KeyListProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam)
{
    static const struct {
        const char *name;
        FingerprintType value;
    } fptypes[] = {
        {"SHA256", SSH_FPTYPE_SHA256},
        {"MD5", SSH_FPTYPE_MD5},
    };

    switch (msg) {
      case WM_INITDIALOG: {
        /*
         * Centre the window.
         */
        RECT rs, rd;
        HWND hw;

        hw = GetDesktopWindow();
        if (GetWindowRect(hw, &rs) && GetWindowRect(hwnd, &rd))
            MoveWindow(hwnd,
                       (rs.right + rs.left + rd.left - rd.right) / 2,
                       (rs.bottom + rs.top + rd.top - rd.bottom) / 2,
                       rd.right - rd.left, rd.bottom - rd.top, true);

        if (has_help())
            SetWindowLongPtr(hwnd, GWL_EXSTYLE,
                             GetWindowLongPtr(hwnd, GWL_EXSTYLE) |
                             WS_EX_CONTEXTHELP);
        else {
          HWND item = GetDlgItem(hwnd, IDC_KEYLIST_HELP);
          if (item)
              DestroyWindow(item);
        }

        keylist = hwnd;
        {
          static int tabs[] = { 35, 75, 300 };
          SendDlgItemMessage(hwnd, IDC_KEYLIST_LISTBOX, LB_SETTABSTOPS,
                             sizeof(tabs) / sizeof(*tabs),
                             (LPARAM) tabs);
        }

        int selection = 0;
        for (size_t i = 0; i < lenof(fptypes); i++) {
            SendDlgItemMessage(hwnd, IDC_KEYLIST_FPTYPE, CB_ADDSTRING,
                               0, (LPARAM)fptypes[i].name);
            if (fptype == fptypes[i].value)
                selection = (int)i;
        }
        SendDlgItemMessage(hwnd, IDC_KEYLIST_FPTYPE,
                           CB_SETCURSEL, 0, selection);

        keylist_update();
        return 0;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
          case IDCANCEL:
            keylist = NULL;
            DestroyWindow(hwnd);
            return 0;
          case IDC_KEYLIST_ADDKEY:
          case IDC_KEYLIST_ADDKEY_ENC:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED) {
                if (modal_passphrase_hwnd) {
                    MessageBeep(MB_ICONERROR);
                    SetForegroundWindow(modal_passphrase_hwnd);
                    break;
                }
                prompt_add_keyfile(LOWORD(wParam) == IDC_KEYLIST_ADDKEY_ENC);
            }
            return 0;
          case IDC_KEYLIST_REMOVE:
          case IDC_KEYLIST_REENCRYPT:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED) {
                int i;
                int rCount, sCount;
                int *selectedArray;

                /* our counter within the array of selected items */
                int itemNum;

                /* get the number of items selected in the list */
                int numSelected = SendDlgItemMessage(
                    hwnd, IDC_KEYLIST_LISTBOX, LB_GETSELCOUNT, 0, 0);

                /* none selected? that was silly */
                if (numSelected == 0) {
                    MessageBeep(0);
                    break;
                }

                /* get item indices in an array */
                selectedArray = snewn(numSelected, int);
                SendDlgItemMessage(hwnd, IDC_KEYLIST_LISTBOX, LB_GETSELITEMS,
                                   numSelected, (WPARAM)selectedArray);

                itemNum = numSelected - 1;
                rCount = pageant_count_ssh1_keys();
                sCount = pageant_count_ssh2_keys();

                /* go through the non-rsakeys until we've covered them all,
                 * and/or we're out of selected items to check. note that
                 * we go *backwards*, to avoid complications from deleting
                 * things hence altering the offset of subsequent items
                 */
                for (i = sCount - 1; (itemNum >= 0) && (i >= 0); i--) {
                    if (selectedArray[itemNum] == rCount + i) {
                        switch (LOWORD(wParam)) {
                          case IDC_KEYLIST_REMOVE:
                            pageant_delete_nth_ssh2_key(i);
                            break;
                          case IDC_KEYLIST_REENCRYPT:
                            pageant_reencrypt_nth_ssh2_key(i);
                            break;
                        }
                        itemNum--;
                    }
                }

                /* do the same for the rsa keys */
                for (i = rCount - 1; (itemNum >= 0) && (i >= 0); i--) {
                    if(selectedArray[itemNum] == i) {
                        switch (LOWORD(wParam)) {
                          case IDC_KEYLIST_REMOVE:
                            pageant_delete_nth_ssh1_key(i);
                            break;
                          case IDC_KEYLIST_REENCRYPT:
                            /* SSH-1 keys can't be re-encrypted */
                            break;
                        }
                        itemNum--;
                    }
                }

                sfree(selectedArray);
                keylist_update();
            }
            return 0;
          case IDC_KEYLIST_HELP:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED) {
                launch_help(hwnd, WINHELP_CTX_pageant_general);
            }
            return 0;
          case IDC_KEYLIST_FPTYPE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int selection = SendDlgItemMessage(
                    hwnd, IDC_KEYLIST_FPTYPE, CB_GETCURSEL, 0, 0);
                if (selection >= 0 && (size_t)selection < lenof(fptypes)) {
                    fptype = fptypes[selection].value;
                    keylist_update();
                }
            }
            return 0;
        }
        return 0;
      case WM_HELP: {
        int id = ((LPHELPINFO)lParam)->iCtrlId;
        const char *topic = NULL;
        switch (id) {
          case IDC_KEYLIST_LISTBOX:
          case IDC_KEYLIST_FPTYPE:
          case IDC_KEYLIST_FPTYPE_STATIC:
            topic = WINHELP_CTX_pageant_keylist; break;
          case IDC_KEYLIST_ADDKEY: topic = WINHELP_CTX_pageant_addkey; break;
          case IDC_KEYLIST_REMOVE: topic = WINHELP_CTX_pageant_remkey; break;
          case IDC_KEYLIST_ADDKEY_ENC:
          case IDC_KEYLIST_REENCRYPT:
            topic = WINHELP_CTX_pageant_deferred; break;
        }
        if (topic) {
          launch_help(hwnd, topic);
        } else {
          MessageBeep(0);
        }
        break;
      }
      case WM_CLOSE:
        keylist = NULL;
        DestroyWindow(hwnd);
        return 0;
    }
    return 0;
}

/* Set up a system tray icon */
static BOOL AddTrayIcon(HWND hwnd)
{
    BOOL res;
    NOTIFYICONDATA tnid;
    HICON hicon;

#ifdef NIM_SETVERSION
    tnid.uVersion = 0;
    res = Shell_NotifyIcon(NIM_SETVERSION, &tnid);
#endif

    tnid.cbSize = sizeof(NOTIFYICONDATA);
    tnid.hWnd = hwnd;
    tnid.uID = 1;              /* unique within this systray use */
    tnid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tnid.uCallbackMessage = WM_SYSTRAY;
    tnid.hIcon = hicon = LoadIcon(hinst, MAKEINTRESOURCE(201));
    strcpy(tnid.szTip, "Pageant (PuTTY authentication agent)");

    res = Shell_NotifyIcon(NIM_ADD, &tnid);
    if (!res)
        SetTimer(hwnd, TID_ADDSYSTRAY, 1000, NULL);

    if (hicon) DestroyIcon(hicon);

    return res;
}

/* Update the saved-sessions menu. */
static void update_sessions(void)
{
    int num_entries;
    HKEY hkey;
    TCHAR buf[MAX_PATH + 1];
    MENUITEMINFO mii;
    strbuf *sb;

    int index_key, index_menu;

    if (!putty_path)
        return;

    if (get_use_inifile()) {
        char* buffer;
        int length = 256;
        char* p;
        while (1) {
            buffer = snewn(length, char);
            if (GetPrivateProfileSectionNames(buffer, length, inifile) < (DWORD) length - 2)
                break;
            sfree(buffer);
            length += 256;
        }
        p = buffer;

        for (num_entries = GetMenuItemCount(session_menu);
            num_entries > initial_menuitems_count;
            num_entries--)
            RemoveMenu(session_menu, 0, MF_BYPOSITION);

        index_menu = 0;
        sb = strbuf_new();
        while (*p != '\0') {
            if (!strncmp(p, HEADER, sizeof(HEADER) - 1)) {
                if (strcmp(buf, PUTTY_DEFAULT) != 0) {
                    strbuf_clear(sb);
                    unescape_registry_key(p + sizeof(HEADER) - 1, sb);
                    memset(&mii, 0, sizeof(mii));
                    mii.cbSize = sizeof(mii);
                    mii.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
                    mii.fType = MFT_STRING;
                    mii.fState = MFS_ENABLED;
                    mii.wID = (index_menu * 16) + IDM_SESSIONS_BASE;
                    mii.dwTypeData = sb->s;
                    InsertMenuItem(session_menu, index_menu, TRUE, &mii);
                    index_menu++;
                }
            }
            p += strlen(p) + 1;
        }
        strbuf_free(sb);
        sfree(buffer);

        if(index_menu == 0) {
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_TYPE | MIIM_STATE;
            mii.fType = MFT_STRING;
            mii.fState = MFS_GRAYED;
            mii.dwTypeData = _T("(No sessions)");
            InsertMenuItem(session_menu, index_menu, TRUE, &mii);
        }
        return;
    }

    if(ERROR_SUCCESS != RegOpenKey(HKEY_CURRENT_USER, PUTTY_REGKEY, &hkey))
        return;

    for(num_entries = GetMenuItemCount(session_menu);
        num_entries > initial_menuitems_count;
        num_entries--)
        RemoveMenu(session_menu, 0, MF_BYPOSITION);

    index_key = 0;
    index_menu = 0;

    sb = strbuf_new();
    while(ERROR_SUCCESS == RegEnumKey(hkey, index_key, buf, MAX_PATH)) {
        if(strcmp(buf, PUTTY_DEFAULT) != 0) {
            strbuf_clear(sb);
            unescape_registry_key(buf, sb);

            memset(&mii, 0, sizeof(mii));
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
            mii.fType = MFT_STRING;
            mii.fState = MFS_ENABLED;
            mii.wID = (index_menu * 16) + IDM_SESSIONS_BASE;
            mii.dwTypeData = sb->s;
            InsertMenuItem(session_menu, index_menu, true, &mii);
            index_menu++;
        }
        index_key++;
    }
    strbuf_free(sb);

    RegCloseKey(hkey);

    if(index_menu == 0) {
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_TYPE | MIIM_STATE;
        mii.fType = MFT_STRING;
        mii.fState = MFS_GRAYED;
        mii.dwTypeData = _T("(No sessions)");
        InsertMenuItem(session_menu, index_menu, true, &mii);
    }
}

#ifndef NO_SECURITY
/*
 * Versions of Pageant prior to 0.61 expected this SID on incoming
 * communications. For backwards compatibility, and more particularly
 * for compatibility with derived works of PuTTY still using the old
 * Pageant client code, we accept it as an alternative to the one
 * returned from get_user_sid() in winpgntc.c.
 */
PSID get_default_sid(void)
{
    HANDLE proc = NULL;
    DWORD sidlen;
    PSECURITY_DESCRIPTOR psd = NULL;
    PSID sid = NULL, copy = NULL, ret = NULL;

    if ((proc = OpenProcess(MAXIMUM_ALLOWED, false,
                            GetCurrentProcessId())) == NULL)
        goto cleanup;

    if (p_GetSecurityInfo(proc, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
                          &sid, NULL, NULL, NULL, &psd) != ERROR_SUCCESS)
        goto cleanup;

    sidlen = GetLengthSid(sid);

    copy = (PSID)smalloc(sidlen);

    if (!CopySid(sidlen, copy, sid))
        goto cleanup;

    /* Success. Move sid into the return value slot, and null it out
     * to stop the cleanup code freeing it. */
    ret = copy;
    copy = NULL;

  cleanup:
    if (proc != NULL)
        CloseHandle(proc);
    if (psd != NULL)
        LocalFree(psd);
    if (copy != NULL)
        sfree(copy);

    return ret;
}
#endif

struct WmCopydataTransaction {
    char *length, *body;
    size_t bodysize, bodylen;
    HANDLE ev_msg_ready, ev_reply_ready;
} wmct;

static struct PageantClient wmcpc;

static void wm_copydata_got_msg(void *vctx)
{
    pageant_handle_msg(&wmcpc, NULL, make_ptrlen(wmct.body, wmct.bodylen));
}

static void wm_copydata_got_response(
    PageantClient *pc, PageantClientRequestId *reqid, ptrlen response)
{
    if (response.len > wmct.bodysize) {
        /* Output would overflow message buffer. Replace with a
         * failure message. */
        static const unsigned char failure[] = { SSH_AGENT_FAILURE };
        response = make_ptrlen(failure, lenof(failure));
        assert(response.len <= wmct.bodysize);
    }

    PUT_32BIT_MSB_FIRST(wmct.length, response.len);
    memcpy(wmct.body, response.ptr, response.len);

    SetEvent(wmct.ev_reply_ready);
}

static bool ask_passphrase_common(PageantClientDialogId *dlgid,
                                  const char *comment)
{
    /* Pageant core should be serialising requests, so we never expect
     * a passphrase prompt to exist already at this point */
    assert(!nonmodal_passphrase_hwnd);

    struct PassphraseProcStruct *pps = snew(struct PassphraseProcStruct);
    pps->modal = false;
    pps->help_topic = WINHELP_CTX_pageant_deferred;
    pps->dlgid = dlgid;
    pps->passphrase = NULL;
    pps->comment = comment;

    nonmodal_passphrase_hwnd = CreateDialogParam(
        hinst, MAKEINTRESOURCE(IDD_ONDEMAND_PASSPHRASE),
        NULL, PassphraseProc, (LPARAM)pps);

    /*
     * Try to put this passphrase prompt into the foreground.
     *
     * This will probably not succeed in giving it the actual keyboard
     * focus, because Windows is quite opposed to applications being
     * able to suddenly steal the focus on their own initiative.
     *
     * That makes sense in a lot of situations, as a defensive
     * measure. If you were about to type a password or other secret
     * data into the window you already had focused, and some
     * malicious app stole the focus, it might manage to trick you
     * into typing your secrets into _it_ instead.
     *
     * In this case it's possible to regard the same defensive measure
     * as counterproductive, because the effect if we _do_ steal focus
     * is that you type something into our passphrase prompt that
     * isn't the passphrase, and we fail to decrypt the key, and no
     * harm is done. Whereas the effect of the user wrongly _assuming_
     * the new passphrase prompt has the focus is much worse: now you
     * type your highly secret passphrase into some other window you
     * didn't mean to trust with that information - such as the
     * agent-forwarded PuTTY in which you just ran an ssh command,
     * which the _whole point_ was to avoid telling your passphrase to!
     *
     * On the other hand, I'm sure _every_ application author can come
     * up with an argument for why they think _they_ should be allowed
     * to steal the focus. Probably most of them include the claim
     * that no harm is done if their application receives data
     * intended for something else, and of course that's not always
     * true!
     *
     * In any case, I don't know of anything I can do about it, or
     * anything I _should_ do about it if I could. If anyone thinks
     * they can improve on all this, patches are welcome.
     */
    SetForegroundWindow(nonmodal_passphrase_hwnd);

    return true;
}

static bool wm_copydata_ask_passphrase(
    PageantClient *pc, PageantClientDialogId *dlgid, const char *comment)
{
    return ask_passphrase_common(dlgid, comment);
}

static const PageantClientVtable wmcpc_vtable = {
    .log = NULL, /* no logging in this client */
    .got_response = wm_copydata_got_response,
    .ask_passphrase = wm_copydata_ask_passphrase,
};

static char *answer_filemapping_message(const char *mapname)
{
    HANDLE maphandle = INVALID_HANDLE_VALUE;
    void *mapaddr = NULL;
    char *err = NULL;
    size_t mapsize;
    unsigned msglen;

#ifndef NO_SECURITY
    PSID mapsid = NULL;
    PSID expectedsid = NULL;
    PSID expectedsid_bc = NULL;
    PSECURITY_DESCRIPTOR psd = NULL;
#endif

    wmct.length = wmct.body = NULL;

#ifdef DEBUG_IPC
    debug("mapname = \"%s\"\n", mapname);
#endif

    maphandle = OpenFileMapping(FILE_MAP_ALL_ACCESS, false, mapname);
    if (maphandle == NULL || maphandle == INVALID_HANDLE_VALUE) {
        err = dupprintf("OpenFileMapping(\"%s\"): %s",
                        mapname, win_strerror(GetLastError()));
        goto cleanup;
    }

#ifdef DEBUG_IPC
    debug("maphandle = %p\n", maphandle);
#endif

#ifndef NO_SECURITY
    if (has_security) {
        DWORD retd;

        if ((expectedsid = get_user_sid()) == NULL) {
            err = dupstr("unable to get user SID");
            goto cleanup;
        }

        if ((expectedsid_bc = get_default_sid()) == NULL) {
            err = dupstr("unable to get default SID");
            goto cleanup;
        }

        if ((retd = p_GetSecurityInfo(
                 maphandle, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
                 &mapsid, NULL, NULL, NULL, &psd) != ERROR_SUCCESS)) {
            err = dupprintf("unable to get owner of file mapping: "
                            "GetSecurityInfo returned: %s",
                            win_strerror(retd));
            goto cleanup;
        }

#ifdef DEBUG_IPC
        {
            LPTSTR ours, ours2, theirs;
            ConvertSidToStringSid(mapsid, &theirs);
            ConvertSidToStringSid(expectedsid, &ours);
            ConvertSidToStringSid(expectedsid_bc, &ours2);
            debug("got sids:\n  oursnew=%s\n  oursold=%s\n"
                  "  theirs=%s\n", ours, ours2, theirs);
            LocalFree(ours);
            LocalFree(ours2);
            LocalFree(theirs);
        }
#endif

        if (!EqualSid(mapsid, expectedsid) &&
            !EqualSid(mapsid, expectedsid_bc)) {
            err = dupstr("wrong owning SID of file mapping");
            goto cleanup;
        }
    } else
#endif /* NO_SECURITY */
    {
#ifdef DEBUG_IPC
        debug("security APIs not present\n");
#endif
    }

    mapaddr = MapViewOfFile(maphandle, FILE_MAP_WRITE, 0, 0, 0);
    if (!mapaddr) {
        err = dupprintf("unable to obtain view of file mapping: %s",
                        win_strerror(GetLastError()));
        goto cleanup;
    }

#ifdef DEBUG_IPC
    debug("mapped address = %p\n", mapaddr);
#endif

    {
        MEMORY_BASIC_INFORMATION mbi;
        size_t mbiSize = VirtualQuery(mapaddr, &mbi, sizeof(mbi));
        if (mbiSize == 0) {
            err = dupprintf("unable to query view of file mapping: %s",
                            win_strerror(GetLastError()));
            goto cleanup;
        }
        if (mbiSize < (offsetof(MEMORY_BASIC_INFORMATION, RegionSize) +
                       sizeof(mbi.RegionSize))) {
            err = dupstr("VirtualQuery returned too little data to get "
                         "region size");
            goto cleanup;
        }

        mapsize = mbi.RegionSize;
    }
#ifdef DEBUG_IPC
    debug("region size = %"SIZEu"\n", mapsize);
#endif
    if (mapsize < 5) {
        err = dupstr("mapping smaller than smallest possible request");
        goto cleanup;
    }

    wmct.length = (char *)mapaddr;
    msglen = GET_32BIT_MSB_FIRST(wmct.length);

#ifdef DEBUG_IPC
    debug("msg length=%08x, msg type=%02x\n",
          msglen, (unsigned)((unsigned char *) mapaddr)[4]);
#endif

    wmct.body = wmct.length + 4;
    wmct.bodysize = mapsize - 4;

    if (msglen > wmct.bodysize) {
        /* Incoming length field is too large. Emit a failure response
         * without even trying to handle the request.
         *
         * (We know this must fit, because we checked mapsize >= 5
         * above.) */
        PUT_32BIT_MSB_FIRST(wmct.length, 1);
        *wmct.body = SSH_AGENT_FAILURE;
    } else {
        wmct.bodylen = msglen;
        SetEvent(wmct.ev_msg_ready);
        WaitForSingleObject(wmct.ev_reply_ready, INFINITE);
    }

  cleanup:
    /* expectedsid has the lifetime of the program, so we don't free it */
    sfree(expectedsid_bc);
    if (psd)
        LocalFree(psd);
    if (mapaddr)
        UnmapViewOfFile(mapaddr);
    if (maphandle != NULL && maphandle != INVALID_HANDLE_VALUE)
        CloseHandle(maphandle);
    return err;
}

static void create_keylist_window(void)
{
    if (keylist)
        return;

    keylist = CreateDialog(hinst, MAKEINTRESOURCE(IDD_KEYLIST),
                           NULL, KeyListProc);
    ShowWindow(keylist, SW_SHOWNORMAL);
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT message,
                                    WPARAM wParam, LPARAM lParam)
{
    static bool menuinprogress;
    static UINT msgTaskbarCreated = 0;

    switch (message) {
      case WM_CREATE:
        msgTaskbarCreated = RegisterWindowMessage(_T("TaskbarCreated"));
        break;
      default:
        if (message==msgTaskbarCreated) {
            /*
             * Explorer has been restarted, so the tray icon will
             * have been lost.
             */
            AddTrayIcon(hwnd);
        }
        break;

      case WM_SYSTRAY:
        if (lParam == WM_RBUTTONUP) {
            POINT cursorpos;
            GetCursorPos(&cursorpos);
            PostMessage(hwnd, WM_SYSTRAY2, cursorpos.x, cursorpos.y);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            /* Run the default menu item. */
            UINT menuitem = GetMenuDefaultItem(systray_menu, false, 0);
            if (menuitem != -1)
                PostMessage(hwnd, WM_COMMAND, menuitem, 0);
        }
        break;
      case WM_SYSTRAY2:
        if (!menuinprogress) {
            menuinprogress = true;
            update_sessions();
            SetForegroundWindow(hwnd);
            TrackPopupMenu(systray_menu,
                           TPM_RIGHTALIGN | TPM_BOTTOMALIGN |
                           TPM_RIGHTBUTTON,
                           wParam, lParam, 0, hwnd, NULL);
            menuinprogress = false;
        }
        break;
      case WM_TIMER:
          switch (wParam) {
          case TID_ADDSYSTRAY:
              KillTimer(hwnd, TID_ADDSYSTRAY);
              AddTrayIcon(hwnd);
              break;
          case TID_DEFERRED_REENCRYPT:
          case TID_REENCRYPT_IDLE:
          case TID_REENCRYPT_TIMEOUT:
              KillTimer(hwnd, wParam);
              gui_reencrypt_all();
              break;
          }
          break;
      case WM_COMMAND:
      case WM_SYSCOMMAND: {
        unsigned command = wParam & ~0xF; /* low 4 bits reserved to Windows */
        switch (command) {
          case IDM_PUTTY: {
            TCHAR cmdline[10];
            cmdline[0] = '\0';
            if (restrict_putty_acl)
                strcat(cmdline, "&R");

            if((INT_PTR)ShellExecute(hwnd, NULL, putty_path, cmdline,
                                     _T(""), SW_SHOW) <= 32) {
              MessageBox(hwnd, "Unable to execute PuTTY!",
                         "Error", MB_OK | MB_ICONERROR);
            }
            break;
          }
          case IDM_CLOSE:
            if (modal_passphrase_hwnd)
                SendMessage(modal_passphrase_hwnd, WM_CLOSE, 0, 0);
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            break;
          case IDM_VIEWKEYS:
            create_keylist_window();
            /*
             * Sometimes the window comes up minimised / hidden for
             * no obvious reason. Prevent this. This also brings it
             * to the front if it's already present (the user
             * selected View Keys because they wanted to _see_ the
             * thing).
             */
            SetForegroundWindow(keylist);
            SetWindowPos(keylist, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            break;
          case IDM_ADDKEY:
          case IDM_ADDKEY_ENCRYPTED:
            if (modal_passphrase_hwnd) {
                MessageBeep(MB_ICONERROR);
                SetForegroundWindow(modal_passphrase_hwnd);
                break;
            }
            prompt_add_keyfile(command == IDM_ADDKEY_ENCRYPTED);
            break;
          case IDM_REMOVE_ALL:
            pageant_delete_all();
            keylist_update();
            break;
          case IDM_REENCRYPT_ALL:
            gui_reencrypt_all();
            break;
          case IDM_ABOUT:
            if (!aboutbox) {
                aboutbox = CreateDialog(hinst, MAKEINTRESOURCE(IDD_ABOUT),
                                        hwnd, AboutProc);
                ShowWindow(aboutbox, SW_SHOWNORMAL);
                /*
                 * Sometimes the window comes up minimised / hidden
                 * for no obvious reason. Prevent this.
                 */
                SetForegroundWindow(aboutbox);
                SetWindowPos(aboutbox, HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            }
            break;
          case IDM_HELP:
            launch_help(hwnd, WINHELP_CTX_pageant_general);
            break;
          case IDM_CONFIRM_REQUEST:
            {
                MENUITEMINFO mii = {sizeof mii, MIIM_STATE};
                confirm_any_request = !confirm_any_request;
                mii.fState = confirm_any_request ? MFS_CHECKED : MFS_UNCHECKED;
                SetMenuItemInfo(systray_menu, IDM_CONFIRM_REQUEST, FALSE, &mii);
            }
            break;
          default: {
            if(wParam >= IDM_SESSIONS_BASE && wParam <= IDM_SESSIONS_MAX) {
              MENUITEMINFO mii;
              TCHAR buf[MAX_PATH + 1];
              TCHAR param[MAX_PATH + 1];
              memset(&mii, 0, sizeof(mii));
              mii.cbSize = sizeof(mii);
              mii.fMask = MIIM_TYPE;
              mii.cch = MAX_PATH;
              mii.dwTypeData = buf;
              GetMenuItemInfo(session_menu, wParam, false, &mii);
              param[0] = '\0';
              if (restrict_putty_acl)
                  strcat(param, "&R");
              strcat(param, "@");
              strcat(param, mii.dwTypeData);
              if((INT_PTR)ShellExecute(hwnd, NULL, putty_path, param,
                                       _T(""), SW_SHOW) <= 32) {
                MessageBox(hwnd, "Unable to execute PuTTY!", "Error",
                           MB_OK | MB_ICONERROR);
              }
            }
            break;
          }
        }
        break;
      }
      case WM_DESTROY:
        quit_help(hwnd);
        PostQuitMessage(0);
        return 0;
      case WM_POWERBROADCAST:
          switch (wParam) {
          case PBT_APMSUSPEND:
              if (lParam == 0) { /* reserved must be 0 */
                  if (reencrypt_settings.on_suspend) {
                      gui_reencrypt_all();
                  }
                  return true;
              }
              break;
          case PBT_POWERSETTINGCHANGE: {
              const POWERBROADCAST_SETTING *pbcSetting = (const POWERBROADCAST_SETTING *)lParam;
              if (IsEqualGUID(&pbcSetting->PowerSetting, &GUID_CONSOLE_DISPLAY_STATE)) {
                  DWORD data = *(const DWORD *)pbcSetting->Data;
                  if (data == 0 && reencrypt_settings.on_display_off) { /* display is off */
                      gui_reencrypt_all();
                  }
                  return true;
              } else if (IsEqualGUID(&pbcSetting->PowerSetting, &GUID_SYSTEM_AWAYMODE)) {
                  DWORD data = *(const DWORD *)pbcSetting->Data;
                  if (data == 1 && reencrypt_settings.on_away) { /* entering away mode */
                      gui_reencrypt_all();
                  }
                  return true;
              } else if (IsEqualGUID(&pbcSetting->PowerSetting, &GUID_LIDSWITCH_STATE_CHANGE)) {
                  ULONG data = *(const ULONG *)pbcSetting->Data;
                  if (data == 0 && reencrypt_settings.on_lid_close) { /* lid is closed */
                      gui_reencrypt_all();
                  }
                  return true;
              }
              break;
          }
        }
        break;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

static LRESULT CALLBACK wm_copydata_WndProc(HWND hwnd, UINT message,
                                            WPARAM wParam, LPARAM lParam)
{
    switch (message) {
      case WM_COPYDATA: {
        COPYDATASTRUCT *cds;
        char *mapname, *err;

        cds = (COPYDATASTRUCT *) lParam;
        if (cds->dwData != AGENT_COPYDATA_ID)
            return 0;              /* not our message, mate */
        mapname = (char *) cds->lpData;
        if (mapname[cds->cbData - 1] != '\0')
            return 0;              /* failure to be ASCIZ! */
        err = answer_filemapping_message(mapname);
        if (err) {
#ifdef DEBUG_IPC
          debug("IPC failed: %s\n", err);
#endif
          sfree(err);
          return 0;
        }
        return 1;
      }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

static DWORD WINAPI wm_copydata_threadfunc(void *param)
{
    HINSTANCE inst = *(HINSTANCE *)param;

    HWND ipchwnd = CreateWindow(IPCCLASSNAME, IPCWINTITLE,
                                WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                100, 100, NULL, NULL, inst, NULL);
    ShowWindow(ipchwnd, SW_HIDE);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) == 1) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

/*
 * Fork and Exec the command in cmdline. [DBW]
 */
void spawn_cmd(const char *cmdline, const char *args, int show)
{
    if (ShellExecute(NULL, _T("open"), cmdline,
                     args, NULL, show) <= (HINSTANCE) 32) {
        char *msg;
        msg = dupprintf("Failed to run \"%s\": %s", cmdline,
                        win_strerror(GetLastError()));
        MessageBox(traywindow, msg, APPNAME, MB_OK | MB_ICONEXCLAMATION);
        sfree(msg);
    }
}

void logevent(LogContext *logctx, const char *event)
{
    unreachable("Pageant can't create a LogContext, so this can't be called");
}

void noise_ultralight(NoiseSourceId id, unsigned long data)
{
    /* Pageant doesn't use random numbers, so we ignore this */
}

void cleanup_exit(int code)
{
    shutdown_help();
    exit(code);
}

static bool winpgnt_listener_ask_passphrase(
    PageantListenerClient *plc, PageantClientDialogId *dlgid,
    const char *comment)
{
    return ask_passphrase_common(dlgid, comment);
}

struct winpgnt_client {
    PageantListenerClient plc;
};
static const PageantListenerClientVtable winpgnt_vtable = {
    .log = NULL, /* no logging */
    .ask_passphrase = winpgnt_listener_ask_passphrase,
};

static struct winpgnt_client wpc[1];

HINSTANCE hinst;

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    MSG msg;
    const char *command = NULL;
    bool added_keys = false;
    bool show_keylist_on_startup = false;
    int argc, i;
    char **argv, **argstart;

    dll_hijacking_protection();

    hinst = inst;

    /*
     * Determine whether we're an NT system (should have security
     * APIs) or a non-NT system (don't do security).
     */
    init_winver();
    has_security = (osPlatformId == VER_PLATFORM_WIN32_NT);

    if (has_security) {
#ifndef NO_SECURITY
        /*
         * Attempt to get the security API we need.
         */
        if (!got_advapi()) {
            MessageBox(NULL,
                       "Unable to access security APIs. Pageant will\n"
                       "not run, in case it causes a security breach.",
                       "Pageant Fatal Error", MB_ICONERROR | MB_OK);
            return 1;
        }
#else
        MessageBox(NULL,
                   "This program has been compiled for Win9X and will\n"
                   "not run on NT, in case it causes a security breach.",
                   "Pageant Fatal Error", MB_ICONERROR | MB_OK);
        return 1;
#endif
    }

    /*
     * See if we can find our Help file.
     */
    init_help();

    /*
     * Look for the PuTTY binary (we will enable the saved session
     * submenu if we find it).
     */
    {
        char b[2048], *p, *q, *r;
        FILE *fp;
        GetModuleFileName(NULL, b, sizeof(b) - 16);
        r = b;
        p = strrchr(b, '\\');
        if (p && p >= r) r = p+1;
        q = strrchr(b, ':');
        if (q && q >= r) r = q+1;
        strcpy(r, "putty.exe");
        if ( (fp = fopen(b, "r")) != NULL) {
            putty_path = dupstr(b);
            fclose(fp);
        } else
            putty_path = NULL;
    }

    /*
     * Find out if Pageant is already running.
     */
    already_running = agent_exists();

    /*
     * Initialise the cross-platform Pageant code.
     */
    if (!already_running) {
        pageant_init();
    }

    /*
     * Process the command line and add keys as listed on it.
     */
    split_into_argv(cmdline, &argc, &argv, &argstart);

    if (argc > 1 && !strcmp(argv[0], "-ini") && *(argv[1]) != '\0') {
        char* dummy;
        DWORD attributes;
        GetFullPathName(argv[1], sizeof inifile, inifile, &dummy);
        attributes = GetFileAttributes(inifile);
        if (attributes != 0xFFFFFFFF && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            HANDLE handle = CreateFile(inifile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
                use_inifile = 1;
                argc -= 2;
                argv += 2;
            }
        }
    }

    bool doing_opts = true;
    bool add_keys_encrypted = false;
    for (i = 0; i < argc; i++) {
        char *p = argv[i];
        if (*p == '-' && doing_opts) {
            if (!strcmp(p, "-pgpfp")) {
                pgp_fingerprints_msgbox(NULL);
                return 1;
            } else if (!strcmp(p, "-restrict-acl") ||
                       !strcmp(p, "-restrict_acl") ||
                       !strcmp(p, "-restrictacl")) {
                restrict_process_acl();
            } else if (!strcmp(p, "-restrict-putty-acl") ||
                       !strcmp(p, "-restrict_putty_acl")) {
                restrict_putty_acl = true;
            } else if (!strcmp(p, "--no-decrypt") ||
                       !strcmp(p, "-no-decrypt") ||
                       !strcmp(p, "--no_decrypt") ||
                       !strcmp(p, "-no_decrypt") ||
                       !strcmp(p, "--nodecrypt") ||
                       !strcmp(p, "-nodecrypt") ||
                       !strcmp(p, "--encrypted") ||
                       !strcmp(p, "-encrypted")) {
                add_keys_encrypted = true;
            } else if (!strcmp(p, "-keylist") || !strcmp(p, "--keylist")) {
                show_keylist_on_startup = true;
            } else if (!strcmp(p, "-confirm")) {
                confirm_any_request = 1;
            } else if (!strncmp(p, "-reencrypt_on=", 14)) {
                char *error = parse_reencrypt_settings(p + 14);
                if (error) {
                    MessageBox(NULL, error, "Pageant command-line syntax error", MB_ICONERROR | MB_OK);
                    exit(1);
                }
            } else if (!strcmp(p, "-c")) {
                /*
                 * If we see `-c', then the rest of the
                 * command line should be treated as a
                 * command to be spawned.
                 */
                if (i < argc-1)
                    command = argstart[i+1];
                else
                    command = "";
                break;
            } else if (!strcmp(p, "--")) {
                doing_opts = false;
            } else {
                char *msg = dupprintf("unrecognised command-line option\n"
                                      "'%s'", p);
                MessageBox(NULL, msg, "Pageant command-line syntax error",
                           MB_ICONERROR | MB_OK);
                exit(1);
            }
        } else {
            Filename *fn = filename_from_str(p);
            win_add_keyfile(fn, add_keys_encrypted);
            filename_free(fn);
            added_keys = true;
        }
    }

    /*
     * Forget any passphrase that we retained while going over
     * command line keyfiles.
     */
    pageant_forget_passphrases();

    if (command) {
        char *args;
        if (command[0] == '"')
            args = strchr(++command, '"');
        else
            args = strchr(command, ' ');
        if (args) {
            *args++ = 0;
            while(*args && isspace(*args)) args++;
        }
        spawn_cmd(command, args, show);
    }

    /*
     * If Pageant was already running, we leave now. If we haven't
     * even taken any auxiliary action (spawned a command or added
     * keys), complain.
     */
    if (already_running) {
        if (!command && !added_keys) {
            MessageBox(NULL, "Pageant is already running", "Pageant Error",
                       MB_ICONERROR | MB_OK);
        }
        return 0;
    }

#if !defined NO_SECURITY

    /*
     * Set up a named-pipe listener.
     */
    {
        Plug *pl_plug;
        wpc->plc.vt = &winpgnt_vtable;
        wpc->plc.suppress_logging = true;
        struct pageant_listen_state *pl =
            pageant_listener_new(&pl_plug, &wpc->plc);
        char *pipename = agent_named_pipe_name();
        Socket *sock = new_named_pipe_listener(pipename, pl_plug);
        if (sk_socket_error(sock)) {
            char *err = dupprintf("Unable to open named pipe at %s "
                                  "for SSH agent:\n%s", pipename,
                                  sk_socket_error(sock));
            MessageBox(NULL, err, "Pageant Error", MB_ICONERROR | MB_OK);
            return 1;
        }
        pageant_listener_got_socket(pl, sock);
        sfree(pipename);
    }

#endif /* !defined NO_SECURITY */

    /*
     * Set up window classes for two hidden windows: one that receives
     * all the messages to do with our presence in the system tray,
     * and one that receives the WM_COPYDATA message used by the
     * old-style Pageant IPC system.
     */

    if (!prev) {
        WNDCLASS wndclass;

        memset(&wndclass, 0, sizeof(wndclass));
        wndclass.lpfnWndProc = TrayWndProc;
        wndclass.hInstance = inst;
        wndclass.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON));
        wndclass.lpszClassName = TRAYCLASSNAME;

        RegisterClass(&wndclass);

        memset(&wndclass, 0, sizeof(wndclass));
        wndclass.lpfnWndProc = wm_copydata_WndProc;
        wndclass.hInstance = inst;
        wndclass.lpszClassName = IPCCLASSNAME;

        RegisterClass(&wndclass);
    }

    keylist = NULL;

    traywindow = CreateWindow(TRAYCLASSNAME, TRAYWINTITLE,
                              WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              100, 100, NULL, NULL, inst, NULL);
    winselgui_set_hwnd(traywindow);
    register_power_notifications();

    /* Set up a system tray icon */
    AddTrayIcon(traywindow);

    /* Accelerators used: nsvkxa */
    systray_menu = CreatePopupMenu();
    if (putty_path) {
        session_menu = CreateMenu();
        AppendMenu(systray_menu, MF_ENABLED, IDM_PUTTY, "&New Session");
        AppendMenu(systray_menu, MF_POPUP | MF_ENABLED,
                   (UINT_PTR) session_menu, "&Saved Sessions");
        AppendMenu(systray_menu, MF_SEPARATOR, 0, 0);
    }
    AppendMenu(systray_menu, MF_ENABLED, IDM_VIEWKEYS,
           "&View Keys");
    AppendMenu(systray_menu, MF_ENABLED, IDM_ADDKEY, "Add &Key");
    AppendMenu(systray_menu, MF_ENABLED, IDM_ADDKEY_ENCRYPTED,
               "Add key (encrypted)");
    AppendMenu(systray_menu, MF_SEPARATOR, 0, 0);
    AppendMenu(systray_menu, MF_ENABLED, IDM_REMOVE_ALL,
               "Remove All Keys");
    AppendMenu(systray_menu, MF_ENABLED, IDM_REENCRYPT_ALL,
               "Re-encrypt All Keys");
    AppendMenu(systray_menu, MF_SEPARATOR, 0, 0);
    AppendMenu(systray_menu, confirm_any_request ? MF_CHECKED : MF_UNCHECKED, IDM_CONFIRM_REQUEST, "&Confirm Any Request");
    AppendMenu(systray_menu, MF_SEPARATOR, 0, 0);
    if (has_help())
        AppendMenu(systray_menu, MF_ENABLED, IDM_HELP, "&Help");
    AppendMenu(systray_menu, MF_ENABLED, IDM_ABOUT, "&About");
    AppendMenu(systray_menu, MF_SEPARATOR, 0, 0);
    AppendMenu(systray_menu, MF_ENABLED, IDM_CLOSE, "E&xit");
    initial_menuitems_count = GetMenuItemCount(session_menu);

    /* Set the default menu item. */
    SetMenuDefaultItem(systray_menu, IDM_VIEWKEYS, false);

    ShowWindow(traywindow, SW_HIDE);

    wmcpc.vt = &wmcpc_vtable;
    wmcpc.suppress_logging = true;
    pageant_register_client(&wmcpc);
    DWORD wm_copydata_threadid;
    wmct.ev_msg_ready = CreateEvent(NULL, false, false, NULL);
    wmct.ev_reply_ready = CreateEvent(NULL, false, false, NULL);
    CreateThread(NULL, 0, wm_copydata_threadfunc,
                 &inst, 0, &wm_copydata_threadid);
    handle_add_foreign_event(wmct.ev_msg_ready, wm_copydata_got_msg, NULL);

    if (show_keylist_on_startup)
        create_keylist_window();

    /*
     * Main message loop.
     */
    while (true) {
        HANDLE *handles;
        int nhandles, n;

        handles = handle_get_events(&nhandles);

        n = MsgWaitForMultipleObjects(nhandles, handles, false,
                                      INFINITE, QS_ALLINPUT);

        if ((unsigned)(n - WAIT_OBJECT_0) < (unsigned)nhandles) {
            handle_got_event(handles[n - WAIT_OBJECT_0]);
            sfree(handles);
        } else
            sfree(handles);

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                goto finished;         /* two-level break */

            if (IsWindow(keylist) && IsDialogMessage(keylist, &msg))
                continue;
            if (IsWindow(aboutbox) && IsDialogMessage(aboutbox, &msg))
                continue;
            if (IsWindow(nonmodal_passphrase_hwnd) &&
                IsDialogMessage(nonmodal_passphrase_hwnd, &msg))
                continue;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        run_toplevel_callbacks();
    }
  finished:

    /* Clean up the system tray icon */
    {
        NOTIFYICONDATA tnid;

        tnid.cbSize = sizeof(NOTIFYICONDATA);
        tnid.hWnd = traywindow;
        tnid.uID = 1;
        tnid.uFlags = 0;

        Shell_NotifyIcon(NIM_DELETE, &tnid);

        DestroyMenu(systray_menu);
    }

    if (keypath) filereq_free(keypath);

    cleanup_exit(msg.wParam);
    return msg.wParam;                 /* just in case optimiser complains */
}

#undef pageant_reencrypt_all
static void gui_reencrypt_all(void)
{
    if (defer_reencrypt_state) {
        deferred_reencrypt = true;
        return;
    }
    pageant_reencrypt_all();
    keylist_update();
}
