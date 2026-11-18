#!/bin/sh
# 在畅行60 (ZTE 7552N) 上安装 work/changxing60-openwrt.tar.gz 里预置的 OpenWrt rootfs。
# 说明见 docs/INSTALL-NEW-PHONE.zh-CN.md
set -eu

TOP=$(cd "$(dirname "$0")" && pwd)
WORK=${CHANGXING60_WORK:-$TOP/work}
KOUT=${CHANGXING60_KERNEL_OUT:-$TOP/out}
TAR=$WORK/changxing60-openwrt.tar.gz
WANT=076f413332592a2d83ebd834550a85e695f52922be7d2038b6a3912f2460d219

say() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

say "host tools"
for c in adb docker python3 lz4; do
    command -v "$c" >/dev/null || die "$c not found (install it first)"
done

say "staged rootfs"
[ -s "$TAR" ] || die "missing $TAR"
sum=$(shasum -a 256 "$TAR" 2>/dev/null | cut -d' ' -f1 || sha256sum "$TAR" | cut -d' ' -f1)
[ "$sum" = "$WANT" ] || die "rootfs checksum mismatch: $sum (expected $WANT)"

say "kernel outputs"
[ -f "$KOUT/Image" ] && ls "$KOUT"/modules/*.ko >/dev/null 2>&1 \
    || die "kernel outputs missing in $KOUT (build with kernel/build-linux.sh; see doc 1.3)"

say "device dumps"
[ -s "$WORK/dumps/boot_ori.img" ] || die "missing work/dumps/boot_ori.img (stock boot of THIS phone; see doc 1.4)"
[ -s "$WORK/dumps/misc-head.bin" ] || die "missing work/dumps/misc-head.bin (see doc 1.4)"

say "device"
adb get-state >/dev/null 2>&1 || die "no adb device (Android booted, USB debugging on)"
uid=$(adb shell 'su -c id -u' </dev/null 2>/dev/null | tr -d '\r')
[ "$uid" = "0" ] || die "su does not return uid 0 (device not rooted?)"
slot=$(adb shell getprop ro.boot.slot_suffix </dev/null | tr -d '\r')
[ "$slot" = "_a" ] || die "Android must be on slot _a (currently '$slot')"

say "starting installer with CHANGXING60_REUSE_BUILD=1"
echo "  rootfs: $TAR"
echo "  kernel: $KOUT"
echo "  选择：2) OpenWrt / 内部存储 / 最后输入 INSTALL"
exec env CHANGXING60_REUSE_BUILD=1 CHANGXING60_WORK="$WORK" CHANGXING60_KERNEL_OUT="$KOUT" "$TOP/install.sh" "$@"
