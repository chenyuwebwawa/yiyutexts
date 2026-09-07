#!/bin/bash
# 译语输入法 YuyinIme — macOS 一键安装/卸载
# 用法：
#   ./mac-setup.sh            安装（编译 + /Applications + 开机自启 + 打开授权页）
#   ./mac-setup.sh uninstall  卸载
set -e
cd "$(dirname "$0")"

PLIST_LABEL=org.yuyan.ime
PLIST=~/Library/LaunchAgents/$PLIST_LABEL.plist
APP=/Applications/YuyinIme.app

if [ "$1" = "uninstall" ]; then
  echo "==> 停止输入法"
  launchctl unload "$PLIST" 2>/dev/null || pkill -f YuyinIme || true
  rm -f "$PLIST"
  rm -rf "$APP"
  echo "已卸载完成。"
  echo "建议：系统设置 → 隐私与安全性 → 输入监控 中移除 YuyinIme 条目（可选）。"
  exit 0
fi

echo "==> 编译（需要 Xcode Command Line Tools）"
if ! xcode-select -p >/dev/null 2>&1; then
  echo "先安装命令行工具："; xcode-select --install; exit 1
fi
mkdir -p build
swiftc -O -framework AppKit -framework CoreGraphics -o build/YuyinIme macos/daemon.swift

echo "==> 安装到 /Applications"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp build/YuyinIme "$APP/Contents/MacOS/YuyinIme"
[ -f dict.tsv ] || python3 tools/build_dict.py en
cp dict.tsv "$APP/Contents/Resources/dict.tsv"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>org.yuyan.ime</string>
  <key>CFBundleName</key><string>YuyinIme</string>
  <key>CFBundleExecutable</key><string>YuyinIme</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0.0</string>
</dict></plist>
PLIST

echo "==> 配置开机自启"
mkdir -p ~/Library/LaunchAgents
cat > "$PLIST" <<PLIST2
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$PLIST_LABEL</string>
  <key>ProgramArguments</key><array><string>$APP/Contents/MacOS/YuyinIme</string></array>
  <key>RunAtLoad</key><true/>
</dict></plist>
PLIST2
launchctl unload "$PLIST" 2>/dev/null || true
launchctl load "$PLIST"

echo "==> 首次授权"
echo "请在「系统设置 → 隐私与安全性 → 输入监控」中勾选 YuyinIme"
open "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility" || true

echo "安装完成。菜单栏出现「译」即运行中。卸载：./mac-setup.sh uninstall"
