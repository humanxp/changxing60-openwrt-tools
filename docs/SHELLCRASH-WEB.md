# ShellCrash 网页管理界面

终端里 `crash` 的菜单，搬到浏览器里：**纯服务端渲染的 HTML，没有 JavaScript**，老手机浏览器也能用。

## 地址

| 网络 | 地址 |
|---|---|
| WiFi 热点 | `http://192.168.7.1/cgi-bin/shellcrash-ui` |
| USB | `http://192.168.77.1/cgi-bin/shellcrash-ui` |
| 短地址（自动跳转） | `http://192.168.7.1/shellcrash.html` |

LuCI 里也有入口：**服务 → ShellCrash**（iframe 打开同一个页面）。

## 怎么工作

1. `/usr/bin/cxmenu`：用 `forkpty` 把 `menu.sh` 跑在一个**伪终端**上，行为和 SSH 里完全一致
   （`/tmp/sc-menu/in` 是按键 FIFO，`/tmp/sc-menu/out` 是输出）；
2. `/usr/bin/changxing60-sc-web`：JSON 后端（状态、启动/停止/重启、更新订阅、自动优选、自启开关）；
3. `/www/cgi-bin/shellcrash-ui`：把菜单输出解析成可点的条目，渲染成 HTML；
4. 每次点条目 = 往 FIFO 写一个按键，菜单状态保持；需要输入时页面显示提问和输入框。

菜单条目按**文字**解析，ShellCrash 升级重排编号不会影响页面。

## 更新订阅

`/usr/bin/changxing60-sc-update` 不写死 `6→7→1`，而是从当前菜单里按文字找：

- 「管理配置文件 / 配置文件管理」
- 「更新配置文件 / 更新订阅」

找不到就退出并报 `label-not-found`，不会误点其他菜单项。更新结束后自动重启服务，
并把结果写进 `/tmp/sc-update.status`（面板据此显示「07:35 成功」）。

## 自检

```sh
/usr/bin/changxing60-sc-web status
/usr/bin/changxing60-sc-web menu | head -20
curl -s 'http://127.0.0.1/cgi-bin/shellcrash-ui' | head -5
```
