#!/bin/bash
# AlienFan.kext 构建脚本
# 只需要 Command Line Tools（不需要完整 Xcode）
set -e

cd "$(dirname "$0")"

SDK="${SDK:-/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk}"
if [ ! -d "$SDK" ]; then
    SDK="$(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX*.sdk 2>/dev/null | tail -1)"
fi
if [ ! -d "$SDK" ]; then
    echo "找不到 SDK" >&2
    exit 1
fi

BUNDLE="build/AlienFan.kext"
/bin/rm -rf build
mkdir -p "$BUNDLE/Contents/MacOS"

CFLAGS=(
    -mkernel -fno-builtin -fno-exceptions -fno-rtti -nostdinc++
    -isysroot "$SDK"
    -I"$SDK/System/Library/Frameworks/Kernel.framework/Headers"
    -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
    -O2 -Wall -Wno-unused-parameter
)

echo "==> 编译"
clang++ "${CFLAGS[@]}" -c AlienFan.cpp -o build/AlienFan.o

echo "==> 链接"
clang++ -mkernel -nostdlib -Xlinker -kext \
    -o "$BUNDLE/Contents/MacOS/AlienFan" \
    -isysroot "$SDK" -L"$SDK/usr/lib" -lkmod -lkmodc++ \
    build/AlienFan.o

cp Info.plist "$BUNDLE/Contents/Info.plist"
plutil -lint "$BUNDLE/Contents/Info.plist" >/dev/null

echo "==> 编译用户态 CLI"
clang -O2 -Wall -Wno-unused-parameter \
    -o build/alienfanctl alienfanctl.c \
    -framework IOKit -framework CoreFoundation

echo "完成：$BUNDLE"
file "$BUNDLE/Contents/MacOS/AlienFan"
ls -l build/alienfanctl
