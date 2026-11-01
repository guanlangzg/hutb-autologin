/* ============================================================================
 *  托盘图标：运行时绘制，不需要 .ico 资源文件
 *  颜色表示状态：绿=已连接 黄=连接中 红=连不上
 * ========================================================================== */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "icon_win.h"

HICON hutb_make_dot_icon(COLORREF c)
{
    const int S = 16;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = S;
    bi.bmiHeader.biHeight = -S;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) return NULL;

    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) { DeleteDC(hdc); return NULL; }
    HGDIOBJ oldBmp = SelectObject(hdc, bmp);

    memset(bits, 0, (size_t)S * S * 4);          /* 透明底 */

    HBRUSH br = CreateSolidBrush(c);
    HGDIOBJ oldBr = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, 1, 1, S - 1, S - 1);
    SelectObject(hdc, oldPen);

    /* 中间点一个白色小圆，深色任务栏上也看得清 */
    HBRUSH wb = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(hdc, wb);
    Ellipse(hdc, 6, 6, 10, 10);
    SelectObject(hdc, oldBr);
    DeleteObject(br);
    DeleteObject(wb);
    SelectObject(hdc, oldBmp);
    DeleteDC(hdc);

    /* GDI 不写 alpha，手动补：非黑像素视为不透明 */
    BYTE *px = (BYTE *)bits;
    for (int i = 0; i < S * S; i++) {
        BYTE *p = px + i * 4;                    /* BGRA */
        if (p[0] | p[1] | p[2]) p[3] = 255;
    }

    HBITMAP mask = CreateBitmap(S, S, 1, 1, NULL);
    ICONINFO ii;
    ii.fIcon = TRUE;
    ii.xHotspot = ii.yHotspot = 0;
    ii.hbmMask = mask;
    ii.hbmColor = bmp;
    HICON ic = CreateIconIndirect(&ii);

    DeleteObject(mask);
    DeleteObject(bmp);
    return ic;
}
