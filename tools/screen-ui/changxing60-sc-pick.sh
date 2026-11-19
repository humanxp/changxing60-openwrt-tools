#!/bin/sh
# Pick the fastest node of the selector group (cxpick does the API work) and hand the resulting
# latency to the panel.  No throughput test here: the button is "switch to a faster node", and what
# the user wants to see afterwards is the new node's delay.
export PATH="$PATH:/opt/changxing60/busybox-bin"

touch /tmp/sc-testing
trap 'rm -f /tmp/sc-testing' EXIT INT TERM
rm -f /tmp/sc-delay /tmp/sc-speed /tmp/sc-picked

/usr/bin/cxpick >/tmp/cxpick.log 2>&1
if [ -s /tmp/sc-pick ]; then
	cp /tmp/sc-pick /tmp/sc-delay
else
	echo fail > /tmp/sc-delay
	exit 1
fi

# Refresh the panel's "current node" line now instead of waiting for its 10 s poll.
/usr/bin/cxnow >/dev/null 2>&1
