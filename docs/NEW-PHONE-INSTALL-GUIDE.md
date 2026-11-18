# 畅行60（ZTE 7552N）新手机安装 OpenWrt —— 交接教程

> 这份文档是给**另一个 harness / 另一个 agent** 执行的。它不依赖之前的对话记录，
> 所有路径、校验值、命令、期望输出都在这里。执行时按顺序做，每一步先看输出再进下一步。

---

## 0. 交付物在哪

```
/Users/ming/Downloads/f50/
├── dist/changxing60-custom-20261118/
│   ├── rootfs.tar.gz                        125 MB  OpenWrt 25.12.5 根文件系统
│   ├── boot-linux-slotb.img                  64 MB  boot 镜像（注意：见 §2.3，必须为目标机重建）
│   ├── boot-linux-slotb.misc-slot-b-trial.bin 32 B  启动槽控制块
│   ├── INSTALL.md                                   安装说明（本文件的人读版）
│   └── SHA256SUMS.txt
├── changxing60-linux/                        安装器仓库（6Leokk/changxing60-linux @163c004）
│   ├── out/Image                             内核（从在跑设备 boot_b 提取）
│   ├── out/modules/                          167 个 .ko
│   ├── work/helpers/                         static busybox / logdw / ueventd 规则 / android 子集
│   ├── build-boot-for-target.sh              ★ 为目标机生成 boot 镜像
│   └── tools/android-install.sh              设备侧安装脚本
│   └── tools/android-mount-changxing60root.sh
└── bootimg/boot_b-current.img                参考用（在跑设备的 boot_b 备份）
```

工具链（本机已装）：

```
/Users/ming/opt/platform-tools/adb        Android Debug Bridge 1.0.41 / 37.0.1
python3                                  系统自带
openssl                                  生成密码哈希
```

校验（先做）：

```sh
cd /Users/ming/Downloads/f50/dist/changxing60-custom-20261118
shasum -a 256 -c SHA256SUMS.txt
```

期望：

```
rootfs.tar.gz: OK                                   076f4133…d219
boot-linux-slotb.img: OK                            0e9abdab…36bf（本机设备专用，目标机要重建）
boot-linux-slotb.misc-slot-b-trial.bin: OK          9e2f7c07…48e2
```

---

## 1. 先判断新手机属于哪种情况（决定能不能装）

手机切到 **Android**，USB 插上 Mac，然后：

```sh
export PATH=/Users/ming/opt/platform-tools:$PATH
adb devices -l
adb shell 'getprop ro.product.model; getprop ro.product.device; getprop ro.board.platform; getprop ro.boot.slot_suffix'
adb shell 'su -c id -u'
```

正常应看到：

```
model: ZTE 7552N
device: P720S20
platform: ums9620
slot: _a
uid: 0
```

任何一项不符就停下，不要继续：

- 不是 `7552N / P720S20` → 机型不对
- slot 不是 `_a` → 先让 Android 在 a 槽启动
- `su` 不是 0 → **没 root，走 §3**

然后判断 Linux 区是否已经腾出来：

```sh
adb shell su -c 'end=0; for p in /sys/block/sda/sda*; do [ -f $p/start ] || continue; e=$(( $(cat $p/start) + $(cat $p/size) )); [ $e -gt $end ] && end=$e; done; echo end=$end'
```

- `end <= 152068096` → **情况一**（已扩区，可以直接装）
- `end > 152068096` → **情况二**（原厂未动，去 §3）

> 152068096 是 512 字节扇区；对应字节 77858865152 / LBA 19,008,512。

---

## 2. 情况一：已 root + 已扩区

### 2.1 生成这台目标机专属的 boot 镜像（必做）

**不要直接刷 `dist/` 里的 `boot-linux-slotb.img`**——那个是用另一台手机的 `boot_a` 头做的。
boot 镜像必须用目标机自己的原厂 boot 头。

```sh
cd /Users/ming/Downloads/f50/changxing60-linux
export PATH=/Users/ming/opt/platform-tools:$PATH
./build-boot-for-target.sh
```

它会：读 `boot_a`（64 MiB）、读 `misc` 前 4 KiB → 用 `out/Image` + `out/modules` + `work/helpers`
生成 `work/boot-linux-slotb.img` 和 `work/boot-linux-slotb.misc-slot-b-trial.bin`，并打印 json。

期望输出（数值必须一致）：

```
"kernel_sha256": "03d50fec4b088555ac86c80f80d79aaccefb6587affd8d88ac6e76a748941bc5"
"ramdisk_size": 8098539
"modules": 94
```

### 2.2 安装 rootfs（不需要 Docker）

```sh
cd /Users/ming/Downloads/f50/changxing60-linux
export PATH=/Users/ming/opt/platform-tools:$PATH
T=/data/local/tmp

adb push /Users/ming/Downloads/f50/dist/changxing60-custom-20261118/rootfs.tar.gz $T/changxing60-openwrt.tar.gz
adb push tools/android-install.sh tools/android-mount-changxing60root.sh $T/

HASH=$(openssl passwd -6 '替换成新密码')
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

adb shell 'su -c "sh /data/local/tmp/android-install.sh"'
```

期望最后一行出现：

```
[device] CHANGXING60-INSTALL-OK
```

> `FORMAT=1` 会 **重建 Linux 区里的 ext4**（该区原有内容清空），但**不碰** Android 的
> `userdata`、GPT、`boot_a`。`android-install.sh` 自己会再检查一次区域与分区是否重叠，
> 重叠就退出（这种情况说明是 §3 的情况二）。

### 2.3 写 boot_b 和 misc

