#include "putty.h"
#include "wtrans.h"

static const UINT WTRANS_TIMER_ID = 500;
static const uint16_t TRANSITION_MS_ACTIVATE = 100;
static const uint16_t TRANSITION_MS_INACTIVATE_MAX = 1000;
static const uint16_t TRANSITION_MS_INACTIVATE_PER_STEP = 20;

// per window globals
static BYTE cur_alpha = 255;
static BYTE target_alpha = 255;
static BYTE transition_start_alpha = 255;
static DWORD transition_start_time;
static DWORD transition_delay;
static uint16_t transition_duration;
static bool is_trans_active;
static bool is_preview_mode;
static bool last_active_state = true;

DECL_WINDOWS_FUNCTION(static, BOOL, SetLayeredWindowAttributes, (_In_ HWND hwnd, _In_ COLORREF crKey, _In_ BYTE bAlpha, _In_ DWORD dwFlags));

static void set_timer(HWND hwnd);
static void change_alpha(HWND hwnd, BYTE new_alpha, int delay);

static void set_window_alpha(HWND hwnd, BYTE alpha)
{
    if (cur_alpha == alpha) {
        return;
    }
    cur_alpha = alpha;
    p_SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
}

void wtrans_init(HMODULE user32_module)
{
    GET_WINDOWS_FUNCTION(user32_module, SetLayeredWindowAttributes);
    DECL_WINDOWS_FUNCTION(, BOOL, SetUserObjectInformationA, (_In_ HANDLE hObj, _In_ int nIndex, _In_reads_bytes_(nLength) PVOID pvInfo, _In_ DWORD nLength));
    GET_WINDOWS_FUNCTION(user32_module, SetUserObjectInformationA);
    if (p_SetUserObjectInformationA) {
        BOOL timer_ex_mute = false;
        p_SetUserObjectInformationA(GetCurrentProcess(), UOI_TIMERPROC_EXCEPTION_SUPPRESSION, &timer_ex_mute, sizeof timer_ex_mute);
    }
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

static void enable_alpha(HWND hwnd)
{
    if (!is_trans_active) {
        is_trans_active = true;
        SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    }
}

void wtrans_set(Conf *conf, HWND hwnd)
{
    if (!p_SetLayeredWindowAttributes) {
        is_trans_active = false;
        return;
    }
    int opacity = conf_clamp_int(conf, CONF_win_opacity, 30, 100);
    int opacity_ip = conf_clamp_int(conf, CONF_win_opacity_inactive_prod, 10, 100);
    conf_clamp_int(conf, CONF_win_opacity_inactive_delay, 0, 10000 * 1000);
    if (opacity == 100 && opacity_ip == 100) {
        if (is_trans_active) {
            is_trans_active = false;
            set_window_alpha(hwnd, 255);
        }
        return;
    }
    enable_alpha(hwnd);
    if (IsWindowVisible(hwnd)) {
        wtrans_activate(conf, hwnd, GetActiveWindow() == hwnd);
    } else {
        set_window_alpha(hwnd, opacity * 255 / 100);
    }
}

static void CALLBACK timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD cur_time)
{
    DWORD elapsed = cur_time - transition_start_time;
    if (transition_delay) {
        elapsed = elapsed >= transition_delay ? elapsed - transition_delay : 0;
    }
    BYTE new_alpha;
    if (elapsed >= transition_duration) {
        new_alpha = target_alpha;
    } else {
        float fraction = (float)elapsed / transition_duration;
        new_alpha = (BYTE)((1 - fraction) * transition_start_alpha + fraction * target_alpha);
    }
    set_window_alpha(hwnd, new_alpha);
    if (new_alpha == target_alpha) {
        KillTimer(hwnd, WTRANS_TIMER_ID);
    } else if (transition_delay) {
        transition_start_time += transition_delay;
        transition_delay = 0;
        set_timer(hwnd);
    }
}

static void set_timer(HWND hwnd)
{
    int delta = abs((int)cur_alpha - (int)target_alpha);
    DWORD interval = max(10, transition_duration / delta);
    SetTimer(hwnd, WTRANS_TIMER_ID, transition_delay ? transition_delay : interval, timer_proc);
}

void wtrans_activate(Conf *conf, HWND hwnd, bool is_active)
{
    if (!is_trans_active) {
        return;
    }
    BYTE new_alpha = is_active ? (BYTE)conf_get_int(conf, CONF_win_opacity) * 255 / 100
        : (BYTE)max(10, (conf_get_int(conf, CONF_win_opacity) * conf_get_int(conf, CONF_win_opacity_inactive_prod) * 255 / 10000));
    last_active_state = is_active;
    if (is_preview_mode) {
        return;
    }
    int delay = is_active ? 0 : conf_get_int(conf, CONF_win_opacity_inactive_delay);
    change_alpha(hwnd, new_alpha, delay);
}

static void change_alpha(HWND hwnd, BYTE new_alpha, int delay)
{
    if (new_alpha == cur_alpha) {
        KillTimer(hwnd, WTRANS_TIMER_ID);
        return;
    }
    target_alpha = new_alpha;
    transition_start_time = GetTickCount();
    transition_start_alpha = cur_alpha;
    transition_delay = delay;
    if (transition_delay <= 20) {
        transition_delay = 0;
    }
    transition_duration = !delay ? TRANSITION_MS_ACTIVATE
        : min(TRANSITION_MS_INACTIVATE_MAX, (cur_alpha - target_alpha) * TRANSITION_MS_INACTIVATE_PER_STEP);
    set_timer(hwnd);
}

void wtrans_destroy(HWND hwnd)
{
    KillTimer(hwnd, WTRANS_TIMER_ID);
}

void wtrans_begin_preview(HWND hwnd)
{
    if (!p_SetLayeredWindowAttributes || is_preview_mode) {
        return;
    }
    is_preview_mode = true;
}

void wtrans_preview(HWND hwnd, int opacity)
{
    if (!is_preview_mode) {
        return;
    }
    BYTE alpha = max(30, min(100, opacity)) * 255 / 100;
    if (cur_alpha == alpha) {
        return;
    }
    enable_alpha(hwnd);
    change_alpha(hwnd, alpha, 0);
}

void wtrans_end_preview(Conf *conf, HWND hwnd)
{
    if (!is_preview_mode) {
        return;
    }
    is_preview_mode = false;
    wtrans_activate(conf, hwnd, last_active_state);
}
