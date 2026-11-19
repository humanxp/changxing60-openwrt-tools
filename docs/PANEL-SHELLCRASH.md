# 面板上的 ShellCrash 区块：自动优选与"当前节点"

## 「自动优选」做了什么

按钮 → `system("/usr/bin/changxing60-sc-pick &")` → 脚本 → `cxpick`：

1. `GET /proxies`，找第一个 `type:"Selector"` 的组；
2. `GET /group/<组>/delay?url=http://www.gstatic.com/generate_204&timeout=2000`
   —— mihomo 并行测该组**所有成员**；
3. 取延迟最小且 >0 的成员，`PUT /proxies/<组> {"name": ...}` 切过去；
4. 写 `/tmp/sc-pick`（延迟）与 `/tmp/sc-picked`（被选中的名字）。

它测的是**延迟**，不测带宽/丢包；超时 2 秒；全部超时则不切换。

## 「刷新节点」做了什么

`system("/usr/bin/changxing60-sc-refresh &")`：重新从 API 拉一遍状态（节点数、连接数、上下行），
不改选择。

## 「当前节点 + 延时」怎么来的（v1.0.8）

新增 `tools/cxnow.c` → `/usr/bin/cxnow`：

- `GET /proxies`，找到主 Selector，然后**沿 `now` 递归往下追**（主组 → 子组 → 叶子节点）；
- 在叶子节点的 `history` 里取最近一次 `delay`；
- 写 `/tmp/sc-now`、`/tmp/sc-now-delay`。

面板设置页每 10 秒最多起一次 `cxnow`（后台，不阻塞绘制），然后直接读这两个文件显示：

```
节点  香港-HKBN-2x
延时  78 ms
```

节点名用混合渲染（ASCII 8px + GB2312 中文 16px），中文/英文混排的节点名可以正常显示；
GB2312 之外的字符（emoji 等）会被跳过。

## 已知边界

- 只在**主 Selector 的成员是叶子节点**时，"选最快"才等于"所有节点里最快"。若成员是子组，
  现在选的是"最快的子组"（子组内部仍由它自己的策略决定）；要彻底逐叶子测，需要再改 `cxpick`。
- 依赖 mihomo 控制端口（默认 9999，`CLASH_PORT` 可覆盖）。
- 订阅地址如果在本机网络不可达（例如被 Cloudflare 段屏蔽），"更新订阅"会失败，
  但已有的本地配置和优选不受影响。
