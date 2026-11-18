# 新手机安装步骤（畅行60 / ZTE 7552N）

本目录已备好一套可直接安装的产物：

- `work/changxing60-openwrt.tar.gz` —— 2026-11-18 从一台在用的畅行60 整机备份的 rootfs
  （OpenWrt 25.12.5、5.4.254、190 个 apk、含 flow offload 调优）
- `out/Image` + `out/modules/` —— 与该 rootfs 配套的**现成内核**，从同一台机器的 `boot_b` 里取出
- `work/helpers/` —— boot ramdisk 里的辅助二进制（static busybox、logdw、ueventd 规则、Android 子集）

**不需要重新编译内核**：`out/Image` 就是设备上正在跑的那个（`Linux version 5.4.254-gb50db5b6224c #3`，
sha256 `03d50fec…1bc5`）。**也不一定需要 Docker**：手动安装路径只用到 adb + python3 + openssl。

---

## 0. 先判断走哪条

| 新手机状态 | 能否直接装 | 去哪节 |
|---|---|---|
| 已 root，userdata 已缩小、尾部有 64 GiB 空闲区 | ✅ | **2. 情况一** |
| 原厂未动（userdata 占满全盘） | ❌ | **3. 情况二** |

判断命令（Android 侧，需 root）：

```sh
adb shell su -c 'end=0; for p in /sys/block/sda/sda*; do [ -f $p/start ] || continue; e=$(( $(cat $p/start) + $(cat $p/size) )); [ $e -gt $end ] && end=$e; done; echo end=$end'
```

Linux 区起点是 **512B 扇区 152068096**（字节 77858865152，LBA 19,008,512）。

- `end <= 152068096` → 情况一
- `end > 152068096` → 情况二

---

## 1. 已就位的产物与校验

```sh
shasum -a 256 work/changxing60-openwrt.tar.gz   # 076f4133…d219
shasum -a 256 out/Image                         # 03d50fec…1bc5
ls out/modules | wc -l                          # 167 个 .ko（boot 需要其中 94 个）
ls work/helpers/bin                            # busybox / logdw（都是 static aarch64）
```

`out/Image` 的版本串可以用 `strings out/Image | grep 'Linux version'` 复核。

---

## 2. 情况一：已 root + 已扩区

### 2.1 电脑准备

```sh
adb version          # Android platform-tools（本机已装在 ~/opt/platform-tools/adb）
python3 --version
lz4 --version        # 只用于处理 boot 镜像，可选
openssl version      # 生成密码哈希
```

### 2.2 设备准备

- Android 正常启动在 **slot a**（`getprop ro.boot.slot_suffix` = `_a`）
- USB 调试打开，`adb devices` 能看到
- `adb shell su -c 'id -u'` 返回 **0**

### 2.3 生成这台手机专属的 boot 镜像

boot 镜像必须用**这台手机自己的原厂 boot 头**，所以最后一步要在目标机上做：

```sh
./build-boot-for-target.sh
```

脚本会：导出 `boot_a`（原厂）与 `misc` 头 → 用 `out/Image` + `out/modules` + `work/helpers`
生成 `work/boot-linux-slotb.img` 和 `work/boot-linux-slotb.misc-slot-b-trial.bin`。

> 已经用本机现有 boot 镜像做过一次完整构建验证：输出 64 MB，ramdisk 8,098,539 字节，94 个模块，
> `misc_slot_b_trial_hex = 5f6200004243414201020000fe002f…`。

### 2.4 安装（手动路径，不需要 Docker）

```sh
T=/data/local/tmp
adb push work/changxing60-openwrt.tar.gz $T/
adb push tools/android-install.sh tools/android-mount-changxing60root.sh $T/

HASH=$(openssl passwd -6 '你的新密码')
cat > /tmp/c60.env <<EOF
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
adb push /tmp/c60.env $T/changxing60-install.env

# 解包 rootfs 到 Linux 区（会先 mkfs ext4；FORMAT=1 时该区原有内容清空）
adb shell 'su -c "sh /data/local/tmp/android-install.sh"'

# 写 boot_b 并校验
adb push work/boot-linux-slotb.img $T/
adb shell 'su -c "dd if=/data/local/tmp/boot-linux-slotb.img of=/dev/block/by-name/boot_b bs=4M && sync"'

# 写 misc 的 32 字节启动控制，指向 slot b
adb push work/boot-linux-slotb.misc-slot-b-trial.bin $T/
adb shell 'su -c "dd if=/data/local/tmp/boot-linux-slotb.misc-slot-b-trial.bin of=/dev/block/by-name/misc bs=1 seek=2048 conv=notrunc && sync"'

adb reboot
```

