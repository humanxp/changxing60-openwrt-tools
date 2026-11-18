#!/bin/sh
# 为一台连着 adb 的畅行60（Android 侧已 root）生成可安装的 boot 镜像。
# 不需要 Docker、不需要重新编译内核：内核取自一台在跑的 5.4.254 机器（out/Image），
# 辅助二进制取自它的 boot ramdisk（work/helpers/）。
set -eu

TOP=$(cd "$(dirname "$0")" && pwd)
ADB=${ADB:-adb}
W=$TOP/work
D=$W/dumps

say() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

say "检查输入"
for f in "$TOP/out/Image" "$TOP/work/helpers/bin/busybox" "$TOP/work/helpers/bin/logdw" \
         "$TOP/work/helpers/etc/ueventd-perms.sh" "$TOP/work/helpers/android"; do
    [ -e "$f" ] || die "missing $f"
done
[ -d "$TOP/out/modules" ] || die "missing out/modules"

say "检查设备"
$ADB get-state >/dev/null 2>&1 || die "no adb device"
[ "$($ADB shell 'su -c id -u' </dev/null | tr -d '\r')" = 0 ] || die "su != 0"
[ "$($ADB shell getprop ro.boot.slot_suffix </dev/null | tr -d '\r')" = "_a" ] || die "Android must be on slot _a"
echo "model: $($ADB shell getprop ro.product.model </dev/null | tr -d '\r') / $($ADB shell getprop ro.product.device </dev/null | tr -d '\r')"

say "导出原厂 boot_a 与 misc 头"
mkdir -p "$D"
$ADB shell 'su -c "dd if=/dev/block/by-name/boot_a of=/data/local/tmp/boot_ori.img bs=4M"' </dev/null >/dev/null
$ADB pull /data/local/tmp/boot_ori.img "$D/boot_ori.img" >/dev/null
$ADB shell 'su -c "dd if=/dev/block/by-name/misc of=/data/local/tmp/misc-head.bin bs=4096 count=1"' </dev/null >/dev/null
$ADB pull /data/local/tmp/misc-head.bin "$D/misc-head.bin" >/dev/null
ls -l "$D/boot_ori.img" "$D/misc-head.bin"

say "生成 boot 镜像"
python3 "$TOP/boot/build-boot-image.py" \
    --stock-boot "$D/boot_ori.img" \
    --misc-head "$D/misc-head.bin" \
    --kernel "$TOP/out/Image" \
    --modules "$TOP/out/modules" \
    --busybox "$TOP/work/helpers/bin/busybox" \
    --logdw "$TOP/work/helpers/bin/logdw" \
    --ueventd-perms "$TOP/work/helpers/etc/ueventd-perms.sh" \
    --android-subset "$TOP/work/helpers/android" \
    --out "$W/boot-linux-slotb.img"

say "完成"
ls -lh "$W/boot-linux-slotb.img" "$W/boot-linux-slotb.misc-slot-b-trial.bin" 2>/dev/null || true
cat <<EOF

下一步（见 docs/INSTALL-NEW-PHONE.zh-CN.md）：
  1) 写入 boot_b：
     adb push "$W/boot-linux-slotb.img" /data/local/tmp/
     adb shell 'su -c "dd if=/data/local/tmp/boot-linux-slotb.img of=/dev/block/by-name/boot_b bs=4M && sync"'
  2) 解包 rootfs 到 Linux 区（手动路径见文档 §1.6）
  3) 写 misc 32 字节：
     adb push "$W/boot-linux-slotb.misc-slot-b-trial.bin" /data/local/tmp/
     adb shell 'su -c "dd if=/data/local/tmp/boot-linux-slotb.misc-slot-b-trial.bin of=/dev/block/by-name/misc bs=1 seek=2048 conv=notrunc && sync"'
EOF
