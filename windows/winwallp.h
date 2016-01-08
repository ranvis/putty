#ifndef PUTTY_WINWALLP_H
#define PUTTY_WINWALLP_H

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

extern HBITMAP background_bmp;
extern int bg_width, bg_height;

extern void wallpaper_paint_zoom(HDC hdc, int x, int y, int width, int height, HDC bg_hdc);
extern void wallpaper_paint_tile(HDC hdc, int x, int y, int width, int height, HDC bg_hdc);
extern void wallpaper_fill_bgcolor(HDC hdc, int x, int y, int width, int height);
extern COLORREF wallpaper_get_bg_color(void);

#endif
