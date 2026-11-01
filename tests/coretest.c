/* 仅用于本地验证 core 的测试桩：最小平台实现 + 只跑自检 */
#include "../src/core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void plat_write(int lv, const char *line)
{
    (void)lv;
    printf("%s\n", line);
    fflush(stdout);
}

void plat_sleep_ms(int ms) { (void)ms; }
long plat_now(void) { return (long)time(NULL); }
int  plat_conf_load(Conf *c) { (void)c; return 0; }
int  plat_conf_save(const Conf *c) { (void)c; return 0; }
void plat_bad_cert_note(void) { }

HttpResp plat_http_get(const char *host, int port, int secure, const char *path)
{
    (void)host; (void)port; (void)secure; (void)path;
    HttpResp r = { 0, NULL };
    return r;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) {
            int f = core_selftest(1);
            printf(f ? "RESULT: FAIL(%d)\n" : "RESULT: PASS\n", f);
            return f ? 1 : 0;
        }
    }
    printf("usage: coretest --selftest\n");
    return 2;
}