### 2.5 或者用仓库的 install.sh（需要 Docker）

```sh
CHANGXING60_REUSE_BUILD=1 ./install.sh
```

它会复用 `work/changxing60-openwrt.tar.gz`，但仍会用 Docker 编译 busybox/logdw 等 helper、
重新生成 boot 镜像。**Docker Desktop 需要管理员权限**；没有的话走 2.4。

### 2.6 装完验证

```sh
ssh root@192.168.77.1 'ubus call system board | head -4; uname -r'
# 期望 OpenWrt 25.12.5 / 5.4.254-gb50db5b6224c

ssh root@192.168.77.1 'ping -c3 223.5.5.5 | tail -2'
ssh root@192.168.77.1 "nft list ruleset | grep -A4 'flowtable ft'"
# 期望 devices 含 br-lan/br-wifi/sipa_eth0/usb0/wlan0；没有就跑 /etc/flowtable-on.sh
```

装完立刻改密码、换 SSH 主机密钥：

```sh
ssh root@192.168.77.1 'passwd; rm -f /etc/dropbear/dropbear_*_host_key; /etc/init.d/dropbear restart'
```

---

## 3. 情况二：原厂未动

安装会被拒绝（`region overlaps partitions`），必须先做两步，第一步就不可逆。

### 3.1 root

不属于本项目范围。社区做法是 Magisk 打补丁 + SPD/BROM 写入，参考：

- `windy-0-0/zte-rescue-kit`、`windy-0-0/zte-firmware`（固件与救砖）
- `byf3332/ZTE-7552N-P720S20-devicetree`、`v225hy/Android_device_ZTE_7552N_orangefox_device_tree`
- 酷安 / CSDN 上的 7552N root 教程

**先把救砖通道打通**（Windows + SPD 工具 + 原厂固件 + GPT 备份），没有就别动。

### 3.2 缩 userdata，腾出 64 GiB

**会清空 Android 数据。** 目标几何：

| 项 | 值 |
|---|---|
| userdata 起点 | 扇区 2,230,784 |
| userdata 新终点 | LBA 19,007,999（64 GiB） |
| Linux 区起点 | 字节 77,858,865,152 / 扇区 152,068,096 / LBA 19,008,512 |
| Linux 区大小 | 50,096,766,976 字节（46.6 GiB） |
| Linux 区终点 | LBA 31,239,167（距备份 GPT 2 MiB） |

要点：主备 GPT 两份都要改并重算 CRC；改完抹 userdata 头部 f2fs 超级块；失败 → bootloop → SPD 恢复。
参考 `6Leokk/zte7552n` 的 `rescue/tools/gpt-shrink-userdata.py` 与 `docs/changxing60/port-checklist.md` §3。

### 3.3 回到情况一

重新设置 Android、开 USB 调试、找回 root，然后从 §2.3 开始。

---

## 4. 回滚与救砖

| 场景 | 操作 |
|---|---|
| 回 Android | `changxing60-next-boot android` 后重启 |
| Linux 起不来 | 连续两次失败自动回 Android |
| 卸载 | `./uninstall.sh`（quick/secure/keep） |
| boot 坏了 | 重刷原厂 boot_a/boot_b（dd 或 SPD） |
| 整机砖 | SPD/BROM + 原厂固件 |

---

## 5. 已知坑

1. **不要用 `sysupgrade`**，不要刷通用 arm64 OpenWrt 镜像。
2. `apk upgrade` 可以，排除 `kernel` / `kmod-*`。
3. 装完任何一次 `fw4 reload` 都会冲掉 flowtable，跑 `/etc/flowtable-on.sh` 恢复。
4. 首次启动是试启动，70 秒内别连着重启两次。
5. 本 rootfs 带着上一台机器的 `/etc/config`（LAN 192.168.7.1、USB 管理口 192.168.77.1）、
   root 密码哈希和 SSH 主机密钥——装完立刻改。
6. boot 镜像**不能跨机复用**：`build-boot-for-target.sh` 必须在那台目标手机上跑。
