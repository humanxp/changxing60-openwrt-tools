# 内屏界面（7552N / 480×854）

设备内屏没有 fbdev，也没有 Wayland/WebKit，所以界面仍是**直接画 DRM dumb buffer**：一个静态
aarch64 二进制，无 libdrm、无浏览器、无字体引擎。参考了荣悦 E5 `Enceka/infoscreen` 的视觉语言，
但渲染路径完全在用户态自己走。

## 界面内容

- 顶部状态栏：设备名、电池图标与百分比、时间（读 `/etc/TZ`）
- 底部导航：概览 / 信号 / 流量 / 短信 / 设置，**可触摸切换**
- 概览：本次开机上下行、网络与 IPv6、热点二维码、电池、CPU / 内存
- 信号：NR 频段 / PCI / RSRP / SINR / 邻区
- 流量：本次开机与本月流量
- 短信：收件箱正文（`cjk.inc` 的 GB2312 点阵渲染）
- 设置：亮度、切安卓、重启，以及整块 ShellCrash 卡片（ShellCrash/core 版本、运行时长、
  模式/端口/CPU/内存、节点/连接数、上下行总量、规则数、TUN 状态；当前节点正常字号，延时 2×）
  - 「自动优选」：`changxing60-sc-pick` → `cxpick`，测当前链路里最后一级组的全部节点并选最快
  - 「刷新节点」：`changxing60-sc-refresh`，重拉订阅并重启核心，期间显示 `refreshing nodes...`
  - 节点 / 延时：`cxnow` 每 10 秒把当前叶子节点写入 `/tmp/sc-now*`；国旗 emoji 已转成
    `HK` / `JP` 等 ASCII 前缀，面板字体能画
- 按钮按下时先反白、文字下移 1px，抬指才执行（面板没有马达，这是唯一的按压反馈）

触摸在出厂固件里默认关闭；本机加载了 `axs_ts` 驱动后由 `patch.py` 接到 event 设备上。

## 目录

| 文件 | 作用 |
|---|---|
| `base/changxing60-screen.c` | 原始 DRM 绘制程序（`patch.py` 的输入） |
| `ui_portrait.inc` | 新界面主体：卡片、页面、触摸按钮、按压态 |
| `key_policy.inc` | 电源/音量键策略（电源短按亮度、双击点亮，音量翻页） |
| `patch.py` | base + overlay → `build/changxing60-screen.c` |
| `build.sh` | 跑 `patch.py` 并用 zig 交叉编译 |
| `cn_labels.h` | 界面固定中文标签的 16×16 1bpp 点阵（生成物） |
| `cn_font.h` | GB2312 汉字点阵，短信正文用（生成物） |
| `gen_cn.py` / `gen_cnfont.py` | 用 Windows 字体重新生成上面两个头文件 |
| `cxpick.c` | 自动优选：测最后一级组的所有成员并 PUT 最快节点 |
| `changxing60-sc-*.sh` | 面板按钮调用的 ShellCrash 包装脚本 |

`cxnow.c` 在仓库根的 `tools/cxnow.c`，不在此目录重复一份。

## 构建

需要 `python3` 和 [zig](https://ziglang.org/)（自带 aarch64-linux-musl libc，无需 Docker）：

```sh
./build.sh
# 或指定 zig 路径
ZIG=/path/to/zig ./build.sh
```

等价于：

```sh
python3 patch.py                       # base + ui_portrait.inc -> build/changxing60-screen.c
cd build
zig cc -target aarch64-linux-musl -O2 -static -s -I. \
    -o screen changxing60-screen.c qr.c -lm
```

另外两个二进制：

```sh
zig cc -target aarch64-linux-musl -O2 -static -s -o cxpick cxpick.c
zig cc -target aarch64-linux-musl -O2 -static -s -o cxnow ../../tools/cxnow.c   # 仓库布局
```

不重新生成点阵头文件也能构建；`gen_cn.py` / `gen_cnfont.py` 需要微软雅黑 / 宋体（脚本里可用
`CJK_FONT` 指到任意等宽 CJK 字体），仅修改界面文案或增加汉字时才需要跑。

## 部署

```sh
/etc/init.d/changxing60-screen stop
cat build/screen | ssh root@192.168.77.1 'cat > /usr/bin/changxing60-screen && chmod 755 /usr/bin/changxing60-screen'
ssh root@192.168.77.1 '/etc/init.d/changxing60-screen start'
```

面板服务由 procd 托管；替换二进制后重启服务即可，不需要重启设备。原始二进制备份在
`/root/changxing60-screen.orig-*`。

## 不上屏验证

`changxing60-screen` 支持直接写一帧 PPM：

```sh
# 需要先停服务，DRM master 只能有一个
ssh root@192.168.77.1 '/etc/init.d/changxing60-screen stop'
ssh root@192.168.77.1 'CX_PAGE=4 /usr/bin/changxing60-screen --shot /tmp/s.ppm'
ssh root@192.168.77.1 '/etc/init.d/changxing60-screen start; cp /tmp/s.ppm /www/s.ppm'
curl -o s.ppm http://192.168.77.1/s.ppm
```

`CX_PAGE`：0 概览、1 信号、2 流量、3 短信、4 设置、5 客户端。抓完图记得把
`/www/s.ppm` 删掉，并确认 `sprd_backlight/brightness` 已恢复。

## 已知边界

- 按钮事件按 `BTN_TOUCH` / `ABS_MT_TRACKING_ID` 判定；如果某个批次的触摸驱动只报
  `ABS_MT_SLOT`，需要再补一条映射。
- `--shot` 与常驻服务不能同时打开 DRM，抓图必须短暂停服务。
- 页面状态不持久化，服务重启后回到「概览」。
