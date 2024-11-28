#ifndef PUTTY_WINWALLP_H
#define PUTTY_WINWALLP_H

#include <wingdi.h>

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

typedef struct wallpaper_paint_mode_tag {
    int place, align;
    BOOL opaque;
    BLENDFUNCTION bf;
} wallpaper_paint_mode;

extern HBITMAP background_bmp;
extern HBITMAP img_bmp;
extern BOOL bg_has_alpha;
extern BOOL img_has_alpha;

extern BOOL msimg_alphablend(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, BLENDFUNCTION ftn);
extern void wallpaper_paint(HDC hdc, const RECT *rect, HBITMAP hbmp, const wallpaper_paint_mode *mode);
extern void wallpaper_fill_bgcolor(HDC hdc, const RECT *rect);
extern HBITMAP create_large_bitmap(HDC hdc, int width, int height);
extern void get_bitmap_size(HBITMAP hbmp, int *width, int *height);
extern COLORREF wallpaper_get_bg_color(void);

/* gdiplus */

int gdip_init(void);
void gdip_terminate(void);
HBITMAP gdip_load_image(const char *path);

#define FILTER_IMAGE_FILES_GDIP (L"Image Files\0*.bmp;*.jpg;*.jpeg;*.png;*.tiff;*.wmf;*.gif\0" \
                                 "All Files (*.*)\0*\0")

#define FILTER_IMAGE_FILES_GDIP_C ("Image Files\0*.bmp;*.jpg;*.jpeg;*.png;*.tiff;*.wmf;*.gif\0" \
                                 "All Files (*.*)\0*\0")

#endif
