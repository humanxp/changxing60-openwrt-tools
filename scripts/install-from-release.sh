#!/bin/sh
# 从 GitHub Release 下载资源，把 OpenWrt 装到一台已 root、已扩区的畅行60 (ZTE 7552N) 上。
#
#   ./install-from-release.sh --tag v1.0.0              # 交互确认
#   ./install-from-release.sh --tag v1.0.0 --yes        # 不确认（危险）
#   ./install-from-release.sh --tag v1.0.0 --dry-run    # 只检查与构建，不写设备
#   ./install-from-release.sh --local /path/to/assets   # 用本地已下载的资源
#
# 前提：目标机 Android 在 slot a、已 root（su=0）、userdata 已缩出 64 GiB 空闲区。
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
ADB=${ADB:-adb}
TAG=${TAG:-v1.0.9}
YES=0
DRY=0
LOCAL=""
PASSWORD=""
OWNER_REPO=${OWNER_REPO:-humanxp/changxing60-openwrt-tools}

say() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --tag) TAG=$2; shift 2 ;;
        --local) LOCAL=$2; shift 2 ;;
        --password) PASSWORD=$2; shift 2 ;;
        --yes|-y) YES=1; shift ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

command -v "$ADB" >/dev/null || die "adb not found (or set ADB=/path/to/adb)"
command -v python3 >/dev/null || die "python3 not found"
command -v openssl >/dev/null || die "openssl not found"

WORK=${WORK:-$REPO/work}
ASSETS=$WORK/assets
D=$WORK/dumps
mkdir -p "$ASSETS" "$D"

# ---------------------------------------------------------------- assets
if [ -n "$LOCAL" ]; then
    say "使用本地资源 $LOCAL"
    for f in Image modules.tar.gz helpers.tar.gz rootfs.tar.gz SHA256SUMS; do
        [ -f "$LOCAL/$f" ] || die "missing $LOCAL/$f"
        cp "$LOCAL/$f" "$ASSETS/$f"
    done
else
    say "从 GitHub Release 下载 $TAG"
    base="https://github.com/$OWNER_REPO/releases/download/$TAG"
    for f in Image modules.tar.gz helpers.tar.gz rootfs.tar.gz SHA256SUMS; do
        if [ -f "$ASSETS/$f" ]; then echo "  cached: $f"; continue; fi
        echo "  downloading: $f"
        curl -fL --retry 3 --connect-timeout 15 -o "$ASSETS/$f.part" "$base/$f" || die "download failed: $f"
        mv "$ASSETS/$f.part" "$ASSETS/$f"
    done
fi

say "校验"
( cd "$ASSETS" && (shasum -a 256 -c SHA256SUMS 2>/dev/null || sha256sum -c SHA256SUMS) ) || die "checksum mismatch"

say "解包"
rm -rf "$ASSETS/modules" "$ASSETS/helpers"
tar xzf "$ASSETS/modules.tar.gz" -C "$ASSETS"
tar xzf "$ASSETS/helpers.tar.gz" -C "$ASSETS"
[ -d "$ASSETS/modules" ] || die "modules archive layout unexpected"
[ -x "$ASSETS/helpers/bin/busybox" ] || die "helpers archive layout unexpected"

# ---------------------------------------------------------------- device
say "检查设备"
$ADB get-state >/dev/null 2>&1 || die "no adb device (Android booted, USB debugging on)"
model=$($ADB shell getprop ro.product.model </dev/null | tr -d '\r')
device=$($ADB shell getprop ro.product.device </dev/null | tr -d '\r')
echo "  device: $model / $device"
case "$model$device" in *7552N*|*P720S20*) ;; *) die "not a ZTE 7552N (7552N / P720S20)" ;; esac
[ "$($ADB shell 'su -c id -u' </dev/null | tr -d '\r')" = 0 ] || die "su does not return 0 (not rooted)"
slot=$($ADB shell getprop ro.boot.slot_suffix </dev/null | tr -d '\r')
[ "$slot" = "_a" ] || die "Android must run from slot _a (is '$slot')"

end=$($ADB shell 'su -c "end=0; for p in /sys/block/sda/sda*; do [ -f \$p/start ] || continue; e=\$(( \$(cat \$p/start) + \$(cat \$p/size) )); [ \$e -gt \$end ] && end=\$e; done; echo \$end"' </dev/null | tr -d '\r')
echo "  last partition end (512B sectors): $end"
[ "$end" -le 152068096 ] || die "Linux region not free (end=$end > 152068096): userdata must be shrunk first"

# ---------------------------------------------------------------- boot image
say "导出 boot_a 与 misc 头"
$ADB shell 'su -c "dd if=/dev/block/by-name/boot_a of=/data/local/tmp/boot_ori.img bs=4M"' </dev/null >/dev/null
$ADB pull /data/local/tmp/boot_ori.img "$D/boot_ori.img" >/dev/null
$ADB shell 'su -c "dd if=/dev/block/by-name/misc of=/data/local/tmp/misc-head.bin bs=4096 count=1"' </dev/null >/dev/null
$ADB pull /data/local/tmp/misc-head.bin "$D/misc-head.bin" >/dev/null

