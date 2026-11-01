/* 把运行时绘制的托盘图标导出成 PNG，用于人工确认图标不是空白/全黑 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/win/icon_win.h"

/* 极简 PNG 写出（调色板压缩），只为了看一眼图标长什么样 */
static void save_png(const char *path, const unsigned char *rgba, int w, int h)
{
    /* 用 zlib 的 stored 块，省得实现 deflate */
    size_t raw_sz = (size_t)h * (1 + w * 4);
    unsigned char *raw = (unsigned char *)malloc(raw_sz);
    for (int y = 0; y < h; y++) {
        raw[y * (1 + w * 4)] = 0;
        memcpy(raw + y * (1 + w * 4) + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }

    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); return; }

    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    fwrite(sig, 1, 8, f);

    const char *chunk_type = "IHDR";
    (void)chunk_type;
    /* IHDR */
    unsigned char ihdr[13];
    ihdr[0] = (unsigned char)(w >> 24); ihdr[1] = (unsigned char)(w >> 16);
    ihdr[2] = (unsigned char)(w >> 8);  ihdr[3] = (unsigned char)w;
    ihdr[4] = (unsigned char)(h >> 24); ihdr[5] = (unsigned char)(h >> 16);
    ihdr[6] = (unsigned char)(h >> 8);  ihdr[7] = (unsigned char)h;
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

    /* 这里用系统自带的压缩太麻烦，改成直接写 BMP，简单可靠 */
    fclose(f);
    remove(path);
    free(raw);

    /* BMP：24 位，自下而上 */
    int line = (w * 3 + 3) & ~3;
    size_t data_sz = (size_t)line * h;
    unsigned char *buf = (unsigned char *)calloc(54 + data_sz, 1);
    buf[0] = 'B'; buf[1] = 'M';
    *(unsigned int *)(buf + 2) = (unsigned int)(54 + data_sz);
    *(unsigned int *)(buf + 10) = 54;
    *(unsigned int *)(buf + 14) = 40;
    *(int *)(buf + 18) = w;
    *(int *)(buf + 22) = h;
    *(unsigned short *)(buf + 26) = 1;
    *(unsigned short *)(buf + 28) = 24;

    for (int y = 0; y < h; y++) {
        unsigned char *dst = buf + 54 + (size_t)(h - 1 - y) * line;
        const unsigned char *src = rgba + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            unsigned char a = src[x * 4 + 3];
            /* 透明处铺成灰白棋盘格，便于肉眼判断轮廓 */
            int checker = ((x / 4) + (y / 4)) & 1;
            unsigned char bg = checker ? 0xEE : 0xCC;
            unsigned char r = src[x * 4 + 0], g = src[x * 4 + 1], b = src[x * 4 + 2];
            unsigned char R = a ? r : bg, G = a ? g : bg, B = a ? b : bg;
            dst[x * 3 + 0] = B;
            dst[x * 3 + 1] = G;
            dst[x * 3 + 2] = R;
        }
    }

    f = fopen(path, "wb");
    if (!f) { free(buf); return; }
    fwrite(buf, 1, 54 + data_sz, f);
    fclose(f);
    free(buf);
}

static void dump_icon(HICON ic, const char *out)
{
    if (!ic) { printf("图标创建失败\n"); return; }
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
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(hdc, bmp);
    memset(bits, 0, S * S * 4);
    DrawIconEx(hdc, 0, 0, ic, S, S, 0, NULL, DI_NORMAL);
    SelectObject(hdc, old);
    DeleteDC(hdc);

    /* DI_NORMAL 会做 alpha 混合，结果不透明；下面按非黑判断透明度 */
    unsigned char rgba[16 * 16 * 4];
    BYTE *px = (BYTE *)bits;
    int opaque = 0;
    for (int i = 0; i < S * S; i++) {
        BYTE b = px[i * 4 + 0], g = px[i * 4 + 1], r = px[i * 4 + 2];
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = (r | g | b) ? 255 : 0;
        if (r | g | b) opaque++;
    }
    printf("%s: 不透明像素 %d/%d\n", out, opaque, S * S);
    save_png(out, rgba, S, S);
    DeleteObject(bmp);
}

int main(void)
{
    HICON a = hutb_make_dot_icon(RGB(46, 204, 113));
    HICON b = hutb_make_dot_icon(RGB(231, 76, 60));
    HICON c = hutb_make_dot_icon(RGB(241, 196, 15));
    dump_icon(a, "icon_ok.bmp");
    dump_icon(b, "icon_bad.bmp");
    dump_icon(c, "icon_idle.bmp");
    if (a) DestroyIcon(a);
    if (b) DestroyIcon(b);
    if (c) DestroyIcon(c);
    return 0;
}
