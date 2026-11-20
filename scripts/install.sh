#!/bin/bash
# AlienFanSuite 安装器：驱动 + CLI + 控制软件
set -e

if [ "$(id -u)" -ne 0 ]; then
  echo "请用 sudo 运行：sudo ./install.sh" >&2
  exit 1
fi

BUNDLE_DIR="$(cd "$(dirname "$0")" && pwd)"
KEXT_SRC="$BUNDLE_DIR/AlienFan.kext"
CLI_SRC="$BUNDLE_DIR/alienfanctl"
APP_SRC="$BUNDLE_DIR/FanControl.app"

KEXT_DST="/Library/Extensions/AlienFan.kext"
CLI_DST="/usr/local/bin/alienfanctl"
APP_DST="/Applications/FanControl.app"

case "$BUNDLE_DIR" in
  /Users/*)
    case "$BUNDLE_DIR" in
      /Users/Shared/*) ;;
      *) echo "提示：若出现权限/TCC 错误，请先把整个解压目录拷到 /Users/Shared/ 再安装。" ;;
    esac
    ;;
esac

for f in "$KEXT_SRC" "$CLI_SRC" "$APP_SRC"; do
  if [ ! -e "$f" ]; then
    echo "缺少发布文件：$f" >&2
    exit 2
  fi
done

echo "==> 1/4 安装 AlienFan.kext"
rm -rf "$KEXT_DST"
cp -R "$KEXT_SRC" "$KEXT_DST"
chown -R root:wheel "$KEXT_DST"
chmod -R go-w "$KEXT_DST"
echo "    $KEXT_DST"

echo "==> 2/4 安装 alienfanctl"
mkdir -p /usr/local/bin
cp "$CLI_SRC" "$CLI_DST"
chown root:wheel "$CLI_DST"
chmod 755 "$CLI_DST"
echo "    $CLI_DST"

echo "==> 3/4 安装 FanControl.app"
rm -rf "$APP_DST"
cp -R "$APP_SRC" "$APP_DST"
chown -R root:wheel "$APP_DST"
echo "    $APP_DST"

echo "==> 4/4 尝试加载驱动"
if kmutil showloaded --list-only 2>/dev/null | grep -qi ' AlienFan'; then
  echo "    驱动已加载，无需重复加载"
else
  kmutil load -p "$KEXT_DST" 2>&1 | sed 's/^/    /' || true
fi

echo
echo "安装完成。请打开 FanControl → 设置，确认驱动状态与版本匹配。"
echo "若「内核已加载」为「否」：到 系统设置 → 隐私与安全性 批准驱动后重启；"
echo "OpenCore 用户请把 AlienFan.kext 加入 EFI/OC/Kexts 与 config.plist → Kernel → Add。"
