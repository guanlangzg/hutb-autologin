/* ============================================================================
 *  hutb core —— 与平台无关的门户认证逻辑
 * ========================================================================== */
#include "core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>

/* ============================================================ AES-128-ECB */
static const uint8_t SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static uint8_t xtime(uint8_t a)
{
    return (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1b : 0));
}

static void aes_expand(const uint8_t key[16], uint8_t rk[176])
{
    uint8_t rcon = 1;
    memcpy(rk, key, 16);
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4];
        memcpy(t, rk + i - 4, 4);
        if (i % 16 == 0) {
            uint8_t a = t[0];
            t[0] = (uint8_t)(SBOX[t[1]] ^ rcon);
            t[1] = SBOX[t[2]];
            t[2] = SBOX[t[3]];
            t[3] = SBOX[a];
            rcon = xtime(rcon);
        }
        for (int j = 0; j < 4; j++)
            rk[i + j] = (uint8_t)(rk[i - 16 + j] ^ t[j]);
    }
}

static void aes_block(const uint8_t in[16], uint8_t out[16], const uint8_t rk[176])
{
    uint8_t s[16], t[16];
    for (int i = 0; i < 16; i++) s[i] = (uint8_t)(in[i] ^ rk[i]);

    for (int round = 1; round <= 10; round++) {
        for (int i = 0; i < 16; i++) s[i] = SBOX[s[i]];

        /* ShiftRows：state 按列存放 */
        t[0]  = s[0];  t[4]  = s[4];  t[8]  = s[8];  t[12] = s[12];
        t[1]  = s[5];  t[5]  = s[9];  t[9]  = s[13]; t[13] = s[1];
        t[2]  = s[10]; t[6]  = s[14]; t[10] = s[2];  t[14] = s[6];
        t[3]  = s[15]; t[7]  = s[3];  t[11] = s[7];  t[15] = s[11];
        memcpy(s, t, 16);

        if (round != 10) {   /* MixColumns */
            for (int c = 0; c < 4; c++) {
                uint8_t *p = s + 4 * c;
                uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
                uint8_t all = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
                p[0] ^= (uint8_t)(all ^ xtime((uint8_t)(a0 ^ a1)));
                p[1] ^= (uint8_t)(all ^ xtime((uint8_t)(a1 ^ a2)));
                p[2] ^= (uint8_t)(all ^ xtime((uint8_t)(a2 ^ a3)));
                p[3] ^= (uint8_t)(all ^ xtime((uint8_t)(a3 ^ a0)));
            }
        }
        for (int i = 0; i < 16; i++) s[i] ^= rk[round * 16 + i];
    }
    memcpy(out, s, 16);
}

static int aes_ecb(const char *plain, int n, uint8_t *out)
{
    uint8_t rk[176];
    aes_expand((const uint8_t *)AES_KEY_TEXT, rk);

    int pad = 16 - (n % 16);
    int total = n + pad;
    uint8_t *buf = (uint8_t *)malloc((size_t)total);
    if (!buf) return 0;
    memcpy(buf, plain, (size_t)n);
    memset(buf + n, pad, (size_t)pad);

    for (int i = 0; i < total; i += 16)
        aes_block(buf + i, out + i, rk);

    free(buf);
    return total;
}

