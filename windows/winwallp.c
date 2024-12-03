#include "putty.h"
#include "winwallp.h"
#include "win-gui-seat.h"

HBITMAP background_bmp = NULL;
HBITMAP img_bmp;
BOOL bg_has_alpha;
BOOL img_has_alpha;

DECL_WINDOWS_FUNCTION(static, BOOL, AlphaBlend, (HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, BLENDFUNCTION ftn));

BOOL msimg_alphablend(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, BLENDFUNCTION ftn)
{
    static HMODULE module;
    if (!module)
        module = load_system32_dll("msimg32.dll");
    if (!module || !p_AlphaBlend && !GET_WINDOWS_FUNCTION(module, AlphaBlend))
        return FALSE;
    return p_AlphaBlend(hdcDest, xoriginDest, yoriginDest, wDest, hDest, hdcSrc, xoriginSrc, yoriginSrc, wSrc, hSrc, ftn);
}

static void wallpaper_paint_zoom(WinGuiSeat *wgs, HDC hdc, const RECT *rect, HDC bg_hdc, int bg_w, int bg_h, const wallpaper_paint_mode *mode);
static void wallpaper_paint_tile(WinGuiSeat *wgs, HDC hdc, const RECT *rect, HDC bg_hdc, int bg_w, int bg_h, const wallpaper_paint_mode *mode);
static void paint_bg_remaining(WinGuiSeat *wgs, HDC hdc, const RECT *rect, const RECT *excl_rect);
static void wallpaper_get_offset(const RECT *rect, int wp_width, int wp_height, const wallpaper_paint_mode *mode, int *x, int *y);

void wallpaper_paint(WinGuiSeat *wgs, HDC hdc, const RECT *rect, HBITMAP hbmp, const wallpaper_paint_mode *mode)
{
    HDC bg_hdc = CreateCompatibleDC(hdc);
    HBITMAP prev_bmp = SelectObject(bg_hdc, hbmp);
    int bg_w, bg_h;
    get_bitmap_size(hbmp, &bg_w, &bg_h);
    if (mode->place & WALLPAPER_PLACE_SHRINK)
        wallpaper_paint_zoom(wgs, hdc, rect, bg_hdc, bg_w, bg_h, mode);
    else
        wallpaper_paint_tile(wgs, hdc, rect, bg_hdc, bg_w, bg_h, mode);
    SelectObject(bg_hdc, prev_bmp);
    DeleteDC(bg_hdc);
}

void wallpaper_paint_zoom(WinGuiSeat *wgs, HDC hdc, const RECT *rect, HDC bg_hdc, int bg_w, int bg_h, const wallpaper_paint_mode *mode)
{
    const HWND hwnd = wgs->term_hwnd;
    RECT term_rect;
    int is_win_ratio_larger;
    int blit_x, blit_y, blit_width, blit_height;
    HRGN old_clip;
    int has_clip;
    int placement = mode->place;
    int is_mode_fit = !!(placement & WALLPAPER_PLACE_FIT);
    GetClientRect(hwnd, &term_rect);
    is_win_ratio_larger = bg_w < bg_h * term_rect.right / term_rect.bottom;
    if (is_win_ratio_larger == is_mode_fit) {
        blit_width = term_rect.right;
        blit_height = term_rect.right * bg_h / bg_w;
    } else {
        blit_height = term_rect.bottom;
        blit_width = term_rect.bottom * bg_w / bg_h;
    }
    if (!(placement & WALLPAPER_PLACE_EXPAND) && blit_width > bg_w) {
        blit_width = bg_w;
        blit_height = bg_h;
    }
    wallpaper_get_offset(&term_rect, blit_width, blit_height, mode, &blit_x, &blit_y);
    old_clip = CreateRectRgn(0, 0, 0, 0);
    has_clip = GetClipRgn(hdc, old_clip);
    if (has_clip != -1) {
        if (has_clip == 1)
            IntersectClipRect(hdc, rect->left, rect->top, rect->right, rect->bottom);
        else {
            HRGN clip_rgn = CreateRectRgn(rect->left, rect->top, rect->right, rect->bottom);
            SelectClipRgn(hdc, clip_rgn);
            DeleteObject(clip_rgn);
        }
        if (mode->opaque) {
            SetStretchBltMode(hdc, HALFTONE);
            StretchBlt(hdc, blit_x, blit_y, blit_width, blit_height,
                       bg_hdc, 0, 0, bg_w, bg_h, SRCCOPY);
            if (!is_mode_fit) {
                RECT update_rect;
                SetRect(&update_rect, blit_x, blit_y, blit_x + blit_width, blit_y + blit_height);
                paint_bg_remaining(wgs, hdc, rect, &update_rect);
            }
        } else {
            msimg_alphablend(hdc, blit_x, blit_y, blit_width, blit_height,
                       bg_hdc, 0, 0, bg_w, bg_h, mode->bf);
        }
        SelectClipRgn(hdc, has_clip ? old_clip : NULL);
    } else if (mode->opaque)
        wallpaper_fill_bgcolor(wgs, hdc, rect);
    DeleteObject(old_clip);
}

