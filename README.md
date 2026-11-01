# 校园网自动登录（HUTB）

湖南工商校园网（Dr.COM eportal）自动登录工具。下载一个文件、双击、输一次账号密码，之后自动保持联网、断线自动重连、开机自动启动。

- **Windows**：单个 exe，约 90 KB，不需要装任何运行环境
- **Linux**：单个二进制 + systemd 服务，`sudo ./install.sh` 一条命令装好

> **免责声明**：本程序仅用于登录**你自己的**校园网账号，不得用于代替他人认证、共享账号或绕过计费。使用本程序产生的后果由使用者自行承担。若学校网络管理规定不允许此类工具，请立即停止使用并删除。

---

## Windows 使用

1. 下载 `hutb-windows-x64.exe`
2. 双击运行
3. 在弹出的窗口里填学号、密码（校园网用户运营商留空），点确定

之后程序会缩到屏幕右下角托盘，图标颜色表示状态：绿色=已连接，黄色=正在连接，红色=连不上。

托盘右键菜单：立即重连 / 修改账号 / 打开日志 / 打开程序目录 / 退出。

程序第一次跑通后会自动设置开机自启，不需要你自己放快捷方式。

### 首次运行时可能遇到的拦截

exe 没有数字签名，Windows 可能提示「已保护你的电脑」：

- 点「更多信息」→「仍要运行」
- 若被 SmartScreen 拦住：右键文件 → 属性 → 勾选「解除锁定」→ 确定
- 个别杀软会报未知程序，加入信任即可

---

## Linux 使用

```bash
tar -xzf hutb-linux-x64.tar.gz
cd hutb-linux-x64
sudo ./install.sh
```

安装脚本会：装程序到 `/usr/local/bin/hutb`、提示输入账号密码（写入 `/etc/hutb/config`，权限 600）、注册 systemd 服务并立即启动。

```bash
systemctl status hutb        # 看服务状态
journalctl -u hutb -f        # 实时看日志
sudo hutb -c                 # 修改账号
sudo ./uninstall.sh          # 卸载（保留账号配置）
sudo ./uninstall.sh --purge  # 卸载并删除账号配置
```

需要 `libcurl` 运行库（Debian/Ubuntu 一般已自带；若报 `libcurl.so.4: cannot open`，执行 `sudo apt install libcurl4`）。

---

## 路由器后面用（一个宿舍共用）

常见用法：小米路由器做 NAT，把程序装在路由器后面的某台 Windows 或 NAS 上，认证一次，全屋设备都能上网。

这成立是因为：**校园网的认证会话按源 IP 建立，不按 MAC**。路由器后面所有设备共用 WAN 口那一个 IP，一份认证覆盖整个局域网。

前提：

- 路由器必须是**路由（NAT）模式**，不能是 AP / 桥接模式，否则每台设备各拿一个 IP，认证只对一台生效
- 跑程序的那台机器要**一直开着**，它关机或休眠，全屋断网
- WAN 口 IP 变了（路由器重启、租约到期）会自动重新认证，中间有几秒到两三分钟的断网

---

## 出问题了怎么办

先跑一条命令收集信息（只读，不会提交认证）：

```bash
# Windows
hutb-windows-x64.exe --diag

# Linux
sudo hutb --diag
```

把输出整段发给作者即可定位。输出里账号已遮蔽、密码已抹掉，可以放心发。

### 常见现象

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 托盘红色，日志写「不在校园网内」 | 当前网络到不了学校门户 | 连上校园网，或检查路由器 WAN |
| 日志写「账号或密码不对」 | 密码错，或运营商后缀选错 | 托盘右键「修改账号」重填 |
| 装完一直连不上 | 开了 Clash 等代理的 TUN / fake-ip 模式 | 把校园网网段加入直连，或临时关掉代理 |
| 学校改版后全部失效 | 门户换了密钥或接口路径 | 见下节，作者发新版 |

---

## 学校改版了（作者看这里）

协议参数编译在代码里，改版时需要改一处再重编译：

`src/core.h` 顶部的 `AES_KEY_TEXT`（密钥）与 `PORTAL_*` 常量（域名、端口、版本号）。

**怎么找新密钥**：打开校园网登录页，F12 → Network，登录一次，找到 `portal/login` 请求；在同目录的 JS（通常是 `a41.js`）里搜 `aes_en:`，默认参数就是密钥。

改完先本机自检（会拿内置的真实抓包数据比对加密结果，跑不过说明改错了）：

```bash
# Windows
powershell -ExecutionPolicy Bypass -File build-windows.ps1

# Linux
sudo apt install build-essential libcurl4-openssl-dev
./build-linux.sh
```

然后打 tag 发布：

```bash
git tag v2.0.1 && git push origin v2.0.1
```

CI 自动编译两个平台并发布到 Release。**同学升级只要下载新文件覆盖旧程序**，账号配置不会丢（Windows 在 `hutb.dat`，Linux 在 `/etc/hutb/config`）。

更细的协议分析与踩坑记录见 [协议分析笔记.md](./协议分析笔记.md)。

---

## 它是怎么工作的

抓包还原的 Dr.COM eportal 协议，全部是 GET 请求：

1. `/drcom/chkstatus` — 取本机在校园网中的 IP、MAC、是否已在线
2. `:802/eportal/portal/page/loadConfig` — 取 `rcn`（每次登录都变）、认证方式、账号前缀规则
3. `:802/eportal/portal/duodian/queryPageSet` — 登录前上报
4. `:802/eportal/portal/login?params=…` — 真正的登录

`params` = `base64(AES-128-ECB-PKCS7(JSON))`，密钥取自门户 JS（`a41.js` 的 `util.aes_en` 默认参数）。

代码结构：

```
src/core.c       协议、加密、编码、统一文案（两端共用）
src/win/         WinHTTP + DPAPI + 配置对话框 + 托盘
src/linux/       libcurl + 0600 配置 + systemd
tests/           自检与配置存储的自动化测试
```

`--selftest` 离线校验：AES/PKCS7/base64 结果与 OpenSSL 基准逐字节一致、base64 边界、URL 转义、JSONP 取值、登录参数拼装。CI 每次构建都跑，改坏协议会直接失败。

（基准向量是合成的，不含任何真实账号；换密钥后这条会立刻失败，提醒你重新生成向量。）

## 许可

MIT
