/* ============================================================================
 *  hutb —— Linux 壳
 *  libcurl 传输 + 0600 配置文件 + 前台守护（交给 systemd 管理）
 * ========================================================================== */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>

#include <curl/curl.h>

#include "../core.h"

static char g_confPath[512];
static volatile sig_atomic_t g_quit;

/* ================================================================ 输出 */
static int g_isTty;

void plat_write(int lv, const char *line)
{
    if (g_isTty) {
        const char *c = "";
        if (lv == LV_OK)   c = "\033[32m";
        if (lv == LV_ERR)  c = "\033[31m";
        if (lv == LV_WAIT) c = "\033[33m";
        fprintf(stdout, "%s%s\033[0m\n", c, line);
    } else {
        fprintf(stdout, "%s\n", line);
    }
    fflush(stdout);
}

void plat_sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }
long plat_now(void) { return (long)time(NULL); }
void plat_bad_cert_note(void) { }

/* ================================================================ 配置 */
static void conf_path_init(void)
{
    const char *env = getenv("HUTB_CONF");
    if (env && env[0]) { snprintf(g_confPath, sizeof g_confPath, "%s", env); return; }

    if (geteuid() == 0) {
        snprintf(g_confPath, sizeof g_confPath, "/etc/hutb/config");
        return;
    }
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(g_confPath, sizeof g_confPath, "%s/.config/hutb/config", home);
}

static void mkdir_parents(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    char *p = strrchr(tmp, '/');
    if (!p) return;
    *p = 0;
    for (char *q = tmp + 1; *q; q++) {
        if (*q == '/') { *q = 0; mkdir(tmp, 0700); *q = '/'; }
    }
    mkdir(tmp, 0700);
}

int plat_conf_load(Conf *c)
{
    conf_path_init();
    memset(c, 0, sizeof *c);

    struct stat st;
    if (stat(g_confPath, &st) != 0) return 0;
    if ((st.st_mode & 077) != 0)
        core_msg(LV_WAIT, "提示：%s 权限过宽，建议执行 chmod 600", g_confPath);

    FILE *f = fopen(g_confPath, "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        size_t L = strlen(v);
        while (L && (v[L - 1] == '\n' || v[L - 1] == '\r')) v[--L] = 0;
        if (!strcmp(k, "user"))          snprintf(c->user, sizeof c->user, "%s", v);
        else if (!strcmp(k, "pass"))     snprintf(c->pass, sizeof c->pass, "%s", v);
        else if (!strcmp(k, "suffix"))   snprintf(c->suffix, sizeof c->suffix, "%s", v);
        else if (!strcmp(k, "interval")) c->interval = atoi(v) > 0 ? atoi(v) : 60;
    }
    fclose(f);
    if (c->interval < 10) c->interval = 60;
    return c->user[0] && c->pass[0];
}

int plat_conf_save(const Conf *c)
{
    conf_path_init();
    mkdir_parents(g_confPath);

    int fd = open(g_confPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        core_msg(LV_ERR, "无法写入配置文件 %s：%s", g_confPath, strerror(errno));
        return 0;
    }
    fchmod(fd, 0600);

    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); return 0; }
    fprintf(f, "user=%s\npass=%s\nsuffix=%s\ninterval=%d\n",
            c->user, c->pass, c->suffix, c->interval);
    fclose(f);
    return 1;
}

/* 读一行（可关闭回显） */
static void read_line(const char *label, char *out, int n, int secret)
{
    printf("%s", label);
    fflush(stdout);

    struct termios old, noecho;
    int restore = 0;
    if (secret && tcgetattr(STDIN_FILENO, &old) == 0) {
        noecho = old;
        noecho.c_lflag &= ~(tcflag_t)ECHO;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &noecho) == 0) restore = 1;
    }

    if (!fgets(out, n, stdin)) out[0] = 0;
    size_t L = strlen(out);
    while (L && (out[L - 1] == '\n' || out[L - 1] == '\r')) out[--L] = 0;

    if (restore) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
        printf("\n");
    }
}

static int do_setup(Conf *c)
{
    printf("校园网账号配置（保存在 %s，权限 600）\n\n", g_confPath);
    read_line("学号 / 账号: ", c->user, sizeof c->user, 0);
    read_line("密码: ", c->pass, sizeof c->pass, 1);
    read_line("运营商后缀（校园网直接回车；电信 @dx，联通 @lt）: ",
              c->suffix, sizeof c->suffix, 0);
    if (c->interval < 10) c->interval = 60;

    printf("\n本程序只用于登录你自己的账号，不能代替他人认证。\n\n");
    if (!c->user[0] || !c->pass[0]) {
        printf("账号或密码为空，已取消。\n");
        return 0;
    }
    return plat_conf_save(c);
}

/* ================================================================ HTTP */
static const char *g_ua =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36";

static size_t on_write(char *ptr, size_t size, size_t nmemb, void *ud)
{
    HttpResp *r = (HttpResp *)ud;
    size_t add = size * nmemb;
    size_t old = r->body ? strlen(r->body) : 0;
    char *nb = (char *)realloc(r->body, old + add + 1);
    if (!nb) return 0;
    r->body = nb;
    memcpy(r->body + old, ptr, add);
    r->body[old + add] = 0;
    return add;
}

