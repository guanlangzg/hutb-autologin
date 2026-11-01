#ifndef HUTB_ICON_WIN_H
#define HUTB_ICON_WIN_H

#include <windows.h>

/* 画一个 16x16 圆点图标；返回的 HICON 需调用方 DestroyIcon */
HICON hutb_make_dot_icon(COLORREF c);

#endif
