# 最后验证过的版本

仓库现在**只保留一个 Release**，所以这份文件记录"当前这版到底验证过什么"。
回退时如果没法从 GitHub 拿到旧包，以这里的校验和与检查项为准。

## v1.1.8 — 2026-11-20

| 资源 | SHA256 |
|---|---|
| `rootfs.tar.gz` | `2571f2b03f5737385ffa3ea653b00d73d689d5e466374e38d248b63cbff698b6` |
| `Image` | `03d50fec4b088555ac86c80f80d79aaccefb6587affd8d88ac6e76a748941bc5` |
| `modules.tar.gz` | `f68dfd1319339a95f448ff1d75030d13b7c060f9cb09c328fca899fd7ffa7103` |
| `helpers.tar.gz` | `05a29ca0d5b4c5b603d0220cb7a791490337c75a9df2e0c8b4a66b773d6fead8` |

### 已验证

- 从 GitHub 下载 → 校验 → 重建 ext4 → 解包 → 写 boot_b / misc → 启动（v1.0.6 起端到端验证过，流程未变）；
- OpenWrt 25.12.5 / 内核 5.4.254-gb50db5b6224c；
- 面板二进制 `f060645bd425e89a23c9bcdfa9f45086`；
  内屏各页、电源菜单、热点面板、温度面板、ShellCrash 节点/延时/流量图均实机使用过；
- CPU 调速器 `schedutil`、充电上限 80%（`charge-cap=800`）、flow offload、`usb0` hotplug 修复；
- 镜像内无订阅配置、无 SSH 公钥、无诊断日志。

### 已知问题（v1.1.8 仍在）

- 启动偶尔卡在 `switch_root` 之后（约一半概率），强制重启一次通常可进；
- 内核每次启动打印 `BUG: Bad page state`（显示驱动重复释放 logo 页）与 `unknown esd check mode:4`，属驱动问题；
- 日志被厂商驱动噪音刷屏（`sprd_wdt_fiq`、`charger_policy dc psy`、`WCN BASE`、`sc27xx-fgu`）；
- `load average` 因内核 D 状态线程偏高（面板已改为显示 idle%）。

### 安装后自查

```sh
ssh root@192.168.77.1 '
  uname -r
  grep DISTRIB_RELEASE /etc/openwrt_release
  sha256sum /usr/bin/changxing60-screen
  cat /opt/changxing60/charge-cap
  cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
  nft list ruleset | grep -A3 "flowtable ft"
'
```

期望：`5.4.254-gb50db5b6224c`、`25.12.5`、`b554fa09…`（面板）、`800`、`schedutil`、flowtable 里含 `sipa_eth0`。
