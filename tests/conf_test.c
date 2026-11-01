/* DPAPI 配置存储的自动化测试（不依赖 GUI） */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/core.h"
#include "../src/win/conf_win.h"

static int fails;

static void check(int cond, const char *name)
{
    printf("%s  %s\n", cond ? "[OK]  " : "[FAIL]", name);
    if (!cond) fails++;
}

/* core.c 里未用到的平台桩，仅为链接通过 */
void plat_write(int lv, const char *line) { (void)lv; (void)line; }
void plat_sleep_ms(int ms) { (void)ms; }
long plat_now(void) { return (long)time(NULL); }
void plat_bad_cert_note(void) { }
HttpResp plat_http_get(const char *host, int port, int secure, const char *path)
{
    (void)host; (void)port; (void)secure; (void)path;
    HttpResp r = { 0, NULL };
    return r;
}

int main(void)
{
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    wcscat(dir, L"hutb_conf_test");
    CreateDirectoryW(dir, NULL);
    conf_set_dir(dir);

    /* 1. 保存 -> 读回 */
    Conf a, b;
    memset(&a, 0, sizeof a);
    strcpy(a.user, "202300000000");
    strcpy(a.pass, "P@ss中文123");
    strcpy(a.suffix, "@dx");
    a.interval = 90;
    check(plat_conf_save(&a) == 1, "保存配置成功");

    memset(&b, 0, sizeof b);
    check(plat_conf_load(&b) == 1, "读回配置成功");
    check(!strcmp(a.user, b.user), "账号一致");
    check(!strcmp(a.pass, b.pass), "密码一致（含中文）");
    check(!strcmp(a.suffix, b.suffix), "运营商后缀一致");
    check(a.interval == b.interval, "检测间隔一致");

    /* 2. 落盘文件里不能出现明文 */
    FILE *f = _wfopen(conf_dat_path(), L"rb");
    check(f != NULL, "配置文件已生成");
    if (f) {
        char raw[8192];
        size_t n = fread(raw, 1, sizeof raw - 1, f);
        raw[n] = 0;
        fclose(f);
        check(strstr(raw, "202300000000") == NULL, "文件里找不到明文账号");
        check(strstr(raw, "P@ss") == NULL, "文件里找不到明文密码");
    }

    /* 3. 旧版 hutb.ini 自动导入 */
    DeleteFileW(conf_dat_path());
    wchar_t ini[MAX_PATH];
    _snwprintf(ini, MAX_PATH, L"%s\\hutb.ini", dir);
    f = _wfopen(ini, L"wb");
    if (f) {
        fputs("user=202300000000\npass=Demo#Pass123\nsuffix=\ninterval=45\n", f);
        fclose(f);
    }
    conf_import_legacy_ini();
    memset(&b, 0, sizeof b);
    check(plat_conf_load(&b) == 1, "旧版 hutb.ini 导入成功");
    check(!strcmp(b.pass, "Demo#Pass123"), "导入的密码正确");
    check(b.interval == 45, "导入的检测间隔正确");

    /* 清理 */
    DeleteFileW(conf_dat_path());
    DeleteFileW(ini);
    RemoveDirectoryW(dir);

    printf(fails ? "\nRESULT: FAIL(%d)\n" : "\nRESULT: PASS\n", fails);
    return fails ? 1 : 0;
}
