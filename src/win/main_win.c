/* ============================================================================
 *  hutb —— Windows 壳
 *  WinHTTP 传输 + DPAPI 凭据 + 首次配置对话框 + 托盘常驻 + 开机自启
 * ========================================================================== */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <commctrl.h>

#include "../core.h"
#include "conf_win.h"
#include "icon_win.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ID_TRAY      1
#define WM_TRAYMSG   (WM_APP + 1)
#define WM_APP_STATE (WM_APP + 2)

#define STATE_IDLE   0   /* 未连接/刚启动 */
#define STATE_OK     1   /* 已连接 */
#define STATE_BAD    2   /* 连不上 */

#define LOG_MAX  (2 * 1024 * 1024)

/* ---- 全局 ---- */
static HINSTANCE g_inst;
static HWND      g_wnd;
static HICON     g_iconOk, g_iconBad, g_iconIdle;
static Conf      g_conf;
static int       g_hasConf;
static int       g_state = STATE_IDLE;
static char      g_stateText[128] = "正在启动…";
static wchar_t   g_exeDir[MAX_PATH];
static int       g_outMode;            /* 0=托盘  1=控制台  2=消息框缓冲 */
static char      g_outBuf[16384];
static size_t    g_outLen;
static volatile int g_quit;
static DWORD     g_lastNotify;          /* 上次弹气泡的时间，避免刷屏 */

/* ============================================================ 编码转换 */
static void u2w(const char *s, wchar_t *w, int n)
{
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
}

static void w2u(const wchar_t *w, char *s, int n)
{
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
}

/* ============================================================== 日志 */
static FILE *g_logf;

static void log_open(void)
{
    FILE *p = _wfopen(conf_log_path(), L"rb");
    if (p) {
        fseek(p, 0, SEEK_END);
        long sz = ftell(p);
        fclose(p);
        if (sz > LOG_MAX) {
            wchar_t old[MAX_PATH];
            _snwprintf(old, MAX_PATH, L"%s.old", conf_log_path());
            DeleteFileW(old);
            MoveFileExW(conf_log_path(), old, MOVEFILE_REPLACE_EXISTING);
        }
    }
    int existed = 0;
    FILE *chk = _wfopen(conf_log_path(), L"rb");
    if (chk) { existed = 1; fclose(chk); }
    g_logf = _wfopen(conf_log_path(), L"a");
    if (g_logf && !existed) {
        static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };  /* 记事本打开不乱码 */
        fwrite(bom, 1, 3, g_logf);
    }
}

void plat_write(int lv, const char *line)
{
    if (g_logf) {
        fprintf(g_logf, "%s\n", line);
        fflush(g_logf);
    }
    if (g_outMode == 1) {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        if (lv == LV_OK)  attr = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        if (lv == LV_ERR) attr = FOREGROUND_RED | FOREGROUND_INTENSITY;
        if (lv == LV_WAIT) attr = FOREGROUND_GREEN | FOREGROUND_RED;
        SetConsoleTextAttribute(h, attr);
        DWORD w = 0;
        WriteFile(h, line, (DWORD)strlen(line), &w, NULL);
        WriteFile(h, "\r\n", 2, &w, NULL);
        SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    } else if (g_outMode == 2) {
        int n = (int)strlen(line);
        if (g_outLen + n + 2 < sizeof g_outBuf) {
            memcpy(g_outBuf + g_outLen, line, (size_t)n);
            g_outLen += n;
            g_outBuf[g_outLen++] = '\n';
            g_outBuf[g_outLen] = 0;
        }
    }
}

void plat_sleep_ms(int ms) { Sleep((DWORD)ms); }
long plat_now(void) { return (long)time(NULL); }
void plat_bad_cert_note(void) { }

/* ============================================================ WinHTTP */
static const wchar_t *g_ua =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    L"(KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36";

