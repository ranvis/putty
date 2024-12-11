#ifndef PUTTY_WINWALLP_H
#define PUTTY_WINWALLP_H

#ifdef _WINDOWS
#include <wingdi.h>
#endif

enum {
    WALLPAPER_MODE_DESKTOP = 1,
    WALLPAPER_MODE_IMAGE = 2,
    WALLPAPER_MODE_DTIMG = 3,
};

enum {
    WALLPAPER_ALIGN_CENTER = 1 << 0,
    WALLPAPER_ALIGN_RIGHT = 1 << 1,
    WALLPAPER_ALIGN_MIDDLE = 1 << 2,
    WALLPAPER_ALIGN_BOTTOM = 1 << 3,
    WALLPAPER_ALIGN_DEFAULT = 0,
};

enum {
    WALLPAPER_PLACE_ORIGINAL = 0,
    WALLPAPER_PLACE_TILE_X = 1 << 0,
    WALLPAPER_PLACE_TILE_Y = 1 << 1,
    WALLPAPER_PLACE_SHRINK = 1 << 2,
    WALLPAPER_PLACE_EXPAND = 1 << 3,
    WALLPAPER_PLACE_FIT = 1 << 4,
    WALLPAPER_PLACE_DEFAULT = WALLPAPER_PLACE_TILE_X | WALLPAPER_PLACE_TILE_Y,
};

#ifdef _WINDOWS

typedef struct WinGuiSeat WinGuiSeat;

typedef struct wallpaper_paint_mode_tag {
    int place, align;
    BOOL opaque;
    BLENDFUNCTION bf;
} wallpaper_paint_mode;

extern HBITMAP background_bmp;
extern HBITMAP img_bmp;
extern bool bg_has_alpha;
extern bool img_has_alpha;

BOOL msimg_alphablend(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, BLENDFUNCTION ftn);
void wallpaper_paint(WinGuiSeat *wgs, HDC hdc, const RECT *rect, HBITMAP hbmp, const wallpaper_paint_mode *mode);
void wallpaper_fill_bgcolor(WinGuiSeat *wgs, HDC hdc, const RECT *rect);
HBITMAP create_large_bitmap(HDC hdc, int width, int height, Conf *conf);
void get_bitmap_size(HBITMAP hbmp, int *width, int *height);
COLORREF wallpaper_get_bg_color(WinGuiSeat *wgs);

/* gdiplus */

int gdip_init(void);
void gdip_terminate(void);
HBITMAP gdip_load_image(const WCHAR *path);

#define FILTER_IMAGE_FILES_GDIP (L"Image Files\0*.bmp;*.jpg;*.jpeg;*.png;*.tiff;*.wmf;*.gif\0" \
                                 "All Files (*.*)\0*\0")

#define FILTER_IMAGE_FILES_GDIP_C ("Image Files\0*.bmp;*.jpg;*.jpeg;*.png;*.tiff;*.wmf;*.gif\0" \
                                 "All Files (*.*)\0*\0")

#endif // _WINDOWS

#endif
