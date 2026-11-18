# 与上游项目的差异

本文说明本仓库**改了什么、为什么改、优化了什么**。代码层面本仓库不复制上游源码，只提供叠加层
（脚本 + 文档 + 设备侧调优记录）。

## 0. 来源

| 项 | 值 |
|---|---|
| 直接上游 | [6Leokk/changxing60-linux](https://github.com/6Leokk/changxing60-linux) @ `163c004`（2026-10-18） |
| 上游的上游 | [dikeckaan/mu300-linux](https://github.com/dikeckaan/mu300-linux)（F50 / MU300 / U30 Air，同 UMS9620 平台） |
| 目标设备 | ZTE 7552N（中兴畅行60，`P720S20`，`ums9620`，Android 13） |
| 上游许可 | MIT（脚本/文档） / GPL-2.0（内核与驱动） |

上游已经把「在 Android 之外跑 OpenWrt」做成可用状态：slot b 启动、`misc` 32 字节切换、
固定分区几何、rootfs 构建、boot 镜像生成。本仓库没有推翻这些设计，只是把**落地成本**降下来，
并把**网络性能**往上推了一档。

---

## 1. 构建与安装链路上的修改

### 1.1 内核：不重新编译，从设备里取

| | 上游做法 | 本仓库做法 |
|---|---|---|
| 内核来源 | `kernel/build-linux.sh`：ZTE 5.4 GPL 源码 + fragment + ThinLTO，Docker 内编译 | 从一台在跑的 7552N 的 `boot_b` 里提取**未压缩 arm64 Image** |
| 依赖 | Docker、ZTE GPL 源码树、realme C51/C53 的 `wlan_combo`、mali r40p0 | `python3` 一个脚本 |
| 耗时 | 首次拉源码 + 编译，小时级 | 秒级 |
| 产物 | `out/Image` + `out/modules` | `out/Image`（32,002,560 B，`Linux version 5.4.254-gb50db5b6224c #3`，sha256 `03d50fec…1bc5`）+ `out/modules`（167 个 .ko，boot 需要其中 94 个） |

为什么能这么取：这个项目的 boot 镜像是 Android boot header v4，`kernel_size` 段里放的就是**未压缩的
Image**（魔数 `4d5a0091`），不是压缩内核。提取后 `strings` 里的版本串与设备 `uname -r` 一致，
说明拿到的就是正在运行的那个内核，不存在 ABI/vermagic 错配。

**代价**：这要求先有一台已装好的同型号设备。没有的话仍应走上游的编译路径。

### 1.2 boot 镜像辅助文件：从 ramdisk 里取

上游 `install.sh` 会用 Docker 现场构建 `busybox`、`logdw`、`bt-init`，并从目标机 Android 抽子集。

本仓库改为从**已装好的 boot 镜像的 ramdisk**（LZ4 压缩的 cpio）里解出：

| 文件 | 大小 | 说明 |
|---|---|---|
| `bin/busybox` | 1,981,032 B | static aarch64，initramfs 的 shell |
| `bin/logdw` | 608,888 B | static aarch64，持久化内核日志 |
| `etc/ueventd-perms.sh` | 56,421 B | 设备节点权限规则 |
| `android/` | 51 MB | Android 运行子集（Bionic、vendor libs） |

于是**整条链不再需要 Docker**（`install.sh` 的 helper 构建步骤被绕过）。

### 1.3 新增脚本

| 文件 | 作用 | 相对上游 |
|---|---|---|
| `scripts/build-boot-for-target.sh` | 连上目标机（Android、已 root），导 `boot_a` + `misc` 头，一条命令生成属于**这台机器**的 boot 镜像 | 上游要手工拼 `boot/build-boot-image.py` 的 8 个参数 |
| `scripts/install-new-phone.sh` | 跑上游 `install.sh` 前的闸门：工具链、rootfs 校验、`out/` 产物、dumps、设备 root/slot | 上游把检查散落在 `install.sh` 里，且要求 Docker |

`build-boot-for-target.sh` 明确拒绝跨机复用镜像——boot header 必须来自目标机自己的 `boot_a`。

### 1.4 预置 rootfs（不随仓库分发）

从在用设备整机备份得到（OpenWrt 25.12.5、190 个 apk），并做了清理：

- 剔除 `6.18.56` 模块（试验残留）
- 剔除 `/root/.ssh`、`/etc/dropbear/authorized_keys`（个人公钥）
- 剔除 `/root/diag/*.log`（9.5 MB 诊断日志）
- 打包结果 125 MB，`076f4133…d219`

不放进仓库的原因见 README：体积、凭据、厂商固件权利。

---

## 2. 设备侧的性能优化

这部分**不在代码仓库里**，是装好之后在设备上做的调优，rootfs 备份里带着。

### 2.1 问题定位

同一张 SIM、同一个小区，Android 能跑 300+ Mbps，OpenWrt 只有约 200 Mbps。实测到的瓶颈：

| 观察 | 值 |
|---|---|
| `sipa_eth0` 队列 | 只有 `rx-0`/`tx-0`，`ethtool -l` 不支持多通道 |
| SIPA 中断 | `sprd,multi-sipa-0` 全部落在 CPU0；`multi-sipa-1`、`general_sipa` 计数为 0 |
| 软中断 | `NET_RX` 几乎全在 CPU0（A55 @768 MHz） |
| 卸载能力 | `rx-checksumming: off`、TSO `off [fixed]` |
| 加速表 | `nft list ruleset \| grep offload` = **0**，每个包都过 conntrack + NAT |
| 硬件流处理 | `sfp_core` 已加载但引用计数 0（Android 那条硬件 NAT 路径没被用起来） |

结论：200 Mbps 不是空口上限，是**单核软件转发**的上限。

### 2.2 做了三件事

1. **软件 flow offload 真正生效**
   - 上游/默认 fw4 生成的 `flowtable ft` 只有 `{ br-lan, br-wifi }`，缺真实网口——Wi-Fi 流量根本进不了表。
   - 修 `fw4.uc` 的设备收集逻辑，把 `sipa_eth0 / usb0 / wlan0` 补进 flowtable。
   - 解决两个坑：① fw4 比 `wlan0` 起得早，开机建表时设备还不存在；② 表已存在时 `fw4 reload` 会
     `Resource busy`（设备 detach 竞态），删旧建新必失败。
   - 做法：开机 `flow_offloading=0` 不建表 → 等 `wlan0` + `sipa_eth0` 都起来 → 临时置 1 → `fw4 reload`
     新建（五设备齐全）→ 再置回 0，保证下次开机仍是干净状态。

2. **RPS**：`sipa_eth0` 的 `rx-0` 设为 `0x80`，收包软中断从 CPU0 挪到 CPU7（A76）。

3. **CPU governor**：全部 `performance`，CPU0 从 768 MHz 拉到 2002 MHz。

效果：用户实测**超过 200 Mbps**。flow offload 是主要贡献（去掉每包 netfilter/NAT），RPS 与 governor
帮助收包侧和首包处理。

### 2.3 附带产物

| 文件 | 作用 |
|---|---|
| `/etc/hotplug.d/iface/99-flowtable` | 开机钩子：等接口就绪 → 建表 → 设 RPS/governor |
| `/etc/flowtable-on.sh` | 任何一次 `fw4 reload` 之后手动恢复 flowtable |

已知限制：任何防火墙重载都会冲掉 flowtable，需要重跑 `flowtable-on.sh`；这是 fw4 的重建竞态导致的，
上游同样存在。

---

## 3. 没有改的部分

- 启动机制（slot b + `misc` 32 字节 + 试启动回落）——原样沿用
- 分区几何（`OFF=77858865152` / `SIZE=50096766976`）——原样沿用
- 内核补丁、驱动移植、rootfs 构建脚本——原样沿用，未做修改
- 上游 `install.sh` / `uninstall.sh`——未修改，只用环境变量（`CHANGXING60_REUSE_BUILD` 等）驱动

---

## 4. 风险与适用边界

- 这套做法把「编内核」变成「信任一台已装好的同型号设备」。内核来源变了，但产物与设备上运行的完全一致。
- 原厂未动的机器仍需先 root + 缩 userdata（不可逆，会清 Android 数据），这部分本仓库不提供自动化。
- flow offload 与 TTL/mangle 规则天然冲突（已建立连接绕过 netfilter）。本项目设备上没有 TTL 规则，
  如果你的用途依赖改 TTL，不要开 offload。