void wallpaper_paint_tile(WinGuiSeat *wgs, HDC hdc, const RECT *rect, HDC bg_hdc, int bg_w, int bg_h, const wallpaper_paint_mode *mode)
{
    const HWND hwnd = wgs->term_hwnd;
    POINT dest_pos;
    int rem_w, rem_h;
    int shift_x, shift_y;
    RECT term_rect, wp_rect;
    int placement = mode->place;
    GetClientRect(hwnd, &term_rect);
    wallpaper_get_offset(&term_rect, bg_w, bg_h, mode, &shift_x, &shift_y);
    SetRect(&wp_rect, shift_x, shift_y, shift_x + bg_w, shift_y + bg_h);
    if (placement & WALLPAPER_PLACE_TILE_X)
        wp_rect.left = 0, wp_rect.right = term_rect.right;
    if (placement & WALLPAPER_PLACE_TILE_Y)
        wp_rect.top = 0, wp_rect.bottom = term_rect.bottom;
    shift_x = bg_w - shift_x % bg_w;
    shift_y = bg_h - shift_y % bg_h;
    rem_h = rect->bottom - rect->top;
    for (dest_pos.y = rect->top; dest_pos.y < rect->bottom; ) {
        int src_y = (shift_y + dest_pos.y) % bg_h;
        int src_h = bg_h - src_y;
        rem_w = rect->right - rect->left;
        for (dest_pos.x = rect->left; dest_pos.x < rect->right; ) {
            int src_x = (shift_x + dest_pos.x) % bg_w;
            int src_w = bg_w - src_x;
            int width = min(rem_w, src_w);
            int height = min(rem_h, src_h);
            if (PtInRect(&wp_rect, dest_pos)) {
                if (mode->opaque) {
                    BitBlt(hdc, dest_pos.x, dest_pos.y, width, height,
                           bg_hdc, src_x, src_y, SRCCOPY);
                } else {
                    msimg_alphablend(hdc, dest_pos.x, dest_pos.y, width, height,
                           bg_hdc, src_x, src_y, width, height, mode->bf);
                }
            } else if (mode->opaque) {
                RECT tile_rect;
                SetRect(&tile_rect, dest_pos.x, dest_pos.y, dest_pos.x + min(rem_w, src_w), dest_pos.y + min(rem_h, src_h));
                wallpaper_fill_bgcolor(wgs, hdc, &tile_rect);
            }
            dest_pos.x += src_w;
            rem_w -= src_w;
        }
        dest_pos.y += src_h;
        rem_h -= src_h;
    }
}

