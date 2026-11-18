# Vendored upstream files

以下文件来自 [6Leokk/changxing60-linux](https://github.com/6Leokk/changxing60-linux) @ `163c004`，
其许可为 MIT（脚本部分）。原样保留，仅用于让安装脚本自包含、无需再克隆上游仓库。

| 文件 | 上游路径 |
|---|---|
| `boot/init` | `boot/init` |
| `boot/build-boot-image.py` | `boot/build-boot-image.py` |
| `boot/module-order.txt` | `boot/module-order.txt` |
| `tools/android-install.sh` | `tools/android-install.sh` |
| `tools/android-mount-changxing60root.sh` | `tools/android-mount-changxing60root.sh` |

版权归原作者；本仓库只做分发。

## 关于 `boot/init`

这里的 `init` 取自**已在 7552N 上实测可用**的 boot 镜像（ramdisk），而不是上游 HEAD 的版本。
原因：上游 HEAD 的 init 在本机实测会在 `switch_root` 之后起不来（NCM 有、DHCP 无），
换回这个版本后同一条 rootfs 正常启动（见仓库 README 的验证记录）。
