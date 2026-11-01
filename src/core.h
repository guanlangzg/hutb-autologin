/* ============================================================================
 *  hutb core —— 与平台无关的门户认证逻辑
 *
 *  平台（Windows / Linux）需要实现本文件末尾声明的 plat_* 函数。
 *  所有中文提示文案集中在这里，保证两个平台说的话一致。
 * ========================================================================== */
#ifndef HUTB_CORE_H
#define HUTB_CORE_H

#include <stddef.h>

#define HUTB_VERSION "2.0.0"

/* ---- 门户参数（学校改版时改这里；源码位置见 README「改版应急」） ---- */
#define PORTAL_HOST    "portal.hutb.edu.cn"
#define PORTAL_EP_PORT 802
#define PORTAL_JS_VER  "4.2.1"
#define PORTAL_JS_OLD  "4.X"
#define AES_KEY_TEXT   "5c1d5ad4dea0e8dd"   /* 取自门户 a41.js util.aes_en 默认值 */

#define HUTB_MAXBUF 8192

/* ---- 日志级别（决定平台侧的颜色/图标） ---- */
enum {
    LV_OK = 0,   /* ✓ 成功 */
    LV_ERR,      /* ✗ 失败 */
    LV_WAIT,     /* · 进行中 */
    LV_INFO      /*   普通信息 */
};

/* ---- 数据结构 ---- */
typedef struct {
    char user[64];
    char pass[128];
    char suffix[32];
    int  interval;          /* 守护检测间隔（秒） */
} Conf;

typedef struct {
    int  status;            /* HTTP 状态码，0 表示连接失败 */
    char *body;             /* 响应正文（可 free） */
} HttpResp;

typedef struct {
    char ip[64];
    char mac[32];
    int  online;
} Status;

typedef struct {
    char rcn[64];
    int  login_method;
    int  account_prefix;
    int  ac_logout;
    int  register_mode;
    int  en_md5;
} Cfg;

/* ---- 字符串工具（自检也依赖它们） ---- */
int  aes_en(const char *text, char *out, int outn);
int  b64_str(const char *in, char *out, int outn);
int  url_enc(const char *in, char *out, int outn);
int  jget(const char *json, const char *key, char *out, int outn);
const char *jsonp_body(const char *s);

/* ---- 业务 ---- */
void core_msg(int lv, const char *fmt, ...);
int  core_status(Status *st);
int  core_load_cfg(const char *ip, const char *mac, Cfg *cf);
int  core_probe_online(void);

/* 返回 2=门户不可达 1=已在线/登录成功 0=被拒 */
int  core_login(const Conf *c, int force);
int  core_logout(void);

/* 自检：离线校验加密/编码/参数拼装，CI 里跑，返回失败条数 */
int  core_selftest(int verbose);

/* 排错：打印三步请求的原始响应（只读，不提交认证） */
int  core_diag(const Conf *c);

/* 由 Cfg/Conf 拼出登录 JSON（单独导出便于自检比对抓包） */
int  core_build_payload(const Conf *c, const char *ip, const char *mac,
                        const Cfg *cf, int page_new, char *out, int outn);

/* ---- 平台必须实现 ---- */
void plat_write(int lv, const char *line);          /* 输出一行（已含时间戳与前缀） */
void plat_sleep_ms(int ms);
long plat_now(void);                                /* 秒 */
int  plat_conf_load(Conf *c);                       /* 1=读到完整配置 */
int  plat_conf_save(const Conf *c);                 /* 1=成功 */
HttpResp plat_http_get(const char *host, int port, int secure, const char *path);
void plat_bad_cert_note(void);                      /* 证书校验失败时提示一次，可为空实现 */

/* ---- 统一文案 ---- */
#define TXT_NET_OFF      "当前没有连上校园网，也没有网络可用"
#define TXT_PORTAL_DOWN  "连不上校园网门户，可能不在校园网内"
#define TXT_IP_FAIL      "没能识别出本机在校园网里的地址"
#define TXT_CFG_FAIL     "读取门户页面配置失败"
#define TXT_AC999        "这个地址已经有在线会话了，不需要重复登录"
#define TXT_LOGIN_OK     "已连接，开始正常上网"
#define TXT_LOGIN_BAD    "账号或密码不对，请重新输入"
#define TXT_MD5_WARN     "门户启用了新的加密方式，本版本暂不支持"
#define TXT_NEED_CONF    "还没有配置账号，请先运行一次程序完成设置"
#define TXT_RECONNECT    "网络断开，正在重新连接"
#define TXT_BACK_ONLINE  "网络已恢复"

#endif /* HUTB_CORE_H */
