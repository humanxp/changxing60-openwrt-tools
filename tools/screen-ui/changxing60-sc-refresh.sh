#!/bin/sh
# Refresh ShellCrash's subscription/nodes from the panel's bottom "刷新节点" button.
#
# ShellCrash's own menu path is used instead of re-implementing the download: main menu 6
# (管理配置文件) -> 7 (更新配置文件) -> 1 (确认).  It downloads the subscription, regenerates the
# config and restarts the core, which is exactly "刷新节点".  The menu reads stdin, so the keys can
# be piped in; a timeout keeps a wedged download from leaving the button stuck forever.
export PATH="$PATH:/opt/changxing60/busybox-bin"

touch /tmp/sc-refreshing
{
	printf '6\n7\n1\n0\n0\n' | timeout 300 sh /etc/ShellCrash/menu.sh
	/etc/init.d/shellcrash restart
} >/tmp/sc-refresh.log 2>&1
rm -f /tmp/sc-refreshing
