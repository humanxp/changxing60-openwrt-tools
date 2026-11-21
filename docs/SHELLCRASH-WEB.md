# ShellCrash 网页端

只有一个功能：**粘贴订阅地址 → 更新节点**。

## 地址

| 网络 | 地址 |
|---|---|
| WiFi 热点 | `http://192.168.7.1/cgi-bin/shellcrash-ui` |
| USB | `http://192.168.77.1/cgi-bin/shellcrash-ui` |

LuCI 的 **服务 → ShellCrash** 也是这个页面。

## 用法

1. 打开页面，输入框里已经填好当前订阅地址；
2. 换成新地址（或保持原样）后点「更新节点」；
3. 按钮变成「正在更新…」，页面每 4 秒自动刷新；
4. 完成后显示 `上次更新：17:17 成功`（失败为红色）。

更新过程约 30–60 秒：下载订阅 → 重建配置 → 重启内核。当前节点和状态在手机内屏
「设置」页的 ShellCrash 卡片里可以看到。

## 实现

- 页面：`/www/cgi-bin/shellcrash-ui`（纯服务端 HTML，无 JavaScript）；
- 提交后用 `awk` 把地址写进 `/etc/ShellCrash/configs/ShellCrash.cfg` 的 `Url=`；
- 然后调用 `/usr/bin/changxing60-sc-refresh` → `/usr/bin/changxing60-sc-update`，
  它在伪终端里驱动 `menu.sh` 的「管理配置文件 → 更新配置文件」，按菜单**文字**定位，不依赖编号；
- 结果写入 `/tmp/sc-update.status`（`ok HH:MM` / `fail HH:MM`），内屏和网页都读它。

## DNS

本版把 dnsmasq 上游改成 mihomo（`127.0.0.1#1053`），并把 mihomo 的 fake-ip 范围设为
`198.18.0.0/15`：

```sh
nslookup www.google.com 192.168.7.1   # → 198.18.x.x
nslookup www.baidu.com  192.168.7.1   # → 真实 IP
```

这样就消除了对国外域名的 DNS 污染。想恢复成公共 DNS：

```sh
uci delete dhcp.@dnsmasq[0].server
uci add_list dhcp.@dnsmasq[0].server='223.5.5.5'
uci commit dhcp; /etc/init.d/dnsmasq restart
```
