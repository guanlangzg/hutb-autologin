#!/bin/sh
# 校园网自动登录 · 卸载脚本
# 用法：sudo ./uninstall.sh          卸载程序和服务，保留账号配置
#       sudo ./uninstall.sh --purge  连账号配置一起删除
set -e

BIN_DST=/usr/local/bin/hutb
CONF_DIR=/etc/hutb
UNIT=/etc/systemd/system/hutb.service

if [ "$(id -u)" != "0" ]; then
    echo "需要管理员权限，请用： sudo ./uninstall.sh"
    exit 1
fi

if command -v systemctl >/dev/null 2>&1; then
    systemctl stop hutb 2>/dev/null || true
    systemctl disable hutb 2>/dev/null || true
fi

[ -f "$UNIT" ] && rm -f "$UNIT" && echo "已移除 systemd 服务"
if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload 2>/dev/null || true
fi

[ -f "$BIN_DST" ] && rm -f "$BIN_DST" && echo "已删除 $BIN_DST"

if [ "$1" = "--purge" ]; then
    rm -rf "$CONF_DIR"
    echo "已删除账号配置 $CONF_DIR"
else
    echo "账号配置保留在 $CONF_DIR，如需一并删除： sudo ./uninstall.sh --purge"
fi
