#!/bin/sh
# 构建 Linux 版
# 依赖： gcc（或 clang）+ libcurl 开发包
#   Debian/Ubuntu: sudo apt install build-essential libcurl4-openssl-dev
set -e

CC=${CC:-cc}

echo "==> 编译 hutb"
$CC -O2 -Wall -Wextra -o hutb src/linux/main_linux.c src/core.c -lcurl

echo "==> 完成：$(ls -l hutb | awk '{print $5}') 字节"

echo "==> 自检"
./hutb --selftest
