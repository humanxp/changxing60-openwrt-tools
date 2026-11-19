"""Assemble the new-screen source: original changxing60-screen.c + the card UI + touch handling."""
import os, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
# The public tree keeps the driver source next to this overlay (base/); the working tree here
# still reads it from the kernel tree, so both layouts build from the same patch.py.
BASE = os.path.join(HERE, "base")
if not os.path.isdir(BASE):
    BASE = os.path.join(HERE, "..", "changxing60-linux-mainline-5.4", "tools", "screen")
SRC = os.path.join(BASE, "changxing60-screen.c")
OUT = os.path.join(HERE, "build")
os.makedirs(OUT, exist_ok=True)

src = open(SRC, encoding="utf-8").read()

# 1. includes: Chinese labels + ioctl for the touch device
anchor = '#include "font8x16.h"'
assert anchor in src
src = src.replace(anchor, anchor + '\n#include "cn_labels.h"\n#include "cn_font.h"\n#include <sys/ioctl.h>', 1)

# 2. the new UI block before render()
inc = open(os.path.join(HERE, "ui_portrait.inc"), encoding="utf-8").read()
anchor = "static void render(void)"
assert anchor in src
src = src.replace(anchor, inc + "\n" + anchor, 1)

# 3. portrait pages take the new renderer
old = "\tif (voice_active()) { call_page(sc, portrait); present(); return; }"
assert old in src
src = src.replace(old, old + "\n\n\tif (portrait) { render_portrait(); present(); return; }", 1)

# 4. notice the touch panel when enumerating input devices
old = "\t\tin_fd[in_n] = fd;"
assert old in src
src = src.replace(old, """\t\t{
\t\t\tchar nm[64] = { 0 };
\t\t\tif (ioctl(fd, EVIOCGNAME(sizeof nm - 1), nm) >= 0 && strstr(nm, "axs")) {
\t\t\t\tstruct input_absinfo ai;
\t\t\t\ttouch_fd = fd;
\t\t\t\tif (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0 && ai.maximum > 0)
\t\t\t\t\ttouch_max_x = ai.maximum;
\t\t\t\tif (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0 && ai.maximum > 0)
\t\t\t\t\ttouch_max_y = ai.maximum;
\t\t\t}
\t\t}
\t\tin_fd[in_n] = fd;""", 1)

# 5. touch events: track position, tap on lift
old = "\t\t\twhile ((got = read(in_fd[i], &ev, sizeof ev)) == (ssize_t)sizeof ev) {"
assert old in src
src = src.replace(old, old + """
\t\t\t\tif (ev.type == EV_ABS) {
\t\t\t\t\tif (ev.code == ABS_MT_POSITION_X) { touch_x = ev.value; if (touch_down) touch_press(); }
\t\t\t\t\telse if (ev.code == ABS_MT_POSITION_Y) { touch_y = ev.value; if (touch_down) touch_press(); }
\t\t\t\t\telse if (ev.code == ABS_MT_TRACKING_ID) {
\t\t\t\t\t\tif (ev.value >= 0) { if (!touch_down) { touch_down = 1; touch_press(); } }
\t\t\t\t\t\telse if (touch_down) { touch_down = 0; touch_tap(); }
\t\t\t\t\t}
\t\t\t\t\tcontinue;
\t\t\t\t}
\t\t\t\tif (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
\t\t\t\t\tif (ev.value) { if (!touch_down) { touch_down = 1; touch_press(); } }
\t\t\t\t\telse if (touch_down) { touch_down = 0; touch_tap(); }
\t\t\t\t\tcontinue;
\t\t\t\t}""", 1)

# 5b. read the fuel gauge / battery only every 10 s: the vendor sc27xx driver logs a kern.err
# "test.." line on every capacity read, so the 2 s refresh was spamming the kernel log.
old = "\t\ttz++;\n\t\tread_system();"
if old in src:
    src = src.replace(old, "\t\ttz++;\n\t\tif (tz % 5 == 1)\n\t\t\tread_system();", 1)

# 5c. power/volume policy: touch never wakes, double power press wakes, volume steps brightness
a = src.index("static void handle_key(__u16 code, __s32 value)")
b = src.index("/* ---------- offline frame dump", a)
src = src[:a] + open(os.path.join(HERE, "key_policy.inc"), encoding="utf-8").read() + "\n" + src[b:]

# 6. --bootlog mode: draw /dev/kmsg on the panel from an early init script
old = "int main(int argc, char **argv)\n{"
assert old in src
src = src.replace(old, old + "\n\tif (argc > 1 && strcmp(argv[1], \"--bootlog\") == 0)\n\t\treturn bootlog_run();", 1)

# keep the build directory self-contained so a rebuild never picks up a stale font/QR header
src_dir = BASE
for name in ("cn_labels.h", "cn_font.h", "font8x16.h", "qr.c", "qr.h", "drm_min.h"):
    a = os.path.join(HERE, name) if name in ("cn_labels.h", "cn_font.h") else os.path.join(src_dir, name)
    shutil.copyfile(a, os.path.join(OUT, name))

dst = os.path.join(OUT, "changxing60-screen.c")
open(dst, "w", encoding="utf-8", newline="\n").write(src)
print("wrote", dst, len(src), "bytes")