say "生成 boot 镜像"
python3 "$REPO/vendor/boot/build-boot-image.py" \
    --stock-boot "$D/boot_ori.img" \
    --misc-head "$D/misc-head.bin" \
    --kernel "$ASSETS/Image" \
    --modules "$ASSETS/modules" \
    --module-order "$REPO/vendor/boot/module-order.txt" \
    --init "$REPO/vendor/boot/init" \
    --busybox "$ASSETS/helpers/bin/busybox" \
    --logdw "$ASSETS/helpers/bin/logdw" \
    --ueventd-perms "$ASSETS/helpers/etc/ueventd-perms.sh" \
    --android-subset "$ASSETS/helpers/android" \
    --out "$WORK/boot-linux-slotb.img"

if [ "$DRY" = 1 ]; then
    say "dry-run：检查全部通过，boot 镜像已生成"
    echo "  $WORK/boot-linux-slotb.img"
    exit 0
fi

# ---------------------------------------------------------------- confirm
[ -n "$PASSWORD" ] || { printf '\n设置 root 密码: '; stty -echo 2>/dev/null || true; read -r PASSWORD; stty echo 2>/dev/null || true; echo; }
[ -n "$PASSWORD" ] || die "empty password"
HASH=$(openssl passwd -6 "$PASSWORD")

say "即将写入"
cat <<EOF
  rootfs:       $ASSETS/rootfs.tar.gz
  boot 镜像:    $WORK/boot-linux-slotb.img
  写入位置:     Linux 区（ext4 重建）+ boot_b + misc 32 字节
  系统:         OpenWrt 25.12.5 / 5.4.254
  热点:         Changxing60 / changxing60（装完请改）
  不动:         boot_a / GPT / userdata
EOF
if [ "$YES" != 1 ]; then
    printf '\n输入 INSTALL 继续: '
    read -r ok
    [ "$ok" = "INSTALL" ] || die "cancelled"
fi

# ---------------------------------------------------------------- install
say "上传并解包 rootfs"
T=/data/local/tmp
$ADB push "$ASSETS/rootfs.tar.gz" $T/changxing60-openwrt.tar.gz >/dev/null
$ADB push "$REPO/vendor/tools/android-install.sh" $T/ >/dev/null
$ADB push "$REPO/vendor/tools/android-mount-changxing60root.sh" $T/ >/dev/null

envf=$(mktemp)
cat > "$envf" <<EOF
OFF=77858865152
SIZE=50096766976
OFF_S=152068096
SIZE_S=97845248
FORMAT=1
OSES="openwrt"
WIPE_LEGACY=0
BOOT_OS=openwrt
DEFAULT_LINUX=1
IMPORT_HOTSPOT=0
PWHASH='$HASH'
EOF
$ADB push "$envf" $T/changxing60-install.env >/dev/null
rm -f "$envf"

$ADB shell 'su -c "sh /data/local/tmp/android-install.sh"' </dev/null | tee "$WORK/device-install.log"
grep -q CHANGXING60-INSTALL-OK "$WORK/device-install.log" || die "device install failed; boot_b and misc untouched"

say "写入 boot_b"
$ADB push "$WORK/boot-linux-slotb.img" $T/ >/dev/null
want=$(shasum -a 256 "$WORK/boot-linux-slotb.img" 2>/dev/null | cut -d' ' -f1 || sha256sum "$WORK/boot-linux-slotb.img" | cut -d' ' -f1)
$ADB shell 'su -c "dd if=/data/local/tmp/boot-linux-slotb.img of=/dev/block/by-name/boot_b bs=4M && sync"' </dev/null >/dev/null
got=$($ADB shell 'su -c "sha256sum /dev/block/by-name/boot_b"' </dev/null | tr -d '\r' | cut -d' ' -f1)
[ "$want" = "$got" ] || die "boot_b verify failed (want $want got $got) — slot a still boots Android"

say "写入 misc（指向 slot b）"
$ADB push "$WORK/boot-linux-slotb.misc-slot-b-trial.bin" $T/ >/dev/null
$ADB shell 'su -c "dd if=/data/local/tmp/boot-linux-slotb.misc-slot-b-trial.bin of=/dev/block/by-name/misc bs=1 seek=2048 conv=notrunc && sync"' </dev/null >/dev/null

say "完成，重启"
$ADB reboot </dev/null || true
cat <<EOF

等 60–90 秒，然后：
  ssh root@192.168.77.1 'ubus call system board | head -4; uname -r'
  # 期望 OpenWrt 25.12.5 / 5.4.254-gb50db5b6224c

热点默认：Changxing60 / changxing60    （立刻改成你自己的）
root 密码：你刚才输入的那个

回 Android：changxing60-next-boot android 然后重启
EOF
