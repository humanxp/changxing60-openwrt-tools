#!/bin/sh
# One-tap latency test from the panel's "测速" button.
#
# Downloads the usual 204 probe *through the local mixed-port proxy*, so the number reflects the
# currently selected node rather than the bare carrier.  Timing comes from /proc/uptime (10 ms
# resolution) because this busybox date does not implement %N.
export PATH="$PATH:/opt/changxing60/busybox-bin"

PORT=7890
touch /tmp/sc-testing
trap 'rm -f /tmp/sc-testing' EXIT INT TERM
rm -f /tmp/sc-delay
start=$(cut -d' ' -f1 /proc/uptime)
if http_proxy="http://127.0.0.1:$PORT" https_proxy="http://127.0.0.1:$PORT" \
	wget -q -O /dev/null -T 8 "http://www.gstatic.com/generate_204" 2>/dev/null; then
	end=$(cut -d' ' -f1 /proc/uptime)
	ms=$(awk -v a="$start" -v b="$end" 'BEGIN { printf "%d", (b - a) * 1000 }')
	echo "$ms" > /tmp/sc-delay
else
	echo fail > /tmp/sc-delay
fi