```sh
cd /Users/ming/Downloads/f50/changxing60-linux
export PATH=/Users/ming/opt/platform-tools:$PATH
T=/data/local/tmp
IMG=work/boot-linux-slotb.img
BC=work/boot-linux-slotb.misc-slot-b-trial.bin

adb push $IMG $T/
adb shell "su -c 'dd if=$T/boot-linux-slotb.img of=/dev/block/by-name/boot_b bs=4M && sync'"

# 校验 boot_b 与本地镜像一致
adb shell 'su -c "sha256sum /dev/block/by-name/boot_b"' | cut -d' ' -f1
shasum -a 256 $IMG | cut -d' ' -f1
# 两个值必须相同，否则停下，不要写 misc

adb push $BC $T/
adb shell "su -c 'dd if=$T/boot-linux-slotb.misc-slot-b-trial.bin of=/dev/block/by-name/misc bs=1 seek=2048 conv=notrunc && sync'"

adb reboot
```

### 2.4 装完验证（约 60–90 秒后）

```sh
ssh root@192.168.77.1 'ubus call system board | head -4; uname -r; uptime'
```

期望：

```
"kernel": "5.4.254-gb50db5b6224c"
"release": { "distribution": "OpenWrt", "version": "25.12.5" ... }
5.4.254-gb50db5b6224c
```

再查：

```sh
ssh root@192.168.77.1 'ip -4 -br addr'                 # br-lan 192.168.77.1 / br-wifi
ssh root@192.168.77.1 'ping -c3 223.5.5.5 | tail -2'   # 0% loss
ssh root@192.168.77.1 "nft list ruleset | grep -A4 'flowtable ft'"
# 期望 devices = { br-lan, br-wifi, sipa_eth0, usb0, wlan0 }
# 若只有 br-lan/br-wifi：ssh root@192.168.77.1 /etc/flowtable-on.sh
```

最后必须做两件事（这个 rootfs 沿用了上一台机器的凭据）：

```sh
ssh root@192.168.77.1 'passwd'
ssh root@192.168.77.1 'rm -f /etc/dropbear/dropbear_*_host_key; /etc/init.d/dropbear restart'
```

---

## 3. 情况二：原厂未动（userdata 占满全盘）

`android-install.sh` 会返回：

```
region overlaps partitions or the backup GPT
```

这是**设计如此**，不是 bug。原厂机必须先做两步，而且第一步就不可逆：

### 3.1 先 root

不属于本项目范围。要点：

- Magisk 给 boot 打补丁，经 SPD/BROM 写入
- **先把救砖通道打通**：Windows + SPD/Unisoc Flash Tool + 原厂固件 + 本机 GPT 备份
- 参考仓库：`windy-0-0/zte-rescue-kit`、`windy-0-0/zte-firmware`、
  `byf3332/ZTE-7552N-P720S20-devicetree`、`v225hy/Android_device_ZTE_7552N_orangefox_device_tree`

### 3.2 缩 userdata（会清空 Android 数据）

| 项 | 值 |
|---|---|
| userdata 起点 | 扇区 2,230,784 |
| userdata 新终点 | LBA 19,007,999（64 GiB） |
| Linux 区起点 | 字节 77,858,865,152 / 扇区 152,068,096 / LBA 19,008,512 |
| Linux 区大小 | 50,096,766,976 字节（46.6 GiB） |
| Linux 区终点 | LBA 31,239,167（距备份 GPT 2 MiB） |

- 主 GPT 与备份 GPT **两份都要改**并重算 CRC，只改一份会被判坏盘
- 改完抹掉 userdata 头部的 f2fs 超级块，让 Android recovery 重建
- 失败 → bootloop → 只能 SPD 恢复 GPT + 重刷
- 参考实现：`6Leokk/zte7552n` 仓库的 `rescue/tools/gpt-shrink-userdata.py`、
  `docs/changxing60/port-checklist.md`

缩完重新设置 Android、开 USB 调试、把 root 找回来，然后从 §2.1 开始。

---

## 4. 回滚 / 救砖

| 场景 | 操作 |
|---|---|
| 想回 Android | `changxing60-next-boot android` 后重启（LuCI：服务 → 启动目标） |
| Linux 起不来 | 连续两次失败自动回 Android |
| 卸载 Linux | `./uninstall.sh`（quick / secure / keep） |
| boot 坏了 | 重刷原厂 `boot_a`/`boot_b`（dd 或 SPD） |
| 整机砖 | SPD/BROM + 原厂固件（Windows） |

---

## 5. 红线（违反会变砖或丢数据）

1. **绝不 `sysupgrade`**，绝不刷通用 arm64 OpenWrt 镜像 —— 会毁分区表。
2. 不要跨机复用 `boot-linux-slotb.img`：boot 头必须来自目标机。
3. 不要写 `boot_a`、不要改 GPT/userdata（除非走 §3.2 并获得明确授权）。
4. `apk upgrade` 可以，但要排除 `kernel` 和 `kmod-*`。
5. 首次启动是试启动，70 秒内不要连着重启两次。
6. 任何一次 `fw4 reload` 都会冲掉 flowtable（速度会掉），跑 `/etc/flowtable-on.sh` 恢复。

---

## 6. 给执行 agent 的规范

- 每一步执行完**先读输出**，与本文的期望值比对；不符就停，不要"重试到成功"。
- 涉及写入（`android-install.sh`、`dd boot_b`、`dd misc`）前，把要执行的原样命令贴给用户确认。
- 记录每一步的 sha256 / 返回值，最后汇总。
- 不确定的（尤其是 §3.2 缩容）**不要自己发挥**，交回人类决定。
- 本机路径和设备序列号可能变（`adb devices -l` 确认），不要硬编码序列号。
