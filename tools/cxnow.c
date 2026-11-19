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
#include <strings.h>

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

/* Ranking for the selector to start from: 3 = the config's main selector, 0 = mihomo's built-in
 * GLOBAL (only used when nothing else exists), 1 = a manual-switch selector, 2 = anything else. */
static int group_rank(const char *n)
{
	if (!strcmp(n, "GLOBAL") || !strcmp(n, "COMPATIBLE") || !strcmp(n, "PASS")) return 0;
	if (strstr(n, "节点选择") || !strcasecmp(n, "proxy") || !strcasecmp(n, "select")) return 3;
	if (strstr(n, "手动切换")) return 1;
	return 2;
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

/* Subscription configs put the country in front of the node name as a pair of regional-indicator
 * emoji (U+1F1E6..U+1F1FF), e.g. the Hong Kong flag followed by " 香港-HKBN-2x". The panel has no
 * emoji glyphs (it drops them), which left the label without the country at all. Rewrite a flag
 * pair as its two ASCII letters, giving "HK 香港-HKBN-2x"; the 8x16 font has those, and the rest
 * of the name is copied verbatim. */
static int utf8_decode(const unsigned char *s, unsigned *cp)
{
	unsigned char c = s[0];
	if (c < 0x80) { *cp = c; return 1; }
	if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
		*cp = ((unsigned)(c & 0x1F) << 6) | (s[1] & 0x3F);
		return 2;
	}
	if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
		*cp = ((unsigned)(c & 0x0F) << 12) | ((unsigned)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
		return 3;
	}
	if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
		*cp = ((unsigned)(c & 0x07) << 18) | ((unsigned)(s[1] & 0x3F) << 12) |
		      ((unsigned)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
		return 4;
	}
	*cp = c;
	return 1;
}

/* A-Z for the 26 regional indicator symbols, 0 for anything else. */
static int region_letter(unsigned cp)
{
	return cp >= 0x1F1E6 && cp <= 0x1F1FF ? 'A' + (int)(cp - 0x1F1E6) : 0;
}

static void display_name(const char *in, char *out, int n)
{
	const unsigned char *p = (const unsigned char *)in;
	int o = 0;
	while (*p && o < n - 1) {
		unsigned cp = 0, ncp = 0;
		int len = utf8_decode(p, &cp);
		int nlen = p[len] ? utf8_decode(p + len, &ncp) : 0;
		int a = region_letter(cp), b = region_letter(ncp);
		if (a && b) {
			if (o + 2 > n - 1) break;
			out[o++] = (char)a;
			out[o++] = (char)b;
			p += len + nlen;
			continue;
		}
		if (o + len > n - 1) break;
		memcpy(out + o, p, len);
		o += len;
		p += len;
	}
	out[o] = 0;
}

int main(void)
{
	const char *env = getenv("CLASH_PORT");
	if (env) port = atoi(env);

	char *buf = malloc(1 << 20);
	if (!buf) return 1;
	if (http("/proxies", buf, 1 << 20) < 0) { free(buf); return 2; }

	/* Pick the selector the rules actually point at. mihomo also lists its built-in GLOBAL group
	 * first, and GLOBAL is a selector too -- walking from it lands on whatever GLOBAL happens to
	 * hold ("🛑 广告拦截" -> DIRECT), not on the node carrying traffic. Prefer the config's main
	 * group by name; fall back to the first selector that is not one of the built-ins. */
	char group[128] = "";
	int best = -1;
	char *p = buf;
	while ((p = strstr(p, "\"type\":\"Selector\""))) {
		char *start = p - 2000;
		if (start < buf) start = buf;
		char *last = NULL, *scan = start;
		while ((scan = strstr(scan, "\"name\":\"")) && scan < p) { last = scan; scan += 8; }
		if (last) {
			char g[128] = "";
			strval(last, "name", g, sizeof g);
			int rank = group_rank(g);
			if (g[0] && rank > best) { best = rank; snprintf(group, sizeof group, "%s", g); }
			if (best == 3) break;
		}
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

	char label[160];
	display_name(cur, label, sizeof label);
	FILE *f = fopen("/tmp/sc-now", "w");
	if (f) { fprintf(f, "%s\n", label); fclose(f); }
	f = fopen("/tmp/sc-now-delay", "w");
	if (f) { fprintf(f, "%d\n", delay); fclose(f); }
	printf("now %s (%d ms)\n", label, delay);
	free(buf);
	return 0;
}
