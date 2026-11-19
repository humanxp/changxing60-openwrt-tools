/* cxnow -- report the node the panel is actually using, plus its last measured delay.
 *
 * mihomo's /proxies lists every group and node. The visible chain starts at the main Selector
 * group and may go through nested groups ("now" of one group points at another group), so walk
 * it down to a real node. The delay comes from that node's "history" (the last probe mihomo ran).
 *
 * Writes /tmp/sc-now (name) and /tmp/sc-now-delay (ms, -1 when unknown) for the panel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int port = 9999;

static int http(const char *path, char *out, int outsz)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(fd); return -1; }
	char req[256];
	int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", path);
	if (write(fd, req, n) != n) { close(fd); return -1; }
	int total = 0;
	for (;;) {
		int r = read(fd, out + total, outsz - 1 - total);
		if (r <= 0) break;
		total += r;
		if (total >= outsz - 1) break;
	}
	out[total] = 0;
	close(fd);
	char *b = strstr(out, "\r\n\r\n");
	if (!b) return -1;
	b += 4;
	int blen = total - (int)(b - out);
	memmove(out, b, blen);
	out[blen] = 0;
	return blen;
}

static char *strval(char *p, const char *key, char *out, int n)
{
	char pat[64];
	snprintf(pat, sizeof pat, "\"%s\":\"", key);
	p = strstr(p, pat);
	if (!p) return NULL;
	p += strlen(pat);
	int o = 0;
	while (*p && *p != '"' && o < n - 1) {
		if (*p == '\\' && p[1]) p++;
		out[o++] = *p++;
	}
	out[o] = 0;
	return p;
}

static int is_group(const char *t)
{
	static const char *g[] = { "Selector", "URLTest", "Fallback", "LoadBalance", "Relay",
	                           "Direct", "Reject", "RejectDrop", "Compatible", "Pass" };
	for (unsigned i = 0; i < sizeof g / sizeof g[0]; i++)
		if (strcmp(t, g[i]) == 0) return 1;
	return 0;
}

/* "<name>":{ ... } -- names are used verbatim; a quote inside a name would defeat this, but the
 * panel only needs a best-effort label and every stock config keeps names plain. */
static char *find_obj(char *buf, const char *name)
{
	char pat[300];
	snprintf(pat, sizeof pat, "\"%s\":{", name);
	return strstr(buf, pat);
}

int main(void)
{
	const char *env = getenv("CLASH_PORT");
	if (env) port = atoi(env);

	char *buf = malloc(1 << 20);
	if (!buf) return 1;
	if (http("/proxies", buf, 1 << 20) < 0) { free(buf); return 2; }

	/* first Selector group: scan for its type, then look back for the nearest "name":"..." */
	char group[128] = "";
	char *p = buf;
	while ((p = strstr(p, "\"type\":\"Selector\""))) {
		char *start = p - 2000;
		if (start < buf) start = buf;
		char *last = NULL, *scan = start;
		while ((scan = strstr(scan, "\"name\":\"")) && scan < p) { last = scan; scan += 8; }
		if (last) { strval(last, "name", group, sizeof group); if (group[0]) break; }
		p += 16;
	}
	if (!group[0]) { free(buf); return 3; }

	char cur[128];
	snprintf(cur, sizeof cur, "%s", group);
	char *obj = NULL;
	for (int depth = 0; depth < 8; depth++) {
		obj = find_obj(buf, cur);
		if (!obj) break;
		char t[32] = "", nxt[128] = "";
		strval(obj, "type", t, sizeof t);
		if (!is_group(t)) break;
		if (!strval(obj, "now", nxt, sizeof nxt) || !nxt[0]) break;
		snprintf(cur, sizeof cur, "%s", nxt);
	}

	int delay = -1;
	if (obj) {
		char *h = strstr(obj, "\"history\":[");
		if (h) {
			char *end = strchr(h, ']');
			char *q = h;
			while ((q = strstr(q, "\"delay\":")) && (!end || q < end)) {
				delay = atoi(q + 8);
				q += 8;
			}
		}
	}

	FILE *f = fopen("/tmp/sc-now", "w");
	if (f) { fprintf(f, "%s\n", cur); fclose(f); }
	f = fopen("/tmp/sc-now-delay", "w");
	if (f) { fprintf(f, "%d\n", delay); fclose(f); }
	printf("now %s (%d ms)\n", cur, delay);
	free(buf);
	return 0;
}