HttpResp plat_http_get(const char *host, int port, int secure, const char *path)
{
    HttpResp r = { 0, NULL };
    wchar_t whost[256], wpath[HUTB_MAXBUF];

    u2w(host, whost, 256);
    u2w(path, wpath, HUTB_MAXBUF);

    HINTERNET hs = WinHttpOpen(g_ua, WINHTTP_ACCESS_TYPE_NO_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hs) return r;
    WinHttpSetTimeouts(hs, 8000, 8000, 8000, 8000);

    HINTERNET hc = WinHttpConnect(hs, whost, (INTERNET_PORT)port, 0);
    if (!hc) { WinHttpCloseHandle(hs); return r; }

    HINTERNET hr = WinHttpOpenRequest(hc, L"GET", wpath, NULL, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hr) { WinHttpCloseHandle(hc); WinHttpCloseHandle(hs); return r; }

    if (!secure) {
        WinHttpAddRequestHeaders(hr, L"Accept: */*\r\n", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    } else {
        DWORD sf = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                   SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hr, WINHTTP_OPTION_SECURITY_FLAGS, &sf, sizeof sf);
        WinHttpAddRequestHeaders(hr, L"Referer: https://portal.hutb.edu.cn/\r\n",
                                 (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(hr, NULL)) {
        DWORD st = 0, sz = sizeof st;
        WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            NULL, &st, &sz, NULL);
        r.status = (int)st;

        size_t cap = 0, len = 0;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hr, &avail) || avail == 0) break;
            if (len + avail + 1 > cap) {
                size_t nc = (len + avail + 1) * 2;
                char *nb = (char *)realloc(r.body, nc);
                if (!nb) break;
                r.body = nb;
                cap = nc;
            }
            DWORD got = 0;
            if (!WinHttpReadData(hr, r.body + len, avail, &got) || got == 0) break;
            len += got;
        }
        if (r.body) r.body[len] = 0;
    }
    WinHttpCloseHandle(hr);
    WinHttpCloseHandle(hc);
    WinHttpCloseHandle(hs);
    return r;
}

/* ============================================================ 托盘操作 */
static int tray_add(void)
{
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = g_wnd;
    nid.uID = ID_TRAY;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYMSG;
    nid.hIcon = g_iconIdle ? g_iconIdle : g_iconBad;
    wchar_t tip[128];
    u2w(g_stateText, tip, 128);
    wcsncpy(nid.szTip, tip, 127);
    BOOL ok = Shell_NotifyIconW(NIM_ADD, &nid);
    if (!ok) core_msg(LV_ERR, "托盘图标添加失败，程序仍在后台运行");
    return ok;
}

static void tray_update(int balloon)
{
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = g_wnd;
    nid.uID = ID_TRAY;
    nid.uFlags = NIF_ICON | NIF_TIP;

    if (g_state == STATE_OK) nid.hIcon = g_iconOk;
    else if (g_state == STATE_BAD) nid.hIcon = g_iconBad;
    else nid.hIcon = g_iconIdle;

    wchar_t tip[128];
    u2w(g_stateText, tip, 128);
    wcsncpy(nid.szTip, tip, 127);

    if (balloon) {
        nid.uFlags |= NIF_INFO;
        wcsncpy(nid.szInfo, tip, 255);
        wcsncpy(nid.szInfoTitle, L"校园网自动登录", 63);
        nid.dwInfoFlags = NIIF_INFO;
    }
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void set_state(int st, const char *text, int balloon)
{
    g_state = st;
    if (text) snprintf(g_stateText, sizeof g_stateText, "%s", text);
    tray_update(balloon);
}

/* 只在状态真正变化且间隔足够时才弹气泡，避免刷屏 */
static void notify_change(int st, const char *text)
{
    DWORD now = GetTickCount();
    int changed = (st != g_state);
    int spaced = (now - g_lastNotify) > 60000;
    set_state(st, text, 0);
    if (changed && spaced) {
        g_lastNotify = now;
        tray_update(1);
    }
}

/* ============================================================ 对话框 */
static HWND g_dlg;
static HWND g_edUser, g_edPass, g_edSuffix;
static int  g_dlgOk;
/* 点确定时窗口马上销毁，必须先在这里把值取出来 */
static char g_dlgUser[128], g_dlgPass[256], g_dlgSuf[64];

#define ID_EDIT_USER   101
#define ID_EDIT_PASS   102
#define ID_EDIT_SUF    103
#define ID_BTN_OK      104
#define ID_BTN_CANCEL  105

static HFONT g_fontLabel, g_fontTitle, g_fontSmall;

static void make_fonts(void)
{
    NONCLIENTMETRICSW ncm;
    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);

    LOGFONTW lf = ncm.lfMessageFont;
    g_fontLabel = CreateFontIndirectW(&lf);

    lf.lfHeight = (LONG)(lf.lfHeight * 1.5);
    lf.lfWeight = FW_BOLD;
    g_fontTitle = CreateFontIndirectW(&lf);

    lf = ncm.lfMessageFont;
    lf.lfHeight = (LONG)(lf.lfHeight * 1.1);
    g_fontSmall = CreateFontIndirectW(&lf);
}

