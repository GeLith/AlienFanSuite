#!/bin/bash
# 从源码构建驱动 + 控制软件，并打成一份发布 zip
set -e
cd "$(dirname "$0")/.."

APP_VER="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' app/Info.plist)"
KEXT_VER="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' driver/Info.plist)"
NAME="AlienFanSuite-v${APP_VER}"
OUT="dist/${NAME}"

echo "==> 版本：App ${APP_VER} / Driver ${KEXT_VER}"
/bin/rm -rf "$OUT" "dist/${NAME}.zip"
mkdir -p "$OUT"

echo "==> 构建驱动"
(cd driver && ./build.sh)

echo "==> 构建控制软件"
(cd app && ./build.sh)

echo "==> 组装发布包"
cp -R driver/build/AlienFan.kext "$OUT/"
cp driver/build/alienfanctl "$OUT/"
cp -R app/build/FanControl.app "$OUT/"
cp scripts/install.sh scripts/uninstall.sh "$OUT/"
chmod 755 "$OUT/install.sh" "$OUT/uninstall.sh"
cp README.md LICENSE "$OUT/"

# 版本号写进发布包根目录，便于用户核对
cat > "$OUT/VERSION.txt" <<EOF
AlienFanSuite ${APP_VER}
FanControl.app ${APP_VER}
AlienFan.kext ${KEXT_VER}
alienfanctl ${KEXT_VER}
EOF

echo "==> 压缩"
(cd dist && zip -qry "${NAME}.zip" "$NAME")

echo
echo "完成：$(pwd)/dist/${NAME}.zip"
ls -lh "dist/${NAME}.zip"