HttpResp plat_http_get(const char *host, int port, int secure, const char *path)
{
    HttpResp r = { 0, NULL };
    char url[HUTB_MAXBUF * 2];

    snprintf(url, sizeof url, "%s://%s:%d%s", secure ? "https" : "http", host, port, path);

    CURL *h = curl_easy_init();
    if (!h) return r;

    struct curl_slist *hdrs = NULL;
    if (secure) hdrs = curl_slist_append(hdrs, "Referer: https://portal.hutb.edu.cn/");
    else        hdrs = curl_slist_append(hdrs, "Accept: */*");

    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_USERAGENT, g_ua);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &r);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    if (secure) {
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);   /* 校园网门户常见自签证书 */
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode rc = curl_easy_perform(h);
    if (rc == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
        r.status = (int)code;
    }
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);
    return r;
}

/* ================================================================ 守护 */
static void on_signal(int sig) { (void)sig; g_quit = 1; }

static void run_daemon(Conf *c)
{
    int down = 0;
    long lastTry = 0;

    core_msg(LV_INFO, "已启动：每 %d 秒检查一次网络", c->interval);

    if (!core_probe_online()) {
        int r = core_login(c, 0);
        if (r == 2) core_msg(LV_ERR, "当前不在校园网内，等待中");
    }

    while (!g_quit) {
        for (int i = 0; i < c->interval * 10 && !g_quit; i++)
            plat_sleep_ms(100);
        if (g_quit) break;

        if (core_probe_online()) {
            if (down) core_msg(LV_OK, "%s", TXT_BACK_ONLINE);
            down = 0;
            continue;
        }

        if (++down < 2) continue;
        if (plat_now() - lastTry < 120) continue;
        lastTry = plat_now();

        core_msg(LV_WAIT, "%s", TXT_RECONNECT);
        int r = core_login(c, 0);
        if (r == 2) { lastTry = plat_now() + 180; down = 0; continue; }  /* 不在校园网，安静等待 */
        if (core_probe_online()) { down = 0; core_msg(LV_OK, "重连成功"); continue; }

        core_msg(LV_WAIT, "连接没有真正恢复，清除旧会话后重试…");
        core_logout();
        plat_sleep_ms(1500);
        if (core_login(c, 1) && core_probe_online()) {
            down = 0;
            core_msg(LV_OK, "重连成功");
        } else {
            core_msg(LV_ERR, "重连失败，稍后自动再试");
        }
    }
    core_msg(LV_INFO, "已停止");
}

/* ================================================================ 入口 */
static void usage(void)
{
    printf("hutb %s —— 校园网自动登录（Linux）\n\n", HUTB_VERSION);
    printf("  sudo hutb -c       配置账号（写入 %s）\n", g_confPath);
    printf("  hutb --daemon      前台守护，交给 systemd 管理\n");
    printf("  hutb -s            查看在线状态\n");
    printf("  hutb -l            登录一次\n");
    printf("  hutb -o            断开连接\n");
    printf("  hutb --diag        输出排错信息（不会提交认证）\n");
    printf("  hutb --selftest    自检\n");
}

int main(int argc, char **argv)
{
    g_isTty = isatty(STDOUT_FILENO);
    conf_path_init();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int argStatus = 0, argLogin = 0, argLogout = 0, argConfig = 0;
    int argTest = 0, argDiag = 0, argDaemon = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-s") || !strcmp(a, "--status"))        argStatus = 1;
        else if (!strcmp(a, "-l") || !strcmp(a, "--login"))    argLogin = 1;
        else if (!strcmp(a, "-o") || !strcmp(a, "--logout"))   argLogout = 1;
        else if (!strcmp(a, "-c") || !strcmp(a, "--config"))   argConfig = 1;
        else if (!strcmp(a, "--daemon"))                       argDaemon = 1;
        else if (!strcmp(a, "--selftest"))                     argTest = 1;
        else if (!strcmp(a, "--diag"))                         argDiag = 1;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help"))     { usage(); return 0; }
        else {
            fprintf(stderr, "未知参数：%s\n\n", a);
            usage();
            return 2;
        }
    }

    if (argTest) return core_selftest(1) ? 1 : 0;

    if (argStatus) {
        Status st;
        if (!core_status(&st)) { core_msg(LV_ERR, "%s", TXT_PORTAL_DOWN); return 1; }
        core_msg(LV_OK, "已连接（地址 %s）", st.ip);
        return 0;
    }

    Conf c;
    int has = plat_conf_load(&c);
    if (!has && argConfig) {
        if (!isatty(STDIN_FILENO)) { core_msg(LV_ERR, "配置需要交互终端，请在终端里运行"); return 2; }
        has = do_setup(&c);
        if (!has) return 2;
    }

    if (argLogout) return core_logout() ? 0 : 1;

    if (!has) {
        if (!argDaemon && isatty(STDIN_FILENO)) {
            has = do_setup(&c);
            if (!has) return 2;
        } else {
            core_msg(LV_ERR, "%s", TXT_NEED_CONF);
            core_msg(LV_INFO, "请先执行：sudo hutb -c");
            return 2;
        }
    }

    if (argDiag) return core_diag(&c) ? 1 : 0;
    if (argLogin) return core_login(&c, 1) ? 0 : 1;
    if (argConfig) return 0;

    if (isatty(STDOUT_FILENO))
        core_msg(LV_INFO, "提示：长期运行请用 systemd（见 install.sh），当前为前台模式");

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    run_daemon(&c);

    curl_global_cleanup();
    return 0;
}