static void paint_bg_remaining(WinGuiSeat *wgs, HDC hdc, const RECT *rect, const RECT *excl_rect)
{
    HPEN pen, saved_pen;
    HBRUSH brush, saved_brush;
    pen = CreatePen(PS_SOLID, 0, wallpaper_get_bg_color(wgs));
    brush = CreateSolidBrush(wallpaper_get_bg_color(wgs));
    saved_pen = SelectObject(hdc, pen);
    saved_brush = SelectObject(hdc, brush);
    if (rect->top < excl_rect->top)
        Rectangle(hdc, rect->left, rect->top, rect->right, excl_rect->top);
    if (rect->bottom > excl_rect->bottom)
        Rectangle(hdc, rect->left, excl_rect->bottom, rect->right, rect->bottom);
    if (rect->left < excl_rect->left)
        Rectangle(hdc, rect->left, excl_rect->top, excl_rect->left, excl_rect->bottom);
    if (rect->right > excl_rect->right)
        Rectangle(hdc, excl_rect->right, excl_rect->top, rect->right, excl_rect->bottom);
    SelectObject(hdc, saved_pen);
    SelectObject(hdc, saved_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

HBITMAP create_large_bitmap(HDC hdc, int width, int height, Conf *conf)
{
    HBITMAP bmp = conf_get_bool(conf, CONF_use_ddb) ? CreateCompatibleBitmap(hdc, width, height) : NULL;
    if (bmp == NULL) {
        BITMAPINFOHEADER bmp_info = {sizeof(BITMAPINFOHEADER)};
        void *pixels;
        bmp_info.biWidth = width;
        bmp_info.biHeight = height;
        bmp_info.biPlanes = 1;
        bmp_info.biBitCount = max(16, GetDeviceCaps(hdc, BITSPIXEL)); /* no multimonitor thing */
        bmp_info.biCompression= BI_RGB;
        bmp = CreateDIBSection(hdc, (BITMAPINFO *)&bmp_info, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    }
    return bmp;
}

void get_bitmap_size(HBITMAP hbmp, int *width, int *height)
{
    BITMAP bitmap;
    GetObject(hbmp, sizeof bitmap, &bitmap);
    *width = bitmap.bmWidth;
    *height = bitmap.bmHeight;
}

static void wallpaper_get_offset(const RECT *rect, int wp_width, int wp_height, const wallpaper_paint_mode *mode, int *x, int *y)
{
    int alignment = mode->align;
    if (alignment & WALLPAPER_ALIGN_CENTER)
        *x = (rect->right - wp_width) / 2;
    else if (alignment & WALLPAPER_ALIGN_RIGHT)
        *x = rect->right - wp_width;
    else
        *x = 0;
    if (alignment & WALLPAPER_ALIGN_MIDDLE)
        *y = (rect->bottom - wp_height) / 2;
    else if (alignment & WALLPAPER_ALIGN_BOTTOM)
        *y = rect->bottom - wp_height;
    else
        *y = 0;
}


/* gdiplus */

#define GDIP_STATUS_OK 0

typedef int GpStatus;
typedef DWORD ARGB;
typedef struct GpBitmap_tag GpBitmap, GpImage;

typedef struct GdiplusStartupInput_tag {
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef struct GdiplusStartupOutput_tag {
    void *NotificationHook;
    void *NotificationUnhook;
} GdiplusStartupOutput;

DECL_WINDOWS_FUNCTION(static, GpStatus, GdiplusStartup, (OUT ULONG_PTR *token, const GdiplusStartupInput *input, OUT GdiplusStartupOutput *output));
DECL_WINDOWS_FUNCTION(static, VOID, GdiplusShutdown, (ULONG_PTR token));
DECL_WINDOWS_FUNCTION(static, GpStatus, GdipCreateBitmapFromFile, (const WCHAR* filename, GpBitmap **bitmap));
DECL_WINDOWS_FUNCTION(static, GpStatus, GdipCreateHBITMAPFromBitmap, (GpBitmap* bitmap, HBITMAP* hbmReturn, ARGB background));
DECL_WINDOWS_FUNCTION(static, GpStatus, GdipDisposeImage, (GpImage *image));

static ULONG_PTR gdip_token;

int gdip_init(void)
{
    HMODULE module;
    GdiplusStartupInput input = {1, NULL, FALSE, FALSE};
    if (gdip_token)
        return 1;
    module = load_system32_dll("gdiplus.dll");
    if (!module)
        return 0;
    /* omit fancy type checking so that we don't have to include C++ headers */
    if (!GET_WINDOWS_FUNCTION_NO_TYPECHECK(module, GdiplusStartup)
        || !GET_WINDOWS_FUNCTION_NO_TYPECHECK(module, GdiplusShutdown)
        || !GET_WINDOWS_FUNCTION_NO_TYPECHECK(module, GdipCreateBitmapFromFile)
        || !GET_WINDOWS_FUNCTION_NO_TYPECHECK(module, GdipCreateHBITMAPFromBitmap)
        || !GET_WINDOWS_FUNCTION_NO_TYPECHECK(module, GdipDisposeImage)) {
        goto bail;
    }
    if (p_GdiplusStartup(&gdip_token, &input, 0) != GDIP_STATUS_OK) {
        gdip_token = 0;
        goto bail;
    }
    return 1;
bail:
    FreeLibrary(module);
    return 0;
}

void gdip_terminate(void)
{
    if (gdip_token) {
        p_GdiplusShutdown(gdip_token);
        gdip_token = 0;
    }
}

HBITMAP gdip_load_image(const char *path)
{
    WCHAR u_path[MAX_PATH];
    GpBitmap *image;
    HBITMAP bitmap;
    if (!gdip_init())
        return NULL;
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, u_path, lenof(u_path)))
        return NULL;
    if (p_GdipCreateBitmapFromFile(u_path, &image) != GDIP_STATUS_OK)
        return NULL;
    if (p_GdipCreateHBITMAPFromBitmap(image, &bitmap, 0) != GDIP_STATUS_OK)
        bitmap = NULL;
    p_GdipDisposeImage(image);
    return bitmap;
}
