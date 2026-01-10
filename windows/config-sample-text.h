// PuTTYrv

typedef struct {
    dlgparam *dp;
    dlgcontrol *sample;
    HFONT sample_font;
} sample_text_wnd_data;

typedef struct {
    dlgcontrol c; // must be the first item to be released properly
    handler_fn orig_handler;
    dlgcontrol *sample;
} dlgcontrol_colour;

extern bool colour_control_override(dlgcontrol *listbox, dlgcontrol *target_ctrl, dlgcontrol *new_ctrl);
extern void dlg_invalidate(dlgcontrol *ctrl, dlgparam *dp, bool erase);

typedef LRESULT (CALLBACK *SUBCLASSPROC)(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
DECL_WINDOWS_FUNCTION(static, BOOL, SetWindowSubclass, (HWND hWnd, SUBCLASSPROC pfnSubclass, UINT_PTR uIdSubclass, DWORD_PTR dwRefData));
DECL_WINDOWS_FUNCTION(static, BOOL, RemoveWindowSubclass, (HWND hWnd, SUBCLASSPROC pfnSubclass, UINT_PTR uIdSubclass));
DECL_WINDOWS_FUNCTION(static, LRESULT, DefSubclassProc, (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam));

static LRESULT CALLBACK sample_text_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data);

static bool sample_text_init()
{
    static bool loaded;
    if (!loaded) {
        HANDLE comctl32 = load_system32_dll("comctl32.dll");
        GET_WINDOWS_FUNCTION_NO_TYPECHECK(comctl32, SetWindowSubclass);
        GET_WINDOWS_FUNCTION_NO_TYPECHECK(comctl32, RemoveWindowSubclass);
        GET_WINDOWS_FUNCTION_NO_TYPECHECK(comctl32, DefSubclassProc);
        loaded = true;
    }
    return p_SetWindowSubclass && p_RemoveWindowSubclass && p_DefSubclassProc;
}

