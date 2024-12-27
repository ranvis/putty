#include "putty.h"
#include "win-gui-seat.h"
#include "wtrans.h"

static UINT next_timer_id = 1500;
static const uint16_t TRANSITION_MS_ACTIVATE = 100;
static const uint16_t TRANSITION_MS_INACTIVATE_MAX = 1000;
static const uint16_t TRANSITION_MS_INACTIVATE_PER_STEP = 20;

DECL_WINDOWS_FUNCTION(static, BOOL, SetLayeredWindowAttributes, (_In_ HWND hwnd, _In_ COLORREF crKey, _In_ BYTE bAlpha, _In_ DWORD dwFlags));

static void set_timer(WinGuiSeat *wgs);
static void change_alpha(WinGuiSeat *wgs, BYTE new_alpha, int delay);

static void set_window_alpha(WinGuiSeat *wgs, BYTE alpha)
{
    WgsWinTransparency *tr = &wgs->wtrans;
    if (tr->cur_alpha == alpha) {
        return;
    }
    tr->cur_alpha = alpha;
    p_SetLayeredWindowAttributes(wgs->term_hwnd, 0, alpha, LWA_ALPHA);
}

void wtrans_init(HMODULE user32_module)
{
    GET_WINDOWS_FUNCTION(user32_module, SetLayeredWindowAttributes);
}

void wtrans_new(WinGuiSeat *wgs)
{
    WgsWinTransparency *tr = &wgs->wtrans;
    tr->target_alpha = 255;
    tr->start_alpha = 255;
    tr->last_active_state = true;
    tr->timer_id = next_timer_id++;
}

int conf_clamp_int(Conf *conf, int pk, int min, int max)
{
    int value = conf_get_int(conf, pk);
    if (value < min) {
        value = min;
    } else if (value > max) {
        value = max;
    } else {
        return value;
    }
    conf_set_int(conf, pk, value);
    return value;
}

static void enable_alpha(WinGuiSeat *wgs)
{
    WgsWinTransparency *tr = &wgs->wtrans;
    if (!tr->is_trans_active) {
        tr->is_trans_active = true;
        SetWindowLong(wgs->term_hwnd, GWL_EXSTYLE, GetWindowLong(wgs->term_hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        set_window_alpha(wgs, 255);
    }
}

void wtrans_set(WinGuiSeat *wgs)
{
    WgsWinTransparency *tr = &wgs->wtrans;
    if (!p_SetLayeredWindowAttributes) {
        tr->is_trans_active = false;
        return;
    }
    Conf *conf = wgs->conf;
    int opacity = conf_clamp_int(conf, CONF_win_opacity, 30, 100);
    int opacity_ip = conf_clamp_int(conf, CONF_win_opacity_inactive_prod, 10, 100);
    conf_clamp_int(conf, CONF_win_opacity_inactive_delay, 0, 10000 * 1000);
    if (opacity == 100 && opacity_ip == 100) {
        if (tr->is_trans_active) {
            tr->is_trans_active = false;
            set_window_alpha(wgs, 255);
        }
        return;
    }
    enable_alpha(wgs);
    if (IsWindowVisible(wgs->term_hwnd)) {
        wtrans_activate(wgs, GetActiveWindow() == wgs->term_hwnd);
    } else {
        set_window_alpha(wgs, opacity * 255 / 100);
    }
}

static void CALLBACK timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD cur_time)
{
    WinGuiSeat *wgs = (WinGuiSeat *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    WgsWinTransparency *tr = &wgs->wtrans;
    DWORD elapsed = cur_time - tr->start_time;
    if (tr->delay) {
        elapsed = elapsed >= tr->delay ? elapsed - tr->delay : 0;
    }
    BYTE new_alpha;
    if (elapsed >= tr->duration) {
        new_alpha = tr->target_alpha;
    } else {
        float fraction = (float)elapsed / tr->duration;
        new_alpha = (BYTE)((1 - fraction) * tr->start_alpha + fraction * tr->target_alpha);
    }
    set_window_alpha(wgs, new_alpha);
    if (new_alpha == tr->target_alpha) {
        KillTimer(hwnd, tr->timer_id);
    } else if (tr->delay) {
        tr->start_time += tr->delay;
        tr->delay = 0;
        set_timer(wgs);
    }
}

static void set_timer(WinGuiSeat *wgs)
{
    WgsWinTransparency *tr = &wgs->wtrans;
    int delta = abs((int)tr->cur_alpha - (int)tr->target_alpha);
    DWORD interval = max(10, tr->duration / delta);
    SetTimer(wgs->term_hwnd, tr->timer_id, tr->delay ? tr->delay : interval, timer_proc);
}

void wtrans_activate(WinGuiSeat *wgs, bool is_active)
{
    WgsWinTransparency *tr = &wgs->wtrans;
    if (!tr->is_trans_active) {
        return;
    }
    Conf *conf = wgs->conf;
    BYTE new_alpha = is_active ? (BYTE)conf_get_int(conf, CONF_win_opacity) * 255 / 100
        : (BYTE)max(10, (conf_get_int(conf, CONF_win_opacity) * conf_get_int(conf, CONF_win_opacity_inactive_prod) * 255 / 10000));
    tr->last_active_state = is_active;
    if (tr->is_preview_mode) {
        return;
    }
    int delay = is_active ? 0 : conf_get_int(conf, CONF_win_opacity_inactive_delay);
    change_alpha(wgs, new_alpha, delay);
}

static void change_alpha(WinGuiSeat *wgs, BYTE new_alpha, int delay)
{
    WgsWinTransparency *tr = &wgs->wtrans;
    if (new_alpha == tr->cur_alpha) {
        KillTimer(wgs->term_hwnd, tr->timer_id);
        return;
    }
    tr->target_alpha = new_alpha;
    tr->start_time = GetTickCount();
    tr->start_alpha = tr->cur_alpha;
    tr->delay = delay;
    if (tr->delay <= 20) {
        tr->delay = 0;
    }
    tr->duration = !delay ? TRANSITION_MS_ACTIVATE
        : min(TRANSITION_MS_INACTIVATE_MAX, (tr->cur_alpha - tr->target_alpha) * TRANSITION_MS_INACTIVATE_PER_STEP);
    set_timer(wgs);
}

void wtrans_destroy(WinGuiSeat *wgs)
{
    KillTimer(wgs->term_hwnd, wgs->wtrans.timer_id);
}

void wtrans_begin_preview(WinGuiSeat *wgs)
{
    WgsWinTransparency *tr = &wgs->wtrans;
    if (!p_SetLayeredWindowAttributes || tr->is_preview_mode) {
        return;
    }
    tr->is_preview_mode = true;
}

void wtrans_preview(WinGuiSeat *wgs, int opacity)
{
    WgsWinTransparency *tr = &wgs->wtrans;
    if (!tr->is_preview_mode) {
        return;
    }
    BYTE alpha = max(30, min(100, opacity)) * 255 / 100;
    if (tr->cur_alpha == alpha) {
        return;
    }
    enable_alpha(wgs);
    change_alpha(wgs, alpha, 0);
}

void wtrans_end_preview(WinGuiSeat *wgs)
{
    WgsWinTransparency *tr = &wgs->wtrans;
    if (!tr->is_preview_mode) {
        return;
    }
    tr->is_preview_mode = false;
    wtrans_activate(wgs, tr->last_active_state);
}
