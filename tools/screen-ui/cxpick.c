/* cxpick -- pick the fastest node of a mihomo selector group and select it.
 *
 * The panel has no curl and busybox wget cannot PUT, so this speaks HTTP/1.1 over a plain socket
 * to the local controller (default 9999).  Flow:
 *   GET  /proxies                       -> find the first Selector group name
 *   GET  /group/<group>/delay?url=..    -> {node: delay_ms, ...} for every member
 *   PUT  /proxies/<group> {"name":x}    -> select the fastest
 * Result for the panel: /tmp/sc-pick holds the delay (ms), /tmp/sc-picked the node name.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <strings.h>

static int port = 9999;

static int http(const char *method, const char *path, const char *body, char *out, int outsz)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(fd); return -1; }

	char req[1024];
	int n = snprintf(req, sizeof req,
		"%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n", method, path);
	if (body) {
		n += snprintf(req + n, sizeof req - n,
			"Content-Type: application/json\r\nContent-Length: %d\r\n", (int)strlen(body));
	}
	n += snprintf(req + n, sizeof req - n, "\r\n");
	int off = 0;
	if (n > 0) {
		if (write(fd, req, n) != n) { close(fd); return -1; }
		if (body && write(fd, body, strlen(body)) != (int)strlen(body)) { close(fd); return -1; }
	}
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

static void urlenc(const char *s, char *out, int n)
{
	static const char *hex = "0123456789ABCDEF";
	int o = 0;
	for (; *s && o < n - 4; s++) {
		unsigned char c = (unsigned char)*s;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		    c == '-' || c == '_' || c == '.' || c == '~') {
			out[o++] = c;
		} else {
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 15];
		}
	}
	out[o] = 0;
}

static void jsonesc(const char *s, char *out, int n)
{
	int o = 0;
	for (; *s && o < n - 3; s++) {
		if (*s == '"' || *s == '\\') { out[o++] = '\\'; out[o++] = *s; }
		else out[o++] = *s;
	}
	out[o] = 0;
}

static int is_group(const char *t)
{
	static const char *g[] = { "Selector", "URLTest", "Fallback", "LoadBalance", "Relay",
	                           "Direct", "Reject", "RejectDrop", "Compatible", "Pass" };
	for (unsigned i = 0; i < sizeof g / sizeof g[0]; i++)
		if (strcmp(t, g[i]) == 0) return 1;
	return 0;
}

/* Same ranking as cxnow: mihomo lists its built-in GLOBAL selector too, and picking that one
 * changes a group nothing routes through. Prefer the config's main selector ("节点选择"). */
static int group_rank(const char *n)
{
	if (!strcmp(n, "GLOBAL") || !strcmp(n, "COMPATIBLE") || !strcmp(n, "PASS")) return 0;
	if (strstr(n, "节点选择") || !strcasecmp(n, "proxy") || !strcasecmp(n, "select")) return 3;
	if (strstr(n, "手动切换")) return 1;
	return 2;
}

static char *find_obj(char *buf, const char *name)
{
	char pat[300];
	snprintf(pat, sizeof pat, "\"%s\":{", name);
	return strstr(buf, pat);
}

/* read the JSON string value of "key":"..." starting at p; returns pointer after value or NULL */
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

int main(void)
{
	const char *env = getenv("CLASH_PORT");
	if (env) port = atoi(env);

	char *buf = malloc(1 << 20);
	if (!buf) return 1;

	if (http("GET", "/proxies", NULL, buf, 1 << 20) < 0) return 2;

	/* Main Selector: prefer the config's 节点选择 over mihomo's built-in GLOBAL. */
	char group[128] = "";
	int best_rank = -1;
	char *p = buf;
	while ((p = strstr(p, "\"type\":\"Selector\""))) {
		char *q = p;
		int back = 0;
		while (q > buf && back < 2000) { q--; back++; }
		char *last = NULL, *scan = q;
		while ((scan = strstr(scan, "\"name\":\"")) && scan < p) {
			last = scan;
			scan += 8;
		}
		if (last) {
			char g[128] = "";
			strval(last, "name", g, sizeof g);
			int rank = group_rank(g);
			if (g[0] && rank > best_rank) { best_rank = rank; snprintf(group, sizeof group, "%s", g); }
			if (best_rank == 3) break;
		}
		p += 16;
	}
	if (!group[0]) { free(buf); return 3; }

	/* Walk the current selection down to the last group that owns a leaf node. Selecting the
	 * top group would only re-pick a sub-group; the useful target is the URLTest/Selector the
	 * panel actually resolves ("节点选择" -> "♻️ 自动选择" -> 🇯🇵 …), so test that group's
	 * members and pin the fastest one. */
	for (int depth = 0; depth < 8; depth++) {
		char *obj = find_obj(buf, group);
		if (!obj) break;
		char t[32] = "", now[128] = "";
		strval(obj, "type", t, sizeof t);
		if (!is_group(t)) break;
		if (!strval(obj, "now", now, sizeof now) || !now[0]) break;
		char *nobj = find_obj(buf, now);
		if (!nobj) break;
		char nt[32] = "";
		strval(nobj, "type", nt, sizeof nt);
		if (!is_group(nt)) break;      /* now is a leaf: this group is the right target */
		snprintf(group, sizeof group, "%s", now);
	}

	char enc[512];
	urlenc(group, enc, sizeof enc);
	char path[1024];
	snprintf(path, sizeof path,
		"/group/%s/delay?url=%s&timeout=2000",
		enc, "http%3A%2F%2Fwww.gstatic.com%2Fgenerate_204");

	if (http("GET", path, NULL, buf, 1 << 20) < 0) { free(buf); return 4; }

	/* parse {"name":123,"name2":0,...} */
	int best = -1;
	char bestname[160] = "";
	char *s = buf;
	while ((s = strchr(s, '"'))) {
		s++;
		char name[160];
		int o = 0;
		while (*s && *s != '"' && o < 159) {
			if (*s == '\\' && s[1]) s++;
			name[o++] = *s++;
		}
		name[o] = 0;
		if (*s == '"') s++;
		if (*s == ':') {
			s++;
			int d = atoi(s);
			if (d > 0 && (best < 0 || d < best)) { best = d; snprintf(bestname, sizeof bestname, "%s", name); }
		}
	}
	if (best < 0) { free(buf); return 5; }

	char encn[512], body[512];
	urlenc(bestname, encn, sizeof encn);
	jsonesc(bestname, body, sizeof body);
	char payload[640];
	snprintf(payload, sizeof payload, "{\"name\":\"%s\"}", body);
	snprintf(path, sizeof path, "/proxies/%s", enc);
	if (http("PUT", path, payload, buf, 1 << 20) < 0) { free(buf); return 6; }

	FILE *f = fopen("/tmp/sc-pick", "w");
	if (f) { fprintf(f, "%d\n", best); fclose(f); }
	f = fopen("/tmp/sc-picked", "w");
	if (f) { fprintf(f, "%s\n", bestname); fclose(f); }

	printf("picked %s (%d ms)\n", bestname, best);
	free(buf);
	return 0;
}
