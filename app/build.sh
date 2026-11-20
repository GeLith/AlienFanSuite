#!/bin/bash
# 编译 FanControl.app（原生壳 + Web UI）
set -e
cd "$(dirname "$0")"

APP=build/FanControl.app
/bin/rm -rf build
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

clang -O2 -fobjc-arc -Wall -Wno-unused-parameter -Wno-deprecated-declarations \
    -o "$APP/Contents/MacOS/FanControl" \
    main.m AppDelegate.m AFBridge.m curve.m curveview.m afclient.c \
    -framework Cocoa -framework IOKit -framework WebKit

cp Info.plist "$APP/Contents/Info.plist"
plutil -lint "$APP/Contents/Info.plist" >/dev/null

# 打包 Web 资源（HTML/CSS/JS/字体）
cp -R web "$APP/Contents/Resources/web"

# 应用图标
cp FanControl.icns "$APP/Contents/Resources/FanControl.icns"

echo "完成：$APP"
file "$APP/Contents/MacOS/FanControl"
ls -l "$APP/Contents/MacOS/FanControl"
echo "Web 资源：$(find "$APP/Contents/Resources/web" -type f | wc -l | tr -d ' ') 个文件"
