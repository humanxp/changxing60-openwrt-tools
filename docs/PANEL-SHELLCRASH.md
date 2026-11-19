# 面板上的 ShellCrash 区块：自动优选与"当前节点"

## 「自动优选」做了什么

按钮 → `system("/usr/bin/changxing60-sc-pick &")` → 脚本 → `cxpick`：

1. `GET /proxies`，找**主选择组**（优先名字含「节点选择」的 Selector，跳过 mihomo 自带的
   `GLOBAL`），再沿 `now` 往下走到最后一级持有叶子节点的组——通常是
   `🚀 节点选择 → ♻️ 自动选择`，于是测的就是 `♻️ 自动选择` 的全部节点，而不是顶层组；
2. `GET /group/<组>/delay?url=http://www.gstatic.com/generate_204&timeout=2000`
   —— mihomo 并行测该组**所有成员**；
3. 取延迟最小且 >0 的成员，`PUT /proxies/<组> {"name": ...}` 切过去；
4. 写 `/tmp/sc-pick`（延迟）与 `/tmp/sc-picked`（被选中的名字）。

它测的是**延迟**，不测带宽/丢包；超时 2 秒；全部超时则不切换。

按钮在手指按下时先反白（文字变白、下移 1px），抬指才执行动作；面板没有马达，这是唯一的
"按到了"反馈。`自动优选` 运行期间延时行显示 `testing...`，结束后 `cxnow` 最多 10 秒内把节点行刷新。

## 「刷新节点」做了什么

`system("/usr/bin/changxing60-sc-refresh &")`：重新从 API 拉一遍状态（节点数、连接数、上下行），
不改选择。

## 「当前节点 + 延时」怎么来的（v1.0.8）

新增 `tools/cxnow.c` → `/usr/bin/cxnow`：

- `GET /proxies`，先选**规则实际指向的主选择组**：优先名字里带「节点选择」的 Selector（本机是
  `🚀 节点选择`）。mihomo 自带的 `GLOBAL` 也排在 Selector 列表里，从它出发会走到
  `GLOBAL → 🛑 广告拦截 → DIRECT`，显示的并不是承载流量的节点，所以跳过；
- 再**沿 `now` 递归往下追**（主组 → 子组 → 叶子节点），在叶子节点的 `history` 里取最近一次 `delay`；
- 节点名开头的国旗是一对区域指示符（如 🇭🇰），面板没有 emoji 字形；`cxnow` 把它换成两字母前缀
  （`HK 香港-HKBN-2x`），其余文字原样保留；
- 写 `/tmp/sc-now`、`/tmp/sc-now-delay`。

面板设置页每 10 秒最多起一次 `cxnow`（后台，不阻塞绘制），然后直接读这两个文件显示：

```
节点  JP 日本-樱花-Hy2-IPv6
延时  71 ms
```

节点名用混合渲染（ASCII 8px + GB2312 中文 16px），中文/英文混排的节点名可以正常显示；
GB2312 之外的其它 emoji 仍会被跳过。

## 已知边界

- 源码：`tools/screen-ui/cxpick.c`、`tools/screen-ui/changxing60-sc-pick.sh`；按压反馈在
  `tools/screen-ui/ui_portrait.inc`，`cxnow` 在 `tools/cxnow.c`。
- 配置里没有「节点选择」这类主组时，退回第一个非内置 Selector；一个都没有才用 `GLOBAL`。
- 正常走 `节点选择 → 自动选择` 时，`自动优选` 测的是 URLTest 组的全部节点。但若主组当前
  直接指向 `DIRECT` 这类叶子（没有下一级组），只能在主组的成员（子组）里选最快的子组；
  子组内部仍由它自己的策略决定。
- 依赖 mihomo 控制端口（默认 9999，`CLASH_PORT` 可覆盖）。
- 订阅地址如果在本机网络不可达（例如被 Cloudflare 段屏蔽），"更新订阅"会失败，
  但已有的本地配置和优选不受影响。
