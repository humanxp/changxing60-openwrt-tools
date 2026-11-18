# changxing60-openwrt-tools

ZTE 7552N（中兴畅行60）安装 OpenWrt 的辅助工具与文档。基于
[6Leokk/changxing60-linux](https://github.com/6Leokk/changxing60-linux)（MIT / GPL-2.0）。

本仓库**只放脚本和文档**，不含任何镜像、内核、rootfs 或厂商二进制。

## 内容

| 路径 | 说明 |
|---|---|
| `docs/NEW-PHONE-INSTALL-GUIDE.md` | 交接给另一个 agent/人执行的完整教程（自包含，带期望输出与红线） |
| `docs/INSTALL-NEW-PHONE.zh-CN.md` | 安装说明：已 root+已扩区 / 原厂未动 两种情况 |
| `scripts/build-boot-for-target.sh` | 用目标机自己的 `boot_a` 头，现场生成 boot 镜像（不需要 Docker） |
| `scripts/install-new-phone.sh` | 走上游 `install.sh` 的包装脚本（需要 Docker，做前置检查） |

## 它解决什么问题

上游 `install.sh` 的启动镜像是**按机器生成**的：必须用目标机自己的原厂 `boot_a` 头。
同时它要求先编出 `out/Image` + `out/modules`，通常意味着一次完整的 5.4 内核交叉编译。

这套脚本里，内核和 boot 辅助二进制可以直接从一台已经在跑的 7552N 上取：

- `out/Image`：从 `boot_b` 的 Android boot header 里提取（未压缩 arm64 Image，`Linux version 5.4.254-…`）
- `out/modules`：`/lib/modules/5.4.254-…/*.ko`
- `work/helpers/`：boot ramdisk 里的 static busybox、logdw、ueventd 规则、Android 子集

于是目标机只需要 **adb + python3 + openssl**，不用 Docker、不用重新编译内核。

## 前置条件

- 目标机是 ZTE 7552N（`ro.product.device=P720S20`，`ro.board.platform=ums9620`）
- Android 在 **slot a** 启动，已 root（`su -c id -u` 返回 0），USB 调试打开
- 主机有 `adb`、`python3`；生成密码哈希用 `openssl passwd -6`
- **Linux 区已腾出**（userdata 已缩到 64 GiB）；原厂未动的机器要先 root + 缩容，见文档 §3

## 快速开始

```sh
git clone https://github.com/6Leokk/changxing60-linux.git
cd changxing60-linux
# 把本仓库的 scripts/ 拷进去，或直接照 docs/ 里的命令执行

# 1) 取内核与辅助文件（从一台已装好的 7552N，见 docs §1）
# 2) 生成目标机 boot 镜像
./build-boot-for-target.sh
# 3) 安装（手动路径，不需要 Docker）
#    见 docs/INSTALL-NEW-PHONE.zh-CN.md §2.4
```

## 为什么仓库里没有镜像

1. **体积**：rootfs 约 125 MB、boot 镜像 64 MB，超过 GitHub 单文件 100 MB 限制。
2. **凭据**：从设备备份出来的 rootfs 含 SSH 主机密钥、root 密码哈希、热点 PSK，不能公开。
3. **第三方权利**：rootfs 含展锐/中兴的 Wi-Fi、基带固件，不属于本项目，不随仓库分发。

需要镜像请自行从自己的设备生成；流程见文档。

## 免责声明

这是一个实验性工具集，没有任何担保。写 `boot_b`、`misc`、缩容分区都可能让设备无法启动，
恢复需要 SPD/BROM 与厂商固件。是否动手、后果如何，由设备所有者自行承担。

## 许可

脚本与文档：MIT。
上游 `changxing60-linux` 的代码与内核补丁：MIT / GPL-2.0，版权归其作者。
