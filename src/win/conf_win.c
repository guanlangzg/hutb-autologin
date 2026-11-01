/* ============================================================================
 *  Windows 凭据存储：DPAPI 加密的 hutb.dat
 *  单独成文件，便于脱离 GUI 做自动化测试
 * ========================================================================== */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core.h"
#include "conf_win.h"

static wchar_t s_dir[MAX_PATH];
static wchar_t s_dat[MAX_PATH];
static wchar_t s_log[MAX_PATH];
static int     s_init;

void conf_set_dir(const wchar_t *dir)
{
    wcsncpy(s_dir, dir, MAX_PATH - 1);
    _snwprintf(s_dat, MAX_PATH, L"%s\\hutb.dat", s_dir);
    _snwprintf(s_log, MAX_PATH, L"%s\\hutb.log", s_dir);
    s_init = 1;
}

void conf_init(void)
{
    if (s_init) return;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *p = wcsrchr(exe, L'\\');
    if (p) *p = 0;
    conf_set_dir(exe);
}

const wchar_t *conf_dat_path(void) { conf_init(); return s_dat; }
const wchar_t *conf_log_path(void) { conf_init(); return s_log; }

int plat_conf_load(Conf *c)
{
    conf_init();
    memset(c, 0, sizeof *c);

    FILE *f = _wfopen(s_dat, L"rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8192) { fclose(f); return 0; }

    BYTE *blob = (BYTE *)malloc((size_t)sz);
    if (!blob) { fclose(f); return 0; }
    if (fread(blob, 1, (size_t)sz, f) != (size_t)sz) { free(blob); fclose(f); return 0; }
    fclose(f);

    DATA_BLOB in = { (DWORD)sz, blob }, out = { 0, NULL };
    if (!CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
        free(blob);
        return 0;                     /* 换机器/换用户后解不开，属于预期行为 */
    }
    free(blob);

    char *txt = (char *)malloc(out.cbData + 1);
    if (!txt) { LocalFree(out.pbData); return 0; }
    memcpy(txt, out.pbData, out.cbData);
    txt[out.cbData] = 0;
    LocalFree(out.pbData);

    /* 明文格式：user\npass\nsuffix\ninterval */
    char *lines[4] = { txt, NULL, NULL, NULL };
    for (int i = 0; i < 3 && lines[i]; i++) {
        char *nl = strchr(lines[i], '\n');
        if (nl) { *nl = 0; lines[i + 1] = nl + 1; }
    }
    if (lines[0]) snprintf(c->user, sizeof c->user, "%s", lines[0]);
    if (lines[1]) snprintf(c->pass, sizeof c->pass, "%s", lines[1]);
    if (lines[2]) snprintf(c->suffix, sizeof c->suffix, "%s", lines[2]);
    c->interval = lines[3] ? atoi(lines[3]) : 60;
    if (c->interval < 10) c->interval = 60;

    SecureZeroMemory(txt, out.cbData);
    free(txt);
    return c->user[0] && c->pass[0];
}

int plat_conf_save(const Conf *c)
{
    conf_init();
    char txt[512];
    int n = snprintf(txt, sizeof txt, "%s\n%s\n%s\n%d", c->user, c->pass, c->suffix, c->interval);
    if (n <= 0) return 0;

    DATA_BLOB in = { (DWORD)n, (BYTE *)txt }, out = { 0, NULL };
    if (!CryptProtectData(&in, L"hutb", NULL, NULL, NULL, 0, &out)) {
        SecureZeroMemory(txt, sizeof txt);
        return 0;
    }

    FILE *f = _wfopen(s_dat, L"wb");
    if (!f) { LocalFree(out.pbData); SecureZeroMemory(txt, sizeof txt); return 0; }
    fwrite(out.pbData, 1, out.cbData, f);
    fclose(f);
    LocalFree(out.pbData);
    SecureZeroMemory(txt, sizeof txt);
    return 1;
}

/* 兼容旧版明文 hutb.ini：只在没有 hutb.dat 时导入一次 */
void conf_import_legacy_ini(void)
{
    conf_init();
    wchar_t ini[MAX_PATH];
    _snwprintf(ini, MAX_PATH, L"%s\\hutb.ini", s_dir);

    FILE *f = _wfopen(ini, L"rb");
    if (!f) return;

    Conf c;
    memset(&c, 0, sizeof c);
    c.interval = 60;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        size_t L = strlen(v);
        while (L && (v[L - 1] == '\r' || v[L - 1] == '\n')) v[--L] = 0;
        if (!_stricmp(k, "user"))     snprintf(c.user, sizeof c.user, "%s", v);
        if (!_stricmp(k, "pass"))     snprintf(c.pass, sizeof c.pass, "%s", v);
        if (!_stricmp(k, "suffix"))   snprintf(c.suffix, sizeof c.suffix, "%s", v);
        if (!_stricmp(k, "interval")) c.interval = atoi(v) > 0 ? atoi(v) : 60;
    }
    fclose(f);

    if (c.user[0] && c.pass[0]) plat_conf_save(&c);
    SecureZeroMemory(&c, sizeof c);
}
