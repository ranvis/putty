#include "putty.h"
#include "winwallp.h"

extern Conf *conf;

HBITMAP background_bmp = NULL;
int bg_width = 0, bg_height = 0;
BOOL bg_has_alpha;

static void paint_bg_remaining(HDC hdc, int x, int y, int width, int height, const RECT *excl_rect);
static void wallpaper_get_offset(const RECT *rect, int wp_width, int wp_height, int *x, int *y);

void wallpaper_paint_zoom(HDC hdc, int x, int y, int width, int height, HDC bg_hdc)
{
    RECT term_rect;
    int is_win_ratio_larger;
    int blit_x, blit_y, blit_width, blit_height;
    HRGN old_clip;
    int has_clip;
    int placement = conf_get_int(conf, CONF_wallpaper_place);
    int is_mode_fit = !!(placement & WALLPAPER_PLACE_FIT);
    GetClientRect(hwnd, &term_rect);
    is_win_ratio_larger = bg_width < bg_height * term_rect.right / term_rect.bottom;
    if (is_win_ratio_larger == is_mode_fit) {
	blit_width = term_rect.right;
	blit_height = term_rect.right * bg_height / bg_width;
    } else {
	blit_height = term_rect.bottom;
	blit_width = term_rect.bottom * bg_width / bg_height;
    }
    if (!(placement & WALLPAPER_PLACE_EXPAND) && blit_width > bg_width) {
	blit_width = bg_width;
	blit_height = bg_height;
    }
    wallpaper_get_offset(&term_rect, blit_width, blit_height, &blit_x, &blit_y);
    old_clip = CreateRectRgn(0, 0, 0, 0);
    has_clip = GetClipRgn(hdc, old_clip);
    if (has_clip != -1) {
	if (has_clip == 1)
	    IntersectClipRect(hdc, x, y, x + width, y + height);
	else {
	    HRGN clip_rgn = CreateRectRgn(x, y, x + width, y + height);
	    SelectClipRgn(hdc, clip_rgn);
	    DeleteObject(clip_rgn);
	}
	SetStretchBltMode(hdc, HALFTONE);
	StretchBlt(hdc, blit_x, blit_y, blit_width, blit_height,
		   bg_hdc, 0, 0, bg_width, bg_height, SRCCOPY);
	if (!is_mode_fit) {
	    RECT update_rect;
	    SetRect(&update_rect, blit_x, blit_y, blit_x + blit_width, blit_y + blit_height);
	    paint_bg_remaining(hdc, x, y, width, height, &update_rect);
	}
	SelectClipRgn(hdc, has_clip ? old_clip : NULL);
    } else
	wallpaper_fill_bgcolor(hdc, x, y, width, height);
    DeleteObject(old_clip);
}

void wallpaper_paint_tile(HDC hdc, int x, int y, int width, int height, HDC bg_hdc)
{
    POINT dest_pos;
    int rem_w, rem_h;
    int shift_x, shift_y;
    RECT term_rect, wp_rect;
    int placement = conf_get_int(conf, CONF_wallpaper_place);
    GetClientRect(hwnd, &term_rect);
    wallpaper_get_offset(&term_rect, bg_width, bg_height, &shift_x, &shift_y);
    SetRect(&wp_rect, shift_x, shift_y, shift_x + bg_width, shift_y + bg_height);
    if (placement & WALLPAPER_PLACE_TILE_X)
	wp_rect.left = 0, wp_rect.right = term_rect.right;
    if (placement & WALLPAPER_PLACE_TILE_Y)
	wp_rect.top = 0, wp_rect.bottom = term_rect.bottom;
    shift_x = bg_width - shift_x % bg_width;
    shift_y = bg_height - shift_y % bg_height;
    rem_h = height;
    for (dest_pos.y = y; dest_pos.y < y + height; ) {
	int src_y = (shift_y + dest_pos.y) % bg_height;
	int src_h = bg_height - src_y;
	rem_w = width;
	for (dest_pos.x = x; dest_pos.x < x + width; ) {
	    int src_x = (shift_x + dest_pos.x) % bg_width;
	    int src_w = bg_width - src_x;
	    if (PtInRect(&wp_rect, dest_pos))
		BitBlt(hdc, dest_pos.x, dest_pos.y, min(rem_w, src_w), min(rem_h, src_h),
		       bg_hdc, src_x, src_y, SRCCOPY);
	    else
		wallpaper_fill_bgcolor(hdc, dest_pos.x, dest_pos.y, min(rem_w, src_w), min(rem_h, src_h));
	    dest_pos.x += src_w;
	    rem_w -= src_w;
	}
	dest_pos.y += src_h;
	rem_h -= src_h;
    }
}

static void paint_bg_remaining(HDC hdc, int x, int y, int width, int height, const RECT *excl_rect)
{
    HPEN pen, saved_pen;
    HBRUSH brush, saved_brush;
    pen = CreatePen(PS_SOLID, 0, wallpaper_get_bg_color());
    brush = CreateSolidBrush(wallpaper_get_bg_color());
    saved_pen = SelectObject(hdc, pen);
    saved_brush = SelectObject(hdc, brush);
    if (y < excl_rect->top)
	Rectangle(hdc, x, y, x + width, excl_rect->top);
    if (y + height > excl_rect->bottom)
	Rectangle(hdc, x, excl_rect->bottom, x + width, y + height);
    if (x < excl_rect->left)
	Rectangle(hdc, x, excl_rect->top, excl_rect->left, excl_rect->bottom);
    if (x + width > excl_rect->right)
	Rectangle(hdc, excl_rect->right, excl_rect->top, x + width, excl_rect->bottom);
    SelectObject(hdc, saved_pen);
    SelectObject(hdc, saved_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void wallpaper_get_offset(const RECT *rect, int wp_width, int wp_height, int *x, int *y)
{
    int alignment = conf_get_int(conf, CONF_wallpaper_align);
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
    if (!GET_WINDOWS_FUNCTION(module, GdiplusStartup)
	|| !GET_WINDOWS_FUNCTION(module, GdiplusShutdown)
	|| !GET_WINDOWS_FUNCTION(module, GdipCreateBitmapFromFile)
	|| !GET_WINDOWS_FUNCTION(module, GdipCreateHBITMAPFromBitmap)
	|| !GET_WINDOWS_FUNCTION(module, GdipDisposeImage)) {
	FreeLibrary(module);
	return 0;
    }
    if (p_GdiplusStartup(&gdip_token, &input, 0) != GDIP_STATUS_OK) {
	gdip_token = 0;
	return 0;
    }
    return 1;
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