/* ================================================================ base64 */
static const char B64C[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_enc(const uint8_t *in, int n, char *out, int outn)
{
    int i, o = 0;
    if (outn < ((n + 2) / 3) * 4 + 1) return 0;
    for (i = 0; i + 2 < n; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64C[(v >> 18) & 63];
        out[o++] = B64C[(v >> 12) & 63];
        out[o++] = B64C[(v >> 6) & 63];
        out[o++] = B64C[v & 63];
    }
    if (i < n) {
        int rem = n - i;
        uint32_t v = (uint32_t)in[i] << 16;
        if (rem == 2) v |= (uint32_t)in[i + 1] << 8;
        out[o++] = B64C[(v >> 18) & 63];
        out[o++] = B64C[(v >> 12) & 63];
        out[o++] = rem == 2 ? B64C[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    out[o] = 0;
    return o;
}

int b64_str(const char *in, char *out, int outn)
{
    return b64_enc((const uint8_t *)in, (int)strlen(in), out, outn);
}

int aes_en(const char *text, char *out, int outn)
{
    uint8_t ct[HUTB_MAXBUF];
    int n = aes_ecb(text, (int)strlen(text), ct);
    if (n <= 0) return 0;
    return b64_enc(ct, n, out, outn);
}

/* ============================================================== URL 编码 */
int url_enc(const char *in, char *out, int outn)
{
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        unsigned char c = *p;
        int keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        int need = keep ? 1 : 3;
        if (o + need >= outn) return 0;
        if (keep) {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
    return o;
}

/* ================================================================ JSON */
int jget(const char *js, const char *key, char *out, int outn)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(js, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    int o = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && o < outn - 1) {
            if (*p == '\\' && p[1]) p++;
            out[o++] = *p++;
        }
        out[o] = 0;
        return 1;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && o < outn - 1)
        out[o++] = *p++;
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\r' || out[o - 1] == '\n'))
        o--;
    out[o] = 0;
    return 1;
}

const char *jsonp_body(const char *s)
{
    const char *p = strchr(s, '(');
    return p ? p + 1 : s;
}

static void jesc(const char *s, char *out, int outn)
{
    int o = 0;
    for (; *s && o < outn - 8; s++) {
        if (*s == '"' || *s == '\\') { out[o++] = '\\'; out[o++] = *s; }
        else if ((unsigned char)*s < 0x20) o += sprintf(out + o, "\\u%04x", *s);
        else out[o++] = *s;
    }
    out[o] = 0;
}

/* =============================================================== 输出 */
static const char *LV_PREFIX[] = { "\xE2\x9C\x93 ", "\xE2\x9C\x97 ", "\xC2\xB7 ", "" };
/* ✓ ✗ · 空 */

void core_msg(int lv, const char *fmt, ...)
{
    char buf[1024];
    char ts[32];
    va_list ap;
    time_t now = time(NULL);

    strftime(ts, sizeof ts, "%H:%M:%S", localtime(&now));

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    char line[1200];
    snprintf(line, sizeof line, "[%s] %s%s", ts, LV_PREFIX[lv], buf);
    plat_write(lv, line);
}

/* ============================================================ 门户交互 */
static int rnd(void)
{
    return rand() % 10000 + 500;
}

int core_status(Status *st)
{
    char path[HUTB_MAXBUF];
    memset(st, 0, sizeof *st);

    snprintf(path, sizeof path,
             "/drcom/chkstatus?callback=dr%d&jsVersion=%s&v=%d&lang=zh",
             rnd(), PORTAL_JS_OLD, rnd());

    HttpResp r = plat_http_get(PORTAL_HOST, 443, 1, path);
    if (r.status != 200 || !r.body) { free(r.body); return 0; }

    const char *js = jsonp_body(r.body);
    char v[128];
    if (jget(js, "result", v, sizeof v)) st->online = atoi(v) == 1;
    if (jget(js, "ss5", v, sizeof v) && v[0])      snprintf(st->ip, sizeof st->ip, "%s", v);
    else if (jget(js, "v46ip", v, sizeof v))       snprintf(st->ip, sizeof st->ip, "%s", v);
    if (jget(js, "ss4", v, sizeof v))              snprintf(st->mac, sizeof st->mac, "%s", v);
    free(r.body);
    return st->ip[0] != 0;
}

int core_load_cfg(const char *ip, const char *mac, Cfg *cf)
{
    char b64ip[128], path[HUTB_MAXBUF], v[128];
    memset(cf, 0, sizeof *cf);
    cf->login_method = 1;

    if (!b64_str(ip, b64ip, sizeof b64ip)) return 0;

    snprintf(path, sizeof path,
             "/eportal/portal/page/loadConfig?callback=dr%d&program_index=&wlan_vlan_id=0"
             "&wlan_user_ip=%s&wlan_user_ipv6=&wlan_user_ssid=&wlan_user_areaid="
             "&wlan_ac_ip=&wlan_ap_mac=%s&gw_id=%s&jsVersion=%s&v=%d&lang=zh",
             rnd(), b64ip, mac, mac, PORTAL_JS_OLD, rnd());

    HttpResp r = plat_http_get(PORTAL_HOST, PORTAL_EP_PORT, 1, path);
    if (r.status != 200 || !r.body) { free(r.body); return 0; }

    const char *js = jsonp_body(r.body);
    if (jget(js, "rcn", v, sizeof v))             snprintf(cf->rcn, sizeof cf->rcn, "%s", v);
    if (jget(js, "login_method", v, sizeof v))    cf->login_method = atoi(v);
    if (jget(js, "account_prefix", v, sizeof v))  cf->account_prefix = atoi(v);
    if (jget(js, "ac_logout", v, sizeof v))       cf->ac_logout = atoi(v);
    if (jget(js, "register_mode", v, sizeof v))   cf->register_mode = atoi(v);
    if (jget(js, "en_md5", v, sizeof v))          cf->en_md5 = atoi(v);
    free(r.body);
    return cf->rcn[0] != 0;
}

/* 登录JSON的唯一来源：自检也用它，保证测的就是跑的那份 */
int core_build_payload(const Conf *c, const char *ip, const char *mac,
                       const Cfg *cf, int page_new, char *out, int outn)
{
    char acct[192], pwd[256];

    snprintf(acct, sizeof acct, "%s%s%s",
             cf->account_prefix ? ",0," : "", c->user, c->suffix);
    jesc(c->pass, pwd, sizeof pwd);

    return snprintf(out, (size_t)outn,
                    "{\"login_method\":%d,\"user_account\":\"%s\",\"user_password\":\"%s\","
                    "\"wlan_user_ip\":\"%s\",\"wlan_user_ipv6\":\"\",\"wlan_user_mac\":\"%s\","
                    "\"wlan_ac_ip\":\"\",\"wlan_ac_name\":\"\",\"jsVersion\":\"%s\","
                    "\"login_t\":\"0\",\"js_status\":\"0\",\"is_page\":\"1\",\"is_page_new\":%d,"
                    "\"terminal_type\":1,\"lang\":\"zh-cn\",\"rcn\":\"%s\"}",
                    cf->login_method, acct, pwd, ip, mac, PORTAL_JS_VER, page_new, cf->rcn);
}

/* 登录前上报，失败不影响登录 */
static void query_page_set(const char *account, const char *ip, const char *mac)
{
    char e_acc[512], e_ip[512], e_mac[512], e_ua[1024], e_t[128];
    char inner[HUTB_MAXBUF], enc[HUTB_MAXBUF * 2], uenc[HUTB_MAXBUF * 3];
    char path[HUTB_MAXBUF * 4];

    if (!aes_en(account, e_acc, sizeof e_acc) || !aes_en(ip, e_ip, sizeof e_ip) ||
        !aes_en(mac, e_mac, sizeof e_mac) || !aes_en("0", e_t, sizeof e_t))
        return;

    /* 页面请求的 User-Agent 小写形式，抓包里确认过 */
    static const char *ua_low = "mozilla/5.0 (windows nt 10.0; win64; x64) applewebkit/537.36 "
                                "(khtml, like gecko) chrome/144.0.0.0 safari/537.36";
    if (!aes_en(ua_low, e_ua, sizeof e_ua)) return;

    snprintf(inner, sizeof inner,
             "{\"account\":\"%s\",\"wlan_user_ip\":\"%s\",\"wlan_user_mac\":\"%s\","
             "\"user_agent\":\"%s\",\"login_t\":\"%s\"}",
             e_acc, e_ip, e_mac, e_ua, e_t);

    if (!aes_en(inner, enc, sizeof enc)) return;
    if (!url_enc(enc, uenc, sizeof uenc)) return;

    snprintf(path, sizeof path,
             "/eportal/portal/duodian/queryPageSet?callback=dr%d&params=%s&jsVersion=%s&v=%d&lang=zh",
             rnd(), uenc, PORTAL_JS_VER, rnd());

    HttpResp r = plat_http_get(PORTAL_HOST, PORTAL_EP_PORT, 1, path);
    free(r.body);
}

int core_login(const Conf *c, int force)
{
    Status st;
    if (!core_status(&st)) {
        core_msg(LV_ERR, "%s", TXT_PORTAL_DOWN);
        return 2;
    }
    if (st.online && !force) {
        core_msg(LV_OK, "已经是连着的状态，不用重复登录");
        return 1;
    }
    if (!st.ip[0] || !strcmp(st.ip, "000.000.000.000")) {
        core_msg(LV_ERR, "%s", TXT_IP_FAIL);
        return 0;
    }

    /* 个别环境 AC 不下发客户端 MAC，此时按页面默认值传全零 */
    if (!st.mac[0]) snprintf(st.mac, sizeof st.mac, "000000000000");

    Cfg cf;
    if (!core_load_cfg(st.ip, st.mac, &cf)) {
        core_msg(LV_ERR, "%s", TXT_CFG_FAIL);
        return 0;
    }
    if (cf.en_md5) core_msg(LV_WAIT, "%s", TXT_MD5_WARN);

    char payload[HUTB_MAXBUF], enc[HUTB_MAXBUF], uenc[HUTB_MAXBUF * 3];
    char path[HUTB_MAXBUF * 4];

    core_build_payload(c, st.ip, st.mac, &cf, rnd(), payload, sizeof payload);
    if (!aes_en(payload, enc, sizeof enc)) { core_msg(LV_ERR, "加密登录参数失败"); return 0; }
    if (!url_enc(enc, uenc, sizeof uenc))  { core_msg(LV_ERR, "编码登录参数失败"); return 0; }

    snprintf(path, sizeof path,
             "/eportal/portal/login?callback=dr%d&params=%s&jsVersion=%s&v=%d&lang=zh",
             rnd(), uenc, PORTAL_JS_VER, rnd());

    query_page_set(c->user, st.ip, st.mac);

    HttpResp r = plat_http_get(PORTAL_HOST, PORTAL_EP_PORT, 1, path);
    if (r.status != 200 || !r.body) {
        core_msg(LV_ERR, "登录请求没有成功发出");
        free(r.body);
        return 0;
    }

    const char *js = jsonp_body(r.body);
    char v[256], msg[256];
    v[0] = msg[0] = 0;
    jget(js, "result", v, sizeof v);
    jget(js, "msg", msg, sizeof msg);

    int ok = !strcmp(v, "1") || !strcmp(v, "ok");
    if (ok) {
        core_msg(LV_OK, "%s", TXT_LOGIN_OK);
    } else if (!strcmp(msg, "AC999")) {
        /* 实测：该地址已有会话时，不论账号密码是否存在都返回此码 */
        core_msg(LV_OK, "%s", TXT_AC999);
        ok = 1;
    } else {
        core_msg(LV_ERR, "%s", TXT_LOGIN_BAD);
        core_msg(LV_INFO, "服务器原话：%s", msg[0] ? msg : v);
    }
    free(r.body);
    return ok;
}

int core_logout(void)
{
    Status st;
    if (!core_status(&st)) { core_msg(LV_ERR, "%s", TXT_PORTAL_DOWN); return 0; }

    Cfg cf;
    if (!core_load_cfg(st.ip, st.mac, &cf)) { core_msg(LV_ERR, "%s", TXT_CFG_FAIL); return 0; }

    char path[HUTB_MAXBUF * 2];
    snprintf(path, sizeof path,
             "/eportal/portal/logout?callback=dr%d&login_method=%d&user_account=drcom"
             "&user_password=123&ac_logout=%d&register_mode=%d&wlan_user_ip=%s"
             "&wlan_user_ipv6=&wlan_vlan_id=0&wlan_user_mac=%s&wlan_ac_ip=&wlan_ac_name="
             "&jsVersion=%s&v=%d&lang=zh",
             rnd(), cf.login_method, cf.ac_logout, cf.register_mode,
             st.ip, st.mac, PORTAL_JS_VER, rnd());

    HttpResp r = plat_http_get(PORTAL_HOST, PORTAL_EP_PORT, 1, path);
    if (r.status != 200 || !r.body) { core_msg(LV_ERR, "注销请求失败"); free(r.body); return 0; }

    const char *js = jsonp_body(r.body);
    char v[128];
    v[0] = 0;
    jget(js, "result", v, sizeof v);
    int ok = !strcmp(v, "1") || !strcmp(v, "ok");
    core_msg(ok ? LV_OK : LV_ERR, ok ? "已断开连接" : "断开连接失败");
    free(r.body);
    return ok;
}

int core_probe_online(void)
{
    static const struct {
        const char *host; int port; const char *path; int want;
    } P[] = {
        { "connect.rom.miui.com", 80, "/generate_204", 204 },
        { "www.gstatic.com",      80, "/generate_204", 204 },
        { "www.baidu.com",        80, "/robots.txt",   200 },
    };

    for (int i = 0; i < (int)(sizeof P / sizeof P[0]); i++) {
        HttpResp r = plat_http_get(P[i].host, P[i].port, 0, P[i].path);
        int ok = (r.status == P[i].want);
        free(r.body);
        if (ok) return 1;
    }
    return 0;
}

/* 排错输出里把密码抹掉——这段是要发给别人的 */
static void redact_pwd(const char *in, char *out, int outn)
{
    const char *key = "\"user_password\":\"";
    const char *p = strstr(in, key);
    if (!p) { snprintf(out, (size_t)outn, "%s", in); return; }

    int head = (int)(p - in) + (int)strlen(key);
    if (head >= outn) head = outn - 1;
    memcpy(out, in, (size_t)head);
    out[head] = 0;

    const char *q = in + head;
    while (*q && *q != '"') q++;          /* 跳过原密码 */

    snprintf(out + head, (size_t)(outn - head), "***REDACTED***%s", q);
}

int core_diag(const Conf *c)
{
    core_msg(LV_INFO, "==== 自检：只读，不会提交认证 ====");
    {
        char masked[128];
        size_t n = strlen(c->user);
        if (n > 6) snprintf(masked, sizeof masked, "%.*s****%s", (int)(n - 4), c->user, c->user + n - 4);
        else       snprintf(masked, sizeof masked, "****");
        core_msg(LV_INFO, "账号 %s%s，检测间隔 %d 秒", masked, c->suffix, c->interval);
    }

    Status st;
    core_msg(LV_WAIT, "1/3 查询本机在校园网中的地址…");
    if (!core_status(&st)) {
        core_msg(LV_ERR, "%s", TXT_PORTAL_DOWN);
        core_msg(LV_INFO, "这一步就不通，说明当前网络到不了门户，后面的步骤无法判断");
        return 1;
    }
    core_msg(LV_OK, "地址 %s，客户端 MAC %s，当前%s在线",
             st.ip, st.mac[0] ? st.mac : "(未下发)",
             st.online ? "" : "未");

    Cfg cf;
    core_msg(LV_WAIT, "2/3 读取门户页面配置…");
    if (!core_load_cfg(st.ip, st.mac, &cf)) {
        core_msg(LV_ERR, "%s", TXT_CFG_FAIL);
        return 1;
    }
    core_msg(LV_OK, "门户配置读取正常（rcn=%s，认证方式=%d）", cf.rcn, cf.login_method);

    char payload[HUTB_MAXBUF], enc[HUTB_MAXBUF], uenc[HUTB_MAXBUF * 3];
    char path[HUTB_MAXBUF * 4], safe[HUTB_MAXBUF];
    core_build_payload(c, st.ip, st.mac, &cf, 6535, payload, sizeof payload);
    redact_pwd(payload, safe, sizeof safe);
    aes_en(payload, enc, sizeof enc);
    url_enc(enc, uenc, sizeof uenc);
    snprintf(path, sizeof path,
             "/eportal/portal/login?callback=dr%d&params=%s&jsVersion=%s&v=6535&lang=zh",
             rnd(), uenc, PORTAL_JS_VER);

    core_msg(LV_WAIT, "3/3 组装登录请求（未提交）…");
    core_msg(LV_OK, "登录请求已就绪");
    core_msg(LV_INFO, "把下面这一整段发给作者即可定位问题：");
    core_msg(LV_INFO, "--8<-- 复制开始 --8<--");
    core_msg(LV_INFO, "code=%s", cf.en_md5 ? "en_md5=1" : "en_md5=0");
    core_msg(LV_INFO, "chk_online=%d", st.online);
    core_msg(LV_INFO, "rcn=%s", cf.rcn);
    core_msg(LV_INFO, "payload=%s", safe);
    core_msg(LV_INFO, "url=%s", path);
    core_msg(LV_INFO, "--8<-- 复制结束 --8<--");
    return 0;
}

/* ================================================================ 自检 */
static int st_fail;
static int g_st_verbose;

static void st_check(int cond, const char *name, const char *detail)
{
    if (cond) {
        if (g_st_verbose) core_msg(LV_OK, "自检通过：%s", name);
    } else {
        st_fail++;
        core_msg(LV_ERR, "自检失败：%s%s%s", name, detail ? " — " : "", detail ? detail : "");
    }
}

int core_selftest(int verbose)
{
    g_st_verbose = verbose;
    st_fail = 0;

    /* 1. AES+base64：拿真实抓包的明文与密文对拍 */
    {
        /* 合成向量：期望值由 OpenSSL 独立计算，只校验 AES/PKCS7/base64 实现 */
        static const char *pt =
            "{\"login_method\":1,\"user_account\":\",0,202300000000\",\"user_password\""
            ":\"Demo#Pass123\",\"wlan_user_ip\":\"10.50.75.181\",\"wlan_user_ipv6\":\"\","
            "\"wlan_user_mac\":\"000000000000\",\"wlan_ac_ip\":\"\",\"wlan_ac_name\":\"\",\""
            "jsVersion\":\"4.2.1\",\"login_t\":\"0\",\"js_status\":\"0\",\"is_page\":\"1\",\"is"
            "_page_new\":6535,\"terminal_type\":1,\"lang\":\"zh-cn\",\"rcn\":\"4fjhI4Ay\"}";
        static const char *ct =
            "mqwFvJwrZkq/7Flx+PkDRKvB/Bt593dBtyQsk5ZjIx56b2/I6NbH6e6yehJw0S2m0k"
            "nOI1Rid5WxB1HkIqR6v+1YMzSD2AzDpR4oMK/cK3Jl3Ps/jcAK0mJ0n5/J0jfMW7yw"
            "som0S1k7hK1leCm2j4mvk06xojWUXBQUALMztN6ltgXR4hhQWpxTza0aqbxGHgL90N"
            "I8t+DGMT3ddxcE6cyGO8OaRYZUNt8stgEfh9bTpceNrI0wijpsFfEND+G2OlALHyKB"
            "mDJY04t+x9WDA/j09Fe7gcwwHCt+O0D2exTMI2PhBEwKBZEqAe9QbdQtM/2uG8V6Wa"
            "LLvI4G4vd3UjuNgQksHmMrMx95OSyHHYNryQcoRTz0ET72s/IFNWNPum9kFsTjx21q"
            "La7fqtXWDcBB8EbDbURCU59QYGWvV2olt1WYIjIV4hEuafiVfUfN";
        char out[HUTB_MAXBUF];
        aes_en(pt, out, sizeof out);
        st_check(!strcmp(out, ct), "加密结果与 OpenSSL 基准一致", NULL);
    }

    /* 2. base64 边界 */
    {
        char o[64];
        b64_str("", o, sizeof o);
        int ok = !strcmp(o, "");
        b64_str("a", o, sizeof o);   ok = ok && !strcmp(o, "YQ==");
        b64_str("ab", o, sizeof o);  ok = ok && !strcmp(o, "YWI=");
        b64_str("abc", o, sizeof o); ok = ok && !strcmp(o, "YWJj");
        st_check(ok, "base64 长度边界（0/1/2/3 字节）", NULL);
    }

    /* 3. URL 编码：base64 里的 + / = 必须被转义 */
    {
        char o[64];
        url_enc("a+b/c=", o, sizeof o);
        st_check(!strcmp(o, "a%2Bb%2Fc%3D"), "URL 编码转义 + / =", o);
    }

    /* 4. JSONP 取值 */
    {
        const char *js = "dr1002({\"result\":1,\"ss5\":\"10.50.75.181\",\"ss4\":\"000000000000\"})";
        const char *body = jsonp_body(js);
        char v[64];
        int ok = jget(body, "result", v, sizeof v) && !strcmp(v, "1");
        ok = ok && jget(body, "ss5", v, sizeof v) && !strcmp(v, "10.50.75.181");
        ok = ok && jget(body, "not_exist", v, sizeof v) == 0;
        st_check(ok, "JSONP 外壳剥离与字段取值", NULL);
    }

    /* 5. 登录参数拼装：与抓包里浏览器提交的完全一致 */
    {
        Conf c;
        Cfg cf;
        Status st;
        memset(&c, 0, sizeof c);
        memset(&cf, 0, sizeof cf);
        memset(&st, 0, sizeof st);
        snprintf(c.user, sizeof c.user, "202300000000");
        snprintf(c.pass, sizeof c.pass, "Demo#Pass123");
        cf.login_method = 1;
        cf.account_prefix = 1;
        snprintf(cf.rcn, sizeof cf.rcn, "4fjhI4Ay");
        snprintf(st.ip, sizeof st.ip, "10.50.75.181");
        snprintf(st.mac, sizeof st.mac, "000000000000");

        char out[HUTB_MAXBUF];
        core_build_payload(&c, st.ip, st.mac, &cf, 6535, out, sizeof out);
        st_check(strstr(out, "\"user_account\":\",0,202300000000\"") != NULL &&
                 strstr(out, "\"is_page_new\":6535") != NULL &&
                 strstr(out, "\"rcn\":\"4fjhI4Ay\"") != NULL,
                 "登录参数按门户格式拼装", NULL);
    }

    if (st_fail)
        core_msg(LV_ERR, "自检未通过，共 %d 项失败", st_fail);
    else if (verbose)
        core_msg(LV_OK, "自检全部通过");
    return st_fail;
}
