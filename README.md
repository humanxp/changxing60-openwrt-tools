# changxing60-openwrt-tools

ZTE 7552N（中兴畅行60）安装 OpenWrt 的**叠加工具与文档**。

站在 [6Leokk/changxing60-linux](https://github.com/6Leokk/changxing60-linux) @ `163c004` 之上
（它本身 fork 自 [dikeckaan/mu300-linux](https://github.com/dikeckaan/mu300-linux)，同为展锐 UMS9620 平台）。

> 本仓库不复制上游代码，只提供脚本、文档和设备侧调优记录。
> 细微节说明：[`docs/CHANGES-VS-UPSTREAM.md`](docs/CHANGES-VS-UPSTREAM.md)

---

## 改了什么

| # | 上游做法 | 这里改成 | 收益 |
|---|---|---|---|
| 1 | `kernel/build-linux.sh`：Docker 里编译 ZTE 5.4 GPL 源码 + wlan/mali 出树模块，小时级 | 从一台已装好的 7552N 的 `boot_b` 里提取**未压缩 arm64 Image**（32,002,560 B）+ `/lib/modules/*.ko` | 秒级拿到与设备**完全一致**的内核（`5.4.254-gb50db5b6224c #3`，sha256 `03d50fec…`），无 ABI 错配 |
| 2 | `install.sh` 用 Docker 现场编 `busybox` / `logdw` / `bt-init` | 从已装好的 boot ramdisk（LZ4 cpio）里解出 static arm64 的 `busybox`、`logdw`、`ueventd-perms.sh`、Android 子集 | **整条链不再需要 Docker** |
| 3 | 手工拼 `boot/build-boot-image.py` 的 8 个参数 | 新增 `scripts/build-boot-for-target.sh`：导 `boot_a` + `misc` 头，一条命令出镜像 | 少踩参数坑，且强制 boot header 来自**目标机自己** |
| 4 | 检查散在 `install.sh` 里、且要求 Docker | 新增 `scripts/install-new-phone.sh`：工具链 / rootfs 校验 / `out/` / dumps / 设备 root / slot 全部先过一遍 | 失败在写入之前，不动机器 |
| 5 | 无 | 预置 rootfs（从在用设备整机备份，剔除 6.18 残留模块、个人公钥、诊断日志） | 装出来就是调优过的状态 |
| 6 | 无 | 两份文档：agent 交接教程 + 人读安装说明（区分「已 root+已扩区」与「原厂未动」） | 换人/换 agent 也能复现 |

## 优化了什么（设备侧，结果在 rootfs 备份里）

同卡同小区，Android 300+ Mbps，OpenWrt 只有约 200 Mbps。定位到瓶颈是**单核软件转发**：
`sipa_eth0` 只有一个队列、SIPA 中断全在 CPU0（A55 @768 MHz）、`nft` 里没有任何 offload、
`sfp_core` 加载了但引用计数为 0。

做了三件事：

1. **让软件 flow offload 真正生效** —— 默认生成的 `flowtable ft` 只含 `{ br-lan, br-wifi }`，
   缺真实网口，Wi-Fi 流量进不了表。补上 `sipa_eth0 / usb0 / wlan0`，并解决
   「fw4 比 wlan0 起得早」和「表已存在时 reload 报 Resource busy」两个时序/竞态问题
   （开机不建表 → 接口齐全后临时开启重建 → 再置回 0）。
2. **RPS** —— `sipa_eth0` 的 `rx-0` 设 `0x80`，收包软中断从 CPU0 挪到 CPU7（A76）。
3. **CPU governor** —— 全部 `performance`，CPU0 768 MHz → 2002 MHz。

结果：实测**超过 200 Mbps**。flow offload 是主要贡献，RPS/governor 帮收包与首包。

附带两个设备侧文件：`/etc/hotplug.d/iface/99-flowtable`（开机钩子）和
`/etc/flowtable-on.sh`（任何 `fw4 reload` 之后恢复 offload）。

## 没改什么

- 启动机制（slot b + `misc` 32 字节 + 试启动回落）
- 分区几何（`OFF=77858865152` / `SIZE=50096766976`）
- 内核补丁、驱动移植、rootfs 构建脚本
- 上游 `install.sh` / `uninstall.sh`——未改一行，只用环境变量驱动

## 仓库内容

| 路径 | 说明 |
|---|---|
| `docs/CHANGES-VS-UPSTREAM.md` | 与上游的逐项差异、优化前后证据 |
| `docs/NEW-PHONE-INSTALL-GUIDE.md` | 交接给另一个 agent/人执行的完整教程（自包含、带期望输出与红线） |
| `docs/INSTALL-NEW-PHONE.zh-CN.md` | 安装说明：已 root+已扩区 / 原厂未动 两种情况 |
| `scripts/build-boot-for-target.sh` | 用目标机自己的 `boot_a` 头生成 boot 镜像（不需要 Docker） |
| `scripts/install-new-phone.sh` | 走上游 `install.sh` 的包装（需要 Docker，做前置检查） |

## 使用前提

- 目标机是 ZTE 7552N（`ro.product.device=P720S20`，`ro.board.platform=ums9620`）
- Android 在 **slot a** 启动，已 root（`su -c id -u` = 0），USB 调试打开
- 主机有 `adb`、`python3`、`openssl`
- **Linux 区已腾出**（userdata 已缩到 64 GiB）。原厂未动的机器要先 root + 缩容——
  这一步不可逆、会清 Android 数据，本仓库不提供自动化。

具体命令见 `docs/INSTALL-NEW-PHONE.zh-CN.md`。

## 为什么仓库里没有镜像

1. **体积**：rootfs 约 125 MB、boot 镜像 64 MB，超过 GitHub 单文件 100 MB 限制。
2. **凭据**：设备备份出来的 rootfs 含 SSH 主机密钥、root 密码哈希、热点 PSK。
3. **第三方权利**：rootfs 含展锐/中兴的 Wi-Fi、基带固件，不属于本项目。

镜像请从自己的设备生成，流程见文档。

## 许可与免责

脚本与文档：MIT。上游代码与内核补丁：MIT / GPL-2.0，版权归其作者。

实验性工具，无担保。写 `boot_b`、`misc`、缩分区都可能让设备无法启动，恢复需要 SPD/BROM
与厂商固件；后果由设备所有者承担。
