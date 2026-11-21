# 网页终端（ttyd）

LuCI 里 **服务 → 终端**，或直接访问 `http://192.168.7.1:7681/`。

## 说明

- 用的是 [ttyd](https://github.com/tsl0922/ttyd) 1.7.7 官方静态版（aarch64，约 1.37 MB）；
- 终端里跑的是 `/bin/login`，**必须输入 root 密码**才能进，不是裸 shell；
- 字体 13px、深色主题，手机浏览器可用；
- 服务由 procd 守护（`/etc/init.d/ttyd`），已设为开机自启；
- 监听 `0.0.0.0:7681`，防火墙 WAN 区默认拒绝入站，外部访问不到；局域网内打开页面仍需密码。

## 常用操作

```sh
/etc/init.d/ttyd restart     # 重启服务
/etc/init.d/ttyd disable     # 关掉并禁止自启
```

## 安全提醒

局域网里任何人都能打开页面（但进不去系统）。如果想彻底关掉：

```sh
/etc/init.d/ttyd stop && /etc/init.d/ttyd disable
```
