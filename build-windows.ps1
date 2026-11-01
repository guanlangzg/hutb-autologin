# 构建 Windows 版（依赖 zig，通过 pip 安装，无需装 Visual Studio）
# 用法： powershell -ExecutionPolicy Bypass -File build-windows.ps1
$ErrorActionPreference = "Stop"

Write-Host "==> 准备编译器 (ziglang)"
python -m pip install --quiet --disable-pip-version-check ziglang

Write-Host "==> 编译 hutb.exe"
python -m ziglang cc -target x86_64-windows-gnu -Oz -s "-Wl,--subsystem,windows" `
    -ffunction-sections -fdata-sections "-Wl,--gc-sections" `
    src/win/main_win.c src/win/conf_win.c src/win/icon_win.c src/core.c `
    -o hutb.exe `
    -lwinhttp -lcrypt32 -lshell32 -lcomctl32 -ladvapi32 -lgdi32 -luser32

$size = (Get-Item hutb.exe).Length
Write-Host ("==> 完成：hutb.exe  {0:N0} 字节" -f $size)

Write-Host "==> 自检"
& .\hutb.exe --selftest
if ($LASTEXITCODE -ne 0) { throw "自检未通过" }
