#!/bin/sh
# Update ShellCrash's database files from the panel's "更新库" button.
#
# ShellCrash has no non-interactive "update database" entry point, but its menu reads stdin, so the
# exact key sequence used by hand (9 更新与支持 -> 3 更新数据库文件 -> 8 更新全部 -> 1 MetaCubeX ->
# 1 BundleMRS -> back out) can be fed to it.  The core is restarted afterwards so the new rules load.
# Progress is shown by the panel through /tmp/sc-updating.
export PATH="$PATH:/opt/changxing60/busybox-bin"

touch /tmp/sc-updating
{
	printf '9\n3\n8\n1\n1\n0\n0\n0\n0\n' | sh /etc/ShellCrash/menu.sh
	/etc/init.d/shellcrash restart
} >/tmp/sc-update.log 2>&1
rm -f /tmp/sc-updating