static HWND mk(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style,
               int x, int y, int w, int h, int id)
{
    HWND hh = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                              x, y, w, h, parent, (HMENU)(INT_PTR)id, g_inst, NULL);
    if (hh) SendMessageW(hh, WM_SETFONT, (WPARAM)g_fontLabel, TRUE);
    return hh;
}

static LRESULT CALLBACK dlg_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(w) == ID_BTN_OK) {
            wchar_t bu[128], bp[256], bs[64];
            GetWindowTextW(g_edUser, bu, 128);
            GetWindowTextW(g_edPass, bp, 256);
            GetWindowTextW(g_edSuffix, bs, 64);
            if (!bu[0]) { MessageBoxW(h, L"请填写学号（或账号）", L"校园网自动登录", MB_ICONWARNING); return 0; }
            if (!bp[0]) { MessageBoxW(h, L"请填写密码", L"校园网自动登录", MB_ICONWARNING); return 0; }
            w2u(bu, g_dlgUser, sizeof g_dlgUser);
            w2u(bp, g_dlgPass, sizeof g_dlgPass);
            w2u(bs, g_dlgSuf, sizeof g_dlgSuf);
            SecureZeroMemory(bp, sizeof bp);
            g_dlgOk = 1;
            DestroyWindow(h);
            return 0;
        }
        if (LOWORD(w) == ID_BTN_CANCEL) { DestroyWindow(h); return 0; }
        if (HIWORD(w) == BN_CLICKED) return 0;
        break;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_dlg = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static int show_config_dialog(int first)
{
    static int registered;
    if (!registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = dlg_proc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"HutbConfig";
        RegisterClassExW(&wc);
        registered = 1;
    }

    int W = 460, H = first ? 400 : 360;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - W) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - H) / 2;

    g_dlgOk = 0;
    g_dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"HutbConfig",
                            first ? L"校园网自动登录 · 第一次配置" : L"修改账号",
                            WS_POPUP | WS_CAPTION | WS_SYSMENU,
                            sx, sy, W, H, NULL, NULL, g_inst, NULL);
    if (!g_dlg) return 0;

    HWND t = mk(g_dlg, L"STATIC", L"填写一次，之后自动登录", WS_VISIBLE,
                24, 16, 400, 26, 0);
    SendMessageW(t, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

    mk(g_dlg, L"STATIC", L"学号 / 账号", 0, 24, 60, 120, 20, 0);
    g_edUser = mk(g_dlg, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 24, 82, 400, 26, ID_EDIT_USER);

    mk(g_dlg, L"STATIC", L"密码", 0, 24, 120, 120, 20, 0);
    g_edPass = mk(g_dlg, L"EDIT", L"", WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL, 24, 142, 400, 26, ID_EDIT_PASS);

    mk(g_dlg, L"STATIC", L"运营商（校园网用户不用填；电信填 @dx，联通填 @lt）", 0,
       24, 180, 400, 20, 0);
    g_edSuffix = mk(g_dlg, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 24, 202, 400, 26, ID_EDIT_SUF);

    HWND note = mk(g_dlg, L"STATIC",
                   L"说明：本程序只用于登录你自己的校园网账号，不能代替他人认证。\r\n"
                   L"账号密码以加密形式保存在本机，不会发送到除学校门户以外的任何地方。",
                   SS_LEFT, 24, 240, 400, 60, 0);
    SendMessageW(note, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    mk(g_dlg, L"BUTTON", L"确定", BS_DEFPUSHBUTTON, 230, first ? 320 : 290, 90, 30, ID_BTN_OK);
    mk(g_dlg, L"BUTTON", L"取消", BS_PUSHBUTTON, 330, first ? 320 : 290, 90, 30, ID_BTN_CANCEL);

    if (g_hasConf) {
        wchar_t w[256];
        u2w(g_conf.user, w, 256);   SetWindowTextW(g_edUser, w);
        u2w(g_conf.pass, w, 256);   SetWindowTextW(g_edPass, w);
        u2w(g_conf.suffix, w, 256); SetWindowTextW(g_edSuffix, w);
    }

    ShowWindow(g_dlg, SW_SHOW);
    UpdateWindow(g_dlg);
    SetForegroundWindow(g_dlg);
    SetFocus(g_edUser);

    MSG msg;
    while (g_dlg && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(g_dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (!g_dlgOk) return 0;

    snprintf(g_conf.user, sizeof g_conf.user, "%s", g_dlgUser);
    snprintf(g_conf.pass, sizeof g_conf.pass, "%s", g_dlgPass);
    snprintf(g_conf.suffix, sizeof g_conf.suffix, "%s", g_dlgSuf);
    if (g_conf.interval < 10) g_conf.interval = 60;
    SecureZeroMemory(g_dlgPass, sizeof g_dlgPass);
    g_hasConf = plat_conf_save(&g_conf);
    if (!g_hasConf) MessageBoxW(NULL, L"保存配置失败，程序无法继续", L"校园网自动登录", MB_ICONERROR);
    return g_hasConf;
}

/* ============================================================ 开机自启 */
#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VAL L"HUTB AutoLogin"

static void autostart_set(int on)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wchar_t cmd[MAX_PATH + 8];
        _snwprintf(cmd, MAX_PATH + 8, L"\"%s\"", exe);
        RegSetValueExW(k, RUN_VAL, 0, REG_SZ, (const BYTE *)cmd,
                       (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, RUN_VAL);
    }
    RegCloseKey(k);
}

/* ============================================================ 守护线程 */
static void run_daemon(void)
{
    int down = 0;
    long lastTry = 0;

    core_msg(LV_INFO, "守护已启动：每 %d 秒检查一次网络", g_conf.interval);

    if (!core_probe_online()) {
        set_state(STATE_IDLE, "正在连接校园网…", 0);
        int r = core_login(&g_conf, 0);
        if (r == 1) set_state(STATE_OK, "已连接，可以正常上网", 0);
        else if (r == 2) set_state(STATE_BAD, "当前不在校园网内", 0);
        else set_state(STATE_BAD, "连接失败，稍后重试", 0);
    } else {
        set_state(STATE_OK, "已连接，可以正常上网", 0);
    }

    while (!g_quit) {
        Sleep((DWORD)g_conf.interval * 1000);
        if (g_quit) break;

        if (core_probe_online()) {
            if (down) {
                core_msg(LV_OK, "%s", TXT_BACK_ONLINE);
                notify_change(STATE_OK, "已连接，可以正常上网");
            }
            down = 0;
            continue;
        }

        if (++down < 2) continue;              /* 连续两次才算真断 */
        if (plat_now() - lastTry < 120) continue;
        lastTry = plat_now();

        core_msg(LV_WAIT, "%s", TXT_RECONNECT);
        set_state(STATE_IDLE, "网络断开，正在重连…", 0);

        int r = core_login(&g_conf, 0);
        if (r == 2) {
            notify_change(STATE_BAD, "当前不在校园网内");
            lastTry = plat_now() + 180;      /* 门户都连不上，5 分钟后再试 */
            down = 0;
            continue;
        }
        if (core_probe_online()) {
            core_msg(LV_OK, "重连成功");
            down = 0;
            notify_change(STATE_OK, "已连接，可以正常上网");
            continue;
        }

        /* 认证说通了但流量仍不通：清掉僵死会话再试一次 */
        core_msg(LV_WAIT, "连接没有真正恢复，清除旧会话后重试…");
        core_logout();
        Sleep(1500);
        if (core_login(&g_conf, 1) && core_probe_online()) {
            core_msg(LV_OK, "重连成功");
            down = 0;
            notify_change(STATE_OK, "已连接，可以正常上网");
        } else {
            notify_change(STATE_BAD, "重连失败，稍后自动再试");
        }
    }
    core_msg(LV_INFO, "守护已停止");
}

static DWORD WINAPI daemon_thread(LPVOID p)
{
    (void)p;
    run_daemon();
    return 0;
}

/* ============================================================ 托盘菜单 */
static void tray_menu(void)
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"校园网自动登录");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);

    wchar_t st[160];
    u2w(g_stateText, st, 160);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, st);

    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 1001, L"立即重连");
    AppendMenuW(m, MF_STRING, 1002, L"修改账号");
    AppendMenuW(m, MF_STRING, 1003, L"打开日志");
    AppendMenuW(m, MF_STRING, 1004, L"打开程序目录");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 1005, L"退出");

    SetForegroundWindow(g_wnd);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                  pt.x, pt.y, 0, g_wnd, NULL);
    DestroyMenu(m);

    switch (cmd) {
    case 1001:
        set_state(STATE_IDLE, "正在重连…", 0);
        if (core_login(&g_conf, 1) && core_probe_online())
            notify_change(STATE_OK, "已连接，可以正常上网");
        else
            notify_change(STATE_BAD, "重连失败，稍后自动再试");
        break;
    case 1002:
        if (show_config_dialog(0)) {
            set_state(STATE_IDLE, "账号已更新，正在重连…", 0);
            if (core_login(&g_conf, 1) && core_probe_online())
                notify_change(STATE_OK, "已连接，可以正常上网");
            else
                notify_change(STATE_BAD, "连接失败，稍后自动再试");
        }
        break;
    case 1003:
        ShellExecuteW(NULL, L"open", L"notepad.exe", conf_log_path(), NULL, SW_SHOWNORMAL);
        break;
    case 1004:
        ShellExecuteW(NULL, L"open", g_exeDir, NULL, NULL, SW_SHOWNORMAL);
        break;
    case 1005:
        g_quit = 1;
        DestroyWindow(g_wnd);
        break;
    }
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_TRAYMSG:
        if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU) tray_menu();
        if (LOWORD(l) == WM_LBUTTONDBLCLK) {
            wchar_t st[160];
            u2w(g_stateText, st, 160);
            MessageBoxW(NULL, st, L"校园网自动登录", MB_ICONINFORMATION);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ============================================================ 入口 */
static void msgbox_out(const char *title, const char *body)
{
    wchar_t wt[128], wb[16384];
    u2w(title, wt, 128);
    u2w(body, wb, 16384);
    MessageBoxW(NULL, wb, wt, MB_ICONINFORMATION | MB_OK);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev; (void)cmd; (void)show;
    g_inst = hInst;

    SetConsoleOutputCP(CP_UTF8);
    conf_init();
    {
        wchar_t dp[MAX_PATH];
        wcsncpy(dp, conf_dat_path(), MAX_PATH - 1);
        wchar_t *sl = wcsrchr(dp, 0x5C);
        if (sl) *sl = 0;
        wcsncpy(g_exeDir, dp, MAX_PATH - 1);
    }
    log_open();

    /* 命令行启动时输出到终端；被重定向到管道/文件（如 CI）时也照常写 */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        g_outMode = 1;
    } else {
        HANDLE so = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD ft = so ? GetFileType(so) : FILE_TYPE_UNKNOWN;
        if (ft == FILE_TYPE_PIPE || ft == FILE_TYPE_DISK) g_outMode = 1;
    }

    /* 参数 */
    int argStatus = 0, argLogin = 0, argLogout = 0, argConfig = 0, argTest = 0, argDiag = 0;
    {
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; i++) {
            char a[64];
            w2u(argv[i], a, sizeof a);
            if (!strcmp(a, "-s") || !strcmp(a, "--status"))       argStatus = 1;
            else if (!strcmp(a, "-l") || !strcmp(a, "--login"))   argLogin = 1;
            else if (!strcmp(a, "-o") || !strcmp(a, "--logout"))  argLogout = 1;
            else if (!strcmp(a, "-c") || !strcmp(a, "--config"))  argConfig = 1;
            else if (!strcmp(a, "--selftest"))                    argTest = 1;
            else if (!strcmp(a, "--diag"))                        argDiag = 1;
            else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
                if (g_outMode) {
                    core_msg(LV_INFO, "hutb %s —— 校园网自动登录", HUTB_VERSION);
                    core_msg(LV_INFO, "直接双击：后台常驻，断网自动重连");
                    core_msg(LV_INFO, "-s 查看状态   -l 登录一次   -o 断开   -c 修改账号");
                    core_msg(LV_INFO, "--diag 输出排错信息   --selftest 自检");
                } else {
                    msgbox_out("校园网自动登录",
                               "直接双击运行即可，程序会在后台自动保持联网。\n\n"
                               "命令行参数：\n"
                               "  -s 查看状态    -l 登录一次\n"
                               "  -o 断开连接    -c 修改账号\n"
                               "  --diag 输出排错信息");
                }
                return 0;
            }
        }
        if (argv) LocalFree(argv);
    }

    if (argTest) return core_selftest(g_outMode == 1) ? 1 : 0;

    /* 命令行模式：没有控制台就把输出收进消息框 */
    if (argStatus || argLogin || argLogout || argConfig || argDiag) {
        if (!g_outMode) g_outMode = 2;
    }

    g_hasConf = plat_conf_load(&g_conf);
    if (!g_hasConf) conf_import_legacy_ini();
    if (!g_hasConf) g_hasConf = plat_conf_load(&g_conf);

    if (argStatus) {
        Status st;
        if (!core_status(&st)) core_msg(LV_ERR, "%s", TXT_PORTAL_DOWN);
        else core_msg(LV_OK, "已连接（地址 %s）", st.ip);
        if (g_outMode == 2) msgbox_out("校园网状态", g_outBuf);
        return 0;
    }
    if (argLogout) {
        core_logout();
        if (g_outMode == 2) msgbox_out("校园网自动登录", g_outBuf);
        return 0;
    }
    if (argDiag) {
        if (!g_hasConf) { core_msg(LV_ERR, "%s", TXT_NEED_CONF); }
        else core_diag(&g_conf);
        if (g_outMode == 2) msgbox_out("排错信息", g_outBuf);
        return 0;
    }
    if (argLogin) {
        if (!g_hasConf) { core_msg(LV_ERR, "%s", TXT_NEED_CONF); }
        else core_login(&g_conf, 1);
        if (g_outMode == 2) msgbox_out("校园网自动登录", g_outBuf);
        return 0;
    }

    /* ---- 以下进入托盘常驻 ---- */
    g_outMode = 0;

    HANDLE one = CreateMutexW(NULL, TRUE, L"Global\\HUTB_AUTOLOGIN");
    if (one && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"程序已经在运行了，请看屏幕右下角的托盘图标。",
                    L"校园网自动登录", MB_ICONINFORMATION);
        return 0;
    }

    g_iconOk   = hutb_make_dot_icon(RGB(46, 204, 113));
    g_iconBad  = hutb_make_dot_icon(RGB(231, 76, 60));
    g_iconIdle = hutb_make_dot_icon(RGB(241, 196, 15));
    make_fonts();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = g_inst;
    wc.lpszClassName = L"HutbMain";
    RegisterClassExW(&wc);

    g_wnd = CreateWindowExW(0, L"HutbMain", L"校园网自动登录", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, NULL, g_inst, NULL);
    if (!g_wnd) return 1;

    if (!g_hasConf) {
        if (!show_config_dialog(1)) return 0;
    }
    if (argConfig) {
        if (!show_config_dialog(0)) return 0;
    }

    autostart_set(1);        /* 首次跑通后自动设置开机自启 */
    if (tray_add()) core_msg(LV_OK, "已在右下角托盘常驻");

    core_msg(LV_INFO, "hutb %s 启动，账号 %s%s", HUTB_VERSION, g_conf.user, g_conf.suffix);

    HANDLE th = CreateThread(NULL, 0, daemon_thread, NULL, 0, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_quit = 1;
    if (th) {
        WaitForSingleObject(th, 5000);
        CloseHandle(th);
    }
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = g_wnd;
    nid.uID = ID_TRAY;
    Shell_NotifyIconW(NIM_DELETE, &nid);

    if (g_iconOk) DestroyIcon(g_iconOk);
    if (g_iconBad) DestroyIcon(g_iconBad);
    if (g_iconIdle) DestroyIcon(g_iconIdle);
    if (g_logf) fclose(g_logf);
    if (one) CloseHandle(one);
    return 0;
}
