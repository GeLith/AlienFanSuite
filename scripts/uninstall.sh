#!/bin/bash
# AlienFanSuite 卸载器
set -e

if [ "$(id -u)" -ne 0 ]; then
  echo "请用 sudo 运行：sudo ./uninstall.sh" >&2
  exit 1
fi

echo "==> 卸载内核扩展"
kmutil unload -b com.dongdong.AlienFan 2>&1 | sed 's/^/    /' || true

echo "==> 删除文件"
rm -rf "/Library/Extensions/AlienFan.kext"
rm -f "/usr/local/bin/alienfanctl"
rm -rf "/Applications/FanControl.app"

echo
echo "已卸载。建议重启以清理内核集合中的残留。"
echo "曲线预设保留在 ~/Library/Application Support/AlienFan/，如需清理请手动删除。"
