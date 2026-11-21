# 无 SIM / 断网时的 WAN 状态自愈

`/opt/changxing60/bin/wan-watch`，由 cron 每 2 分钟执行一次。

## 行为

| 检测 | 动作 |
|---|---|
| 无 SIM（`AT+CPIN?` 非 READY） | 清掉 `sipa_eth0` 的旧 IP 与默认路由；`AT+CFUN=0` 关闭射频（只执行一次） |
| SIM 插回 | `AT+CFUN=1` 打开射频；`ubus call network.interface.wan up` 重新拨号 |
| 有 IP 但连续 3 次不通 | 重启 WAN 接口 |

日志：`/tmp/wan-watch.log`。

## 为什么需要

拔卡后基带不会立刻释放数据接口，`sipa_eth0` 会保留旧 IP 和默认路由，导致：

- 系统和客户端以为 WAN 是好的，访问外网先送进一个不通的网关，表现为**卡很久然后超时**；
- 基带在无卡状态下持续搜网（`+CGREG: 2`），是明显的耗电来源。

清掉旧状态后，访问会**立即失败**（`Network unreachable`），并且射频关闭、省电。

## 手动执行

```sh
/opt/changxing60/bin/wan-watch
cat /tmp/wan-watch.log
```

## 插卡后

最多 2 分钟自动恢复；急的话手动跑一次 `wan-watch`，或 `/etc/init.d/network restart`。
