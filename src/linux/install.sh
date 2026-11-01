#!/bin/sh
# 校园网自动登录 · Linux 安装脚本
# 用法：sudo ./install.sh
set -e

BIN_DST=/usr/local/bin/hutb
CONF_DIR=/etc/hutb
CONF=$CONF_DIR/config
UNIT=/etc/systemd/system/hutb.service
SRC_DIR=$(cd "$(dirname "$0")" && pwd)

if [ "$(id -u)" != "0" ]; then
    echo "需要管理员权限，请用： sudo ./install.sh"
    exit 1
fi

if [ ! -f "$SRC_DIR/hutb" ]; then
    echo "找不到 $SRC_DIR/hutb，请先把编译好的程序放在同一目录"
    exit 1
fi

echo "==> 安装程序到 $BIN_DST"
install -m 755 "$SRC_DIR/hutb" "$BIN_DST"

mkdir -p "$CONF_DIR"
chmod 700 "$CONF_DIR"

if [ -f "$CONF" ]; then
    echo "==> 检测到已有账号配置，保留不覆盖"
    echo "    如需修改请执行： sudo $BIN_DST -c"
else
    echo "==> 请填写校园网账号（只保存在本机，权限 600）"
    "$BIN_DST" -c
fi

if ! command -v systemctl >/dev/null 2>&1; then
    echo
    echo "本机没有 systemd，无法自动配置开机自启。"
    echo "可以直接前台运行： $BIN_DST --daemon"
    exit 0
fi

echo "==> 写入 systemd 服务 $UNIT"
cat > "$UNIT" <<EOF
[Unit]
Description=Campus Network Auto Login (HUTB)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=$BIN_DST --daemon
Restart=always
RestartSec=15
# 网络还没通时不要疯狂重启
StartLimitIntervalSec=0

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable hutb >/dev/null 2>&1
systemctl restart hutb

sleep 2
echo
if systemctl is-active --quiet hutb; then
    echo "安装完成，服务已启动。"
    echo "  查看状态： systemctl status hutb"
    echo "  查看日志： journalctl -u hutb -f"
    echo "  修改账号： sudo $BIN_DST -c"
else
    echo "服务启动失败，请查看： journalctl -u hutb -n 30"
    exit 1
fi
