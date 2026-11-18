# 内屏页签卡顿：page-flip 等待从 500 ms 降到 40 ms

## 现象

在 480×854 内屏上切换"概览 / 信号 / 流量 / 短信 / 设置"页签时，随机出现半秒左右的卡顿。

## 原因

`changxing60-screen` 的帧提交是同步的：

```c
ioctl(drm_fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf);
if (flip_wait(500) == 0) { ...切到后备缓冲... }
else { flip_mode = 0; /* 永久退回 memcpy */ }
```

单线程：绘制、输入、提交都在一个循环里。DPU 偶尔迟到时，这个 `flip_wait(500)`
就把整个界面卡住最多 500 ms；而且只要超时一次，就永久放弃 page flip。

## 改法

```c
if (ioctl(drm_fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) == 0 && flip_wait(40) == 0) {
        flip_miss = 0;
        scan_buf ^= 1;
        fb = fb_buf[scan_buf ^ 1];
        return;
}
/* 40 ms 没完成只是迟到，不是驱动坏了：这一帧用 memcpy，下一帧继续试 flip。
 * 连续 5 次没完成才放弃 page flip。 */
if (++flip_miss >= 5) {
        flip_mode = 0;
        fprintf(stderr, "changxing60-screen: page flip unavailable, switching to buffer copy\n");
}
```

效果：最坏卡顿从 ~500 ms 降到 ~40 ms，且不会因为一次迟到永久降级。

## 源码位置与构建

页签 UI 的源码不在本仓库（`screen-ui/` 由作者的工作区维护），本仓库记录的是补丁本身。
构建方式（zig 交叉编译到 aarch64-musl）：

```sh
python3 screen-ui/gen_cn.py
python3 screen-ui/patch.py
cd screen-ui/build
zig cc -target aarch64-linux-musl -O2 -static -s -I. \
    -o screen changxing60-screen.c qr.c -lm
```

部署（热替换，不重启设备）：

```sh
ssh root@192.168.77.1 'cp /usr/bin/changxing60-screen /root/changxing60-screen.bak'
ssh root@192.168.77.1 'cat > /usr/bin/changxing60-screen' < screen
ssh root@192.168.77.1 '/etc/init.d/changxing60-screen restart'
```

## 仍然存在的问题

内核侧每次启动会打印：

```
BUG: Bad page state in process kworker/u16:5  pfn:b0400
        sprd_free_reserved_area ← sprd_dpu_cleanup_fb ← sprd_plane_cleanup_fb
[drm:sprd_panel_esd_work_func] *ERROR* unknown esd check mode:4
```

驱动释放 bootloader logo 显存时重复释放页面，内核被 taint。这属于显示驱动移植问题，
本次没有处理；它与启动偶发卡在 `switch_root` 之后可能同源。