static void sample_text_handler(dlgcontrol *ctrl, dlgparam *dp, void *data, int event)
{
    if (event != EVENT_CALLBACK) return;
    HWND hwnd = (HWND)data;
    RECT r;
    if (GetWindowRect(hwnd, &r)) {
        POINT p = {.x = r.left, .y = r.top - 4};
        if (ScreenToClient(GetParent(hwnd), &p))
            SetWindowPos(hwnd, NULL, p.x, p.y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
    }
    sample_text_wnd_data *wdata = snew(sample_text_wnd_data);
    wdata->dp = dp;
    wdata->sample = ctrl;
    wdata->sample_font = NULL;
    p_SetWindowSubclass(hwnd, sample_text_proc, 0, (DWORD_PTR)wdata);
}

static void colour_handler_override(dlgcontrol *ctrl, dlgparam *dp, void *data, int event)
{
    //Conf *conf = (Conf *)data;
    dlgcontrol_colour *ctrl_c = (dlgcontrol_colour *)ctrl;
    ctrl_c->orig_handler(ctrl, dp, data, event);
    int type = ctrl->type;
    if (
        (event == EVENT_SELCHANGE && type == CTRL_LISTBOX)
        || (event == EVENT_VALCHANGE && type == CTRL_EDITBOX)
        || (event == EVENT_CALLBACK && type == CTRL_BUTTON)
    )
        dlg_invalidate(ctrl_c->sample, dp, false);
}

static void sample_text_setup(struct controlbox *b, HWND *hwndp, bool has_help, bool midsession, int protocol)
{
    struct controlset *s;
    if (!sample_text_init()) return;

    /*
     * The Window/Colour panel.
     */
    s = ctrl_getset(b, "Window/Colours", "adjust", "-");
    handler_fn colour_handler_orig = NULL;
    dlgcontrol *listbox = NULL;
    for (int i = 0; i < s->ncontrols; i++) {
        dlgcontrol *c = s->ctrls[i];
        if (c->type == CTRL_LISTBOX) {
            colour_handler_orig = c->handler;
            listbox = c;
            break;
        }
    }
    if (!colour_handler_orig || !listbox) return;
    listbox->listbox.height -= 3;
    dlgcontrol *sample = ctrl_text(s, "Sample\nText", NULL);
    sample->handler = sample_text_handler;
    // override colour handler so that sample text is refreshed accordingly
    for (int i = 0; i < s->ncontrols; i++) {
        int type = s->ctrls[i]->type;
        if (!(type == CTRL_LISTBOX || type == CTRL_EDITBOX || type == CTRL_BUTTON)) continue;
        if (s->ctrls[i]->handler != colour_handler_orig) continue;
        dlgcontrol_colour *ctrl_c = snew(dlgcontrol_colour);
        dlgcontrol *orig_c = s->ctrls[i];
        memcpy(&ctrl_c->c, orig_c, sizeof ctrl_c->c);
        ctrl_c->c.handler = colour_handler_override;
        ctrl_c->orig_handler = colour_handler_orig;
        ctrl_c->sample = sample;
        colour_control_override(listbox, orig_c, &ctrl_c->c);
        if (orig_c == listbox) listbox = &ctrl_c->c, sample->context = P(&ctrl_c->c);
        s->ctrls[i] = &ctrl_c->c;
        sfree(orig_c);
    }
}

static COLORREF get_conf_colour_rgb(Conf *conf, int i)
{
    int r = conf_get_int_int(conf, CONF_colours, i*3+0);
    int g = conf_get_int_int(conf, CONF_colours, i*3+1);
    int b = conf_get_int_int(conf, CONF_colours, i*3+2);
    return RGB(r, g, b);
}

static void text_out(HDC hdc, int x, int y, const char *str, SIZE *sz)
{
    TextOutA(hdc, x, y, str, (int)strlen(str));
    GetTextExtentPoint32A(hdc, str, (int)strlen(str), sz);
}

static void sample_text_paint(HWND hwnd, HDC hdc, sample_text_wnd_data *wdata)
{
    dlgparam *dp = wdata->dp;
    Conf *conf = (Conf *)dp->data;
    if (!wdata->sample_font) {
        RECT cr;
        GetClientRect(hwnd, &cr);
        wdata->sample_font = CreateFontA(-cr.bottom / 2, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN,
            "Consolas"
        );
    }
    SelectObject(hdc, wdata->sample_font);
    SetBkMode(hdc, OPAQUE);

    int sel = dlg_listbox_index((dlgcontrol *)wdata->sample->context.p, dp);
    if (sel < 0 || sel >= CONF_NCOLOURS)
        sel = 0;
    int text_fg = CONF_COLOUR_fg;
    int text_bold = CONF_COLOUR_fg_bold;
    int text_current = CONF_COLOUR_fg;
    int bg_current = CONF_COLOUR_bg;
    int cursor_fg = CONF_COLOUR_cursor_fg;
    int cursor_bg = CONF_COLOUR_cursor_bg;
    static const uint8_t indexed_colours[] = {
        CONF_COLOUR_black, CONF_COLOUR_black_bold,
        CONF_COLOUR_red, CONF_COLOUR_red_bold,
        CONF_COLOUR_green, CONF_COLOUR_green_bold,
        CONF_COLOUR_yellow, CONF_COLOUR_yellow_bold,
        CONF_COLOUR_blue, CONF_COLOUR_blue_bold,
        CONF_COLOUR_magenta, CONF_COLOUR_magenta_bold,
        CONF_COLOUR_cyan, CONF_COLOUR_cyan_bold,
        CONF_COLOUR_white, CONF_COLOUR_white_bold,
    };
    for (int i = 0; i < lenof(indexed_colours); i += 2) {
        if (sel == indexed_colours[i] || sel == indexed_colours[i + 1]) {
            text_fg = indexed_colours[i];
            text_bold = indexed_colours[i + 1];
            break;
        }
    }
    if (sel == CONF_COLOUR_bg || sel == CONF_COLOUR_bg_bold)
        bg_current = sel;
    else if (sel == CONF_COLOUR_cursor_fg_ime || sel == CONF_COLOUR_cursor_bg_ime) {
        cursor_fg = CONF_COLOUR_cursor_fg_ime;
        cursor_bg = CONF_COLOUR_cursor_bg_ime;
    } else if (sel != CONF_COLOUR_bg && sel != CONF_COLOUR_cursor_fg && sel != CONF_COLOUR_cursor_bg)
        text_current = sel;
    static const uint8_t bold_colours[] = {
        CONF_COLOUR_fg_bold,
        CONF_COLOUR_bg_bold,
        CONF_COLOUR_black_bold,
        CONF_COLOUR_red_bold,
        CONF_COLOUR_green_bold,
        CONF_COLOUR_yellow_bold,
        CONF_COLOUR_blue_bold,
        CONF_COLOUR_magenta_bold,
        CONF_COLOUR_cyan_bold,
        CONF_COLOUR_white_bold,
    };
    bool is_bold = false;
    for (int i = 0; i < lenof(bold_colours); i++) {
        if (sel == bold_colours[i]) {
            is_bold = true;
            break;
        }
    }

    /*
      ^_: text colour
      $_: bg colour
      where _ is one of:
        s: selected colour
        p: plain colour
        b: bold colour
        0..7: indexed colour (bold if bold is selected in the list)
        c: cursor colour (IME-on if selected in the list)
        uppercased: use the opposite bg/text colour instead
    */
    const char st_str[] = "^s$sLorem ^p$pipsum ^bdolor $bsit^p amet$p, ^c$cc^p$ponsectetur\n"
      "^0adip^1isci^2ng el^3it, se^4d do e^5iusm^6od te^7mpor ";
    char st_buf[sizeof(st_str)];
    int x = 0, y = 0;
    SIZE sz = {.cy = 0};
    struct {
        uint8_t selected[2], plain[2], bold[2], cursor[2];
    } colour_map = {
        .selected = {text_current, bg_current},
        .plain = {text_fg, CONF_COLOUR_bg},
        .bold = {text_bold, CONF_COLOUR_bg_bold},
        .cursor = {cursor_fg, cursor_bg},
    };
    bool is_cursor = false;
    int cursor_type = conf_get_int(conf, CONF_cursor_type);
    int cursor_pen_colour = CONF_COLOUR_fg;
    for (int pos = 0, buf_len = 0;; pos++) {
        if (st_str[pos] && !(st_str[pos] == '^' || st_str[pos] == '$' || st_str[pos] == '\n')) {
            st_buf[buf_len++] = st_str[pos];
            continue;
        }
        if (buf_len) {
            st_buf[buf_len] = '\0';
            text_out(hdc, x, y, st_buf, &sz);
            if (is_cursor && cursor_type != CURSOR_BLOCK) {
                HPEN pen = CreatePen(PS_SOLID, 1, get_conf_colour_rgb(conf, cursor_pen_colour));
                HPEN old_pen = SelectObject(hdc, pen);
                if (cursor_type == CURSOR_UNDERLINE) {
                    int y2 = y + sz.cy * 15 / 18;
                    MoveToEx(hdc, x, y2, NULL);
                    LineTo(hdc, x + sz.cx + 1, y2);
                } else {  // CURSOR_VERTICAL_LINE
                    MoveToEx(hdc, x, y, NULL);
                    LineTo(hdc, x, y + sz.cy);
                }
                SelectObject(hdc, old_pen);
                DeleteObject(pen);
            }
            x += sz.cx;
            buf_len = 0;
        }
        if (!st_str[pos]) break;
        if (st_str[pos] == '\n') {
            x = 0;
            y += sz.cy; // only work with preceding text drawn
            continue;
        }
        bool is_text = st_str[pos] == '^';
        bool fg_bg = !is_text;
        char colour_name = st_str[++pos];
        if (colour_name >= 'A' && colour_name <= 'Z') {
            colour_name ^= 'A' ^ 'a';  // switch letter case; ASCII assumed
            fg_bg = !fg_bg;
        }
        int colour = -1;
        if (is_text) is_cursor = colour_name == 'c';
        switch (colour_name) {
          case 's':
            colour = colour_map.selected[fg_bg];
            break;
          case 'p':
            colour = colour_map.plain[fg_bg];
            break;
          case 'b':
            colour = colour_map.bold[fg_bg];
            break;
          case 'c':
            colour = colour_map.cursor[fg_bg];
            break;
          default:
            if (colour_name < '0' && colour_name > '7') break;
            colour = indexed_colours[(colour_name - '0') * 2 + (fg_bg ^ is_bold)];
            break;
        }
        if (colour >= 0) {
            if (!is_cursor || cursor_type == CURSOR_BLOCK) {
                if (is_text)
                    SetTextColor(hdc, get_conf_colour_rgb(conf, colour));
                else
                    SetBkColor(hdc, get_conf_colour_rgb(conf, colour));
            } else if (!is_text)
                cursor_pen_colour = colour;
        }
    }
}

static LRESULT CALLBACK sample_text_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data)
{
    sample_text_wnd_data *wdata = (sample_text_wnd_data *)data;
    switch (msg) {
      case WM_NCDESTROY:
        if (wdata->sample_font)
            DeleteObject(wdata->sample_font);
        sfree(wdata);
        p_RemoveWindowSubclass(hwnd, sample_text_proc, id);
        break;

      case WM_ERASEBKGND:
        return 1;

      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        sample_text_paint(hwnd, hdc, wdata);
        EndPaint(hwnd, &ps);
        return 0;
      }
    }
    return p_DefSubclassProc(hwnd, msg, wParam, lParam);
}
