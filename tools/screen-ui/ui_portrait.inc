#include <sys/wait.h>
/* ---- changxing60 portrait UI: cards, big numbers, bottom nav, touch pages --------------------- */

#include <math.h>
#include <dirent.h>

static void render(void);

#define NAV_H 84

static int cur_page = 0;
static int render_skip_refresh = 0;   /* nav tap: draw cached state, refresh on the next tick */              /* 0 概览 1 信号 2 流量 3 短信 4 设置 */
static int cpu_pct = 0, load_x100 = 0, run_n = 0, blk_n = 0;
static int batt_eta_min = -1, batt_charging = 0;
static long long batt_off_ua = 0;   /* typical discharge current with the panel dark */
static unsigned long long cpu_prev_idle = 0, cpu_prev_total = 0;
static int touch_fd = -1;
static int touch_x = -1, touch_y = -1;
static int touch_down = 0;
static int touch_max_x = 479, touch_max_y = 853;

/* ---- cellular snapshot (from changxing60net status) ---- */
static double sig_rsrp = 0, sig_rsrq = 0, sig_sinr = 0;
static int sig_band = 0, sig_pci = 0, sig_act = 0, sig_ok = 0;
static int sig_arfcn = 0, sig_bw = 0, sig_mcs_ul = -1, sig_mcs_dl = -1, sig_neigh = 0;
static double sig_bler_ul = -1, sig_bler_dl = -1;
static int nb_pci[4]; static double nb_rsrp[4]; static int nb_n = 0;
static char sig_tac[16] = "-", sig_gnb[16] = "-", sig_cid[16] = "-";
static char sig_plmn[16] = "-";
static char wan6_show[48];

/* ---- client snapshot (from hostapd get_clients) ---- */
static char cli_mac[8][18];
static int cli_n = 0;


/* ---- rate ---- */
static unsigned long long rate_last_rx = 0, rate_last_tx = 0;
static long long rate_last_t = 0;
static double rate_rx_bps = 0, rate_tx_bps = 0;

/* ---- touch buttons on the current page ---- */
enum { A_NONE = 0, A_BRIGHT, A_REBOOT, A_ANDROID, A_SCUPD, A_SCRF, A_SCTEST, A_ANDROID_OK, A_CANCEL };
static int confirm_android = 0;
static int ov_yes[4], ov_no[4];
static void confirm_overlay(int P, int CW);
struct btn { int x, y, w, h; const char *cn; int action; };
static struct btn btns[6];
static int nbtn = 0;
/* The button under the finger. The panel has no haptics, so drawing it lit while the finger is
 * down is the only "yes, I felt that" cue; it is cleared and the action fires on lift. */
static int press_action = A_NONE;

static int clamp_i(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void fill_rrect(int x, int y, int w, int h, int r, u32 c)
{
	if (r > h / 2) r = h / 2;
	if (r > w / 2) r = w / 2;
	for (int j = 0; j < h; j++) {
		int dy = 0;
		if (j < r) dy = r - j;
		else if (j >= h - r) dy = j - (h - r) + 1;
		int dx = 0;
		if (dy > 0) {
			double v = (double)(r * r - dy * dy);
			dx = r - (int)sqrt(v > 0 ? v : 0);
		}
		fill_rect(x + dx, y + j, w - 2 * dx, 1, c);
	}
}

static int draw_cn(int x, int y, int scale, const char *txt, u32 col)
{
	const struct cn_glyph *g = cn_find(txt);
	if (!g) return draw_text(x, y, scale, col, txt);
	int stride = (g->w + 7) / 8;
	for (int j = 0; j < g->h; j++) {
		int run = -1;
		for (int i = 0; i <= g->w; i++) {
			int on = (i < g->w) && (g->bits[j * stride + (i >> 3)] & (0x80 >> (i & 7)));
			if (on && run < 0) run = i;
			else if (!on && run >= 0) {
				fill_rect(x + run * scale, y + j * scale, (i - run) * scale, scale, col);
				run = -1;
			}
		}
	}
	return g->w * scale;
}

static int draw_cn_big(int x, int y, const char *txt, u32 col)
{
	const struct cn_glyph *g = cn_big_find(txt);
	if (!g) return 0;
	int stride = (g->w + 7) / 8;
	for (int j = 0; j < g->h; j++) {
		int run = -1;
		for (int i = 0; i <= g->w; i++) {
			int on = (i < g->w) && (g->bits[j * stride + (i >> 3)] & (0x80 >> (i & 7)));
			if (on && run < 0) run = i;
			else if (!on && run >= 0) {
				fill_rect(x + run, y + j, i - run, 1, col);
				run = -1;
			}
		}
	}
	return g->w;
}

static void bar(int x, int y, int w, int h, int pct, u32 col)
{
	fill_rrect(x, y, w, h, h / 2, 0x003A3A3C);
	pct = clamp_i(pct, 0, 100);
	if (pct > 0) fill_rrect(x, y, w * pct / 100, h, h / 2, col);
}

static void card(int x, int y, int w, int h)
{
	fill_rrect(x, y, w, h, 22, 0x002C2C2E);
	fill_rrect(x + 1, y + 1, w - 2, h - 2, 21, 0x001C1C1E);
}

/* ---- tiny JSON probes: the device has no JSON library here, and the keys we need are flat ---- */
static const char *json_scope(const char *buf, const char *key, const char *end_key)
{
	const char *p = strstr(buf, key);
	if (!p) return NULL;
	const char *e = end_key ? strstr(p, end_key) : NULL;
	if (e && e == p) return NULL;
	return p;
}

static int json_num_in(const char *start, const char *stop, const char *key, double *out)
{
	char pat[48];
	snprintf(pat, sizeof pat, "\"%s\":", key);
	const char *p = strstr(start, pat);
	if (!p || (stop && p > stop)) return 0;
	p += strlen(pat);
	while (*p == ' ' || *p == '\t') p++;
	if (*p == 'n' || *p == 'N') return 0;      /* null */
	*out = atof(p);
	return 1;
}

static int json_str_in(const char *start, const char *stop, const char *key, char *out, size_t n)
{
	char pat[48];
	snprintf(pat, sizeof pat, "\"%s\":", key);
	const char *p = strstr(start, pat);
	if (!p || (stop && p > stop)) return 0;
	p += strlen(pat);
	while (*p == ' ' || *p == '\t') p++;
	if (*p != '"') return 0;
	p++;
	const char *e = strchr(p, '"');
	if (!e) return 0;
	size_t len = (size_t)(e - p);
	if (len >= n) len = n - 1;
	memcpy(out, p, len);
	out[len] = 0;
	return 1;
}

static void wan6_refresh(void)
{
	static time_t last = 0;
	time_t now = time(NULL);
	if (wan6_show[0] && now - last < 30) return;
	last = now;
	char buf[4096];
	wan6_show[0] = 0;
	if (run_cmd_all("cat /proc/net/if_inet6", buf, sizeof buf) != 1) return;
	char *save = NULL, *line = strtok_r(buf, "\n", &save);
	while (line) {
		char hex[40], dev[24];
		unsigned idx, plen, scope, flags;
		if (sscanf(line, "%39s %x %x %x %x %23s", hex, &idx, &plen, &scope, &flags, dev) == 6 &&
		    strcmp(dev, "sipa_eth0") == 0 && scope == 0) {
			int k = 0;
			for (int i = 0; i < 8; i++) {
				if (i) wan6_show[k++] = ':';
				memcpy(wan6_show + k, hex + i * 4, 4);
				k += 4;
			}
			wan6_show[k] = 0;
			return;
		}
		line = strtok_r(NULL, "\n", &save);
	}
}

static int clash_up = 0, clash_proxies = 0, clash_port = 0, clash_conns = 0;
static int clash_mem_kb = 0, clash_uptime = 0, clash_cpu = 0, clash_rules = 0, clash_tun = 0;
static double clash_down = 0, clash_up_b = 0;
static char clash_mode[24] = "-", clash_pid[16] = "", clash_ver[24] = "-", clash_now[48] = "-", sc_delay[16] = "", sc_speed[16] = "";
static char clash_scver[24] = "-";
static char sc_now[64] = "", sc_now_delay[12] = "";   /* node the panel is really using, and its last delay */
static double rate_hist[60], rate_hist_tx[60];

static int clash_get(int port, const char *path, char *buf, size_t n)
{
	char cmd[160];
	snprintf(cmd, sizeof cmd, "wget -qO- -T 1 http://127.0.0.1:%d%s", port, path);
	return run_cmd_all(cmd, buf, n) == 1;
}

static int clash_is_group(const char *t)
{
	return strcmp(t, "Selector") == 0 || strcmp(t, "URLTest") == 0 ||
	       strcmp(t, "Fallback") == 0 || strcmp(t, "LoadBalance") == 0 ||
	       strcmp(t, "Relay") == 0 || strcmp(t, "Direct") == 0 ||
	       strcmp(t, "Reject") == 0 || strcmp(t, "RejectDrop") == 0 ||
	       strcmp(t, "Compatible") == 0 || strcmp(t, "Pass") == 0;
}

/* utime+stime of the core, in clock ticks; -1 when /proc/<pid>/stat is unreadable. */
static long clash_cpu_ticks(void)
{
	char path[64], buf[1024];
	if (!clash_pid[0]) return -1;
	snprintf(path, sizeof path, "/proc/%s/stat", clash_pid);
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	size_t n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = 0;
	char *p = strrchr(buf, ')');          /* comm may contain spaces and parens */
	if (!p) return -1;
	p += 2;                               /* skip ") " */
	int field = 3;
	long ut = 0, st = 0;
	char *tok = strtok(p, " ");
	while (tok) {
		if (field == 14) ut = atol(tok);
		else if (field == 15) { st = atol(tok); break; }
		field++;
		tok = strtok(NULL, " ");
	}
	return ut + st;
}

static void clash_refresh(int force)
{
	static time_t last = 0;
	time_t now = time(NULL);
	if (!force && now - last < 5) return;
	last = now;

	char buf[24576];
	sc_delay[0] = 0;
	sc_speed[0] = 0;
	read_file_str("/tmp/sc-delay", sc_delay, sizeof sc_delay);
	read_file_str("/tmp/sc-speed", sc_speed, sizeof sc_speed);
	clash_up = run_cmd("pidof mihomo", buf, sizeof buf) == 1;
	if (!clash_up)
		clash_up = run_cmd("pidof CrashCore", buf, sizeof buf) == 1;
	clash_pid[0] = 0;
	if (clash_up) {
		char *nl = strchr(buf, '\n');
		if (nl) *nl = 0;
		snprintf(clash_pid, sizeof clash_pid, "%s", buf);
	}
	if (!clash_up) {
		strcpy(clash_mode, "-"); strcpy(clash_ver, "-"); strcpy(clash_now, "-");
		clash_proxies = clash_conns = 0; clash_down = clash_up_b = 0; clash_port = 0;
		clash_mem_kb = clash_uptime = 0;
		clash_cpu = clash_rules = clash_tun = 0;
		strcpy(clash_scver, "-");
		return;
	}

	/* Panel-side detail: the core's RSS and how long ShellCrash has been up. The start time is
	 * written by its init script; /proc/<pid>/status gives the resident size. */
	clash_mem_kb = 0;
	if (clash_pid[0]) {
		char path[64], line[160];
		snprintf(path, sizeof path, "/proc/%s/status", clash_pid);
		FILE *f = fopen(path, "r");
		if (f) {
			while (fgets(line, sizeof line, f))
				if (!strncmp(line, "VmRSS:", 6)) { clash_mem_kb = atoi(line + 6); break; }
			fclose(f);
		}
	}
	clash_uptime = 0;
	{
		char st[32];
		if (read_file_str("/tmp/ShellCrash/crash_start_time", st, sizeof st) == 0) {
			long long t0 = atoll(st);
			if (t0 > 0 && now > t0 && now - t0 < 30 * 86400) clash_uptime = (int)(now - t0);
		}
	}

	/* CPU% of the core, sampled between refreshes (~5 s). */
	{
		static long long prev = -1;
		static time_t prev_t = 0;
		long long ticks = clash_cpu_ticks();
		clash_cpu = 0;
		if (ticks >= 0 && prev >= 0 && now > prev_t) {
			long hz = sysconf(_SC_CLK_TCK);
			if (hz <= 0) hz = 100;
			double pct = (double)(ticks - prev) * 100.0 / ((double)hz * (double)(now - prev_t));
			clash_cpu = (int)(pct + 0.5);
			if (clash_cpu > 999) clash_cpu = 999;
		}
		prev = ticks;
		prev_t = now;
	}

	read_file_str("/etc/ShellCrash/version", clash_scver, sizeof clash_scver);
	if (!clash_scver[0]) strcpy(clash_scver, "-");
	else { char *r = strstr(clash_scver, "release"); if (r) *r = 0; }

	if (!clash_port) {
		static const int ports[] = { 9999, 9090, 9091, 6170 };
		for (int i = 0; i < 4; i++) {
			if (clash_get(ports[i], "/version", buf, sizeof buf)) {
				clash_port = ports[i];
				break;
			}
		}
	}
	if (!clash_port) return;

	if (clash_get(clash_port, "/version", buf, sizeof buf))
		json_str_in(buf, NULL, "version", clash_ver, sizeof clash_ver);

	clash_tun = 0;
	if (clash_get(clash_port, "/configs", buf, sizeof buf)) {
		json_str_in(buf, NULL, "mode", clash_mode, sizeof clash_mode);
		clash_tun = strstr(buf, "\"tun\":{\"enable\":true") != NULL;
	}

	clash_proxies = 0;
	clash_now[0] = '-'; clash_now[1] = 0;
	if (clash_get(clash_port, "/proxies", buf, sizeof buf)) {
		const char *q = buf;
		while ((q = strstr(q, "\"type\":\""))) {
			q += 8;
			char t[24];
			int k = 0;
			while (*q && *q != '"' && k < 23) t[k++] = *q++;
			t[k] = 0;
			if (!clash_is_group(t)) clash_proxies++;
		}
		const char *p = strstr(buf, "\"PROXY\"");
		if (p) {
			const char *nm = strstr(p, "\"now\":\"");
			if (nm) {
				nm += 7;
				const char *e = strchr(nm, '"');
				if (e && (int)(e - nm) < (int)sizeof clash_now) {
					int n2 = (int)(e - nm);
					memcpy(clash_now, nm, n2);
					clash_now[n2] = 0;
				}
			}
		}
	}

	clash_rules = 0;
	if (clash_get(clash_port, "/providers/rules", buf, sizeof buf)) {
		const char *q = buf;
		while ((q = strstr(q, "\"ruleCount\":"))) { clash_rules += atoi(q + 12); q += 12; }
	}

	clash_conns = 0;
	clash_down = clash_up_b = 0;
	if (clash_get(clash_port, "/connections", buf, sizeof buf)) {
		double v;
		if (json_num_in(buf, NULL, "downloadTotal", &v)) clash_down = v;
		if (json_num_in(buf, NULL, "uploadTotal", &v)) clash_up_b = v;
		const char *q = buf;
		while ((q = strstr(q, "\"id\":\""))) { clash_conns++; q += 6; }
	}
}

/* ---- per-client traffic, accumulated across conntrack refreshes (up/down separately) ---- */
struct cli_use { char ip[16]; char name[24]; unsigned long long cur_u, cur_d, prev_u, prev_d, tot_u, tot_d; };
static struct cli_use cli_use[24];
static int cli_use_n = 0;

static int is_lan_ip(const char *ip)
{
	if (strcmp(ip, "192.168.7.1") == 0 || strcmp(ip, "192.168.77.1") == 0) return 0;
	return strncmp(ip, "192.168.7.", 10) == 0 || strncmp(ip, "192.168.77.", 11) == 0;
}

static void resolve_name(struct cli_use *u)
{
	FILE *f = fopen("/tmp/dhcp.leases", "r");
	u->name[0] = 0;
	if (!f) return;
	char line[256];
	while (fgets(line, sizeof line, f)) {
		char lip[24], name[24];
		if (sscanf(line, "%*s %*s %23s %23s", lip, name) == 2 && strcmp(lip, u->ip) == 0) {
			int ascii = 1;
			for (const char *q = name; *q; q++) if ((unsigned char)*q >= 0x80) ascii = 0;
			snprintf(u->name, sizeof u->name, "%s", ascii ? name : "-");
			break;
		}
	}
	fclose(f);
}

static void usage_refresh(void)
{
	static time_t last = 0;
	time_t now = time(NULL);
	if (now - last < 5) return;
	last = now;

	for (int i = 0; i < cli_use_n; i++) { cli_use[i].cur_u = 0; cli_use[i].cur_d = 0; }

	FILE *f = fopen("/proc/net/nf_conntrack", "r");
	if (f) {
		char line[1024];
		while (fgets(line, sizeof line, f)) {
			const char *sp = strstr(line, "src=");
			if (!sp) continue;
			char ip[16] = "";
			int k = 0;
			sp += 4;
			while (*sp && *sp != ' ' && k < 15) ip[k++] = *sp++;
			ip[k] = 0;
			if (!is_lan_ip(ip)) continue;

			unsigned long long b[2] = { 0, 0 };
			int nb = 0;
			const char *p = line;
			while ((p = strstr(p, "bytes=")) && nb < 2) { b[nb++] = strtoull(p + 6, NULL, 10); p += 6; }

			int i;
			for (i = 0; i < cli_use_n; i++) if (strcmp(cli_use[i].ip, ip) == 0) break;
			if (i == cli_use_n) {
				if (cli_use_n >= 24) continue;
				memset(&cli_use[cli_use_n], 0, sizeof cli_use[0]);
				snprintf(cli_use[cli_use_n].ip, sizeof cli_use[0].ip, "%s", ip);
				resolve_name(&cli_use[cli_use_n]);
				cli_use_n++;
			}
			cli_use[i].cur_u += b[0];
			cli_use[i].cur_d += b[1];
		}
		fclose(f);
	}

	for (int i = 0; i < cli_use_n; i++) {
		if (cli_use[i].cur_u > cli_use[i].prev_u) cli_use[i].tot_u += cli_use[i].cur_u - cli_use[i].prev_u;
		if (cli_use[i].cur_d > cli_use[i].prev_d) cli_use[i].tot_d += cli_use[i].cur_d - cli_use[i].prev_d;
		cli_use[i].prev_u = cli_use[i].cur_u;
		cli_use[i].prev_d = cli_use[i].cur_d;
	}

	for (int i = 0; i < cli_use_n; i++)
		for (int j = i + 1; j < cli_use_n; j++)
			if (cli_use[j].tot_u + cli_use[j].tot_d > cli_use[i].tot_u + cli_use[i].tot_d) {
				struct cli_use t = cli_use[i];
				cli_use[i] = cli_use[j];
				cli_use[j] = t;
			}
}

/* The cellular snapshot comes from "ubus call changxing60net status", which takes ~1.5 s on this
 * box (it queries the modem). Run it in a child and parse the answer on a later tick: blocking the
 * UI loop for that long is exactly what made page switches stall. */
static pid_t sig_pid = -1;
static int sig_fd = -1;
static char sig_abuf[8192];
static size_t sig_alen = 0;

static void sig_parse(char *buf)
{
	sig_ok = 0;
	const char *srv = strstr(buf, "\"serving\"");
	const char *sig = strstr(buf, "\"sig\":");
	if (!srv) return;
	double v;
	if (json_num_in(srv, sig, "rsrp", &v)) { sig_rsrp = v; sig_ok = 1; }
	if (json_num_in(srv, sig, "rsrq", &v)) sig_rsrq = v;
	if (json_num_in(srv, sig, "sinr", &v)) sig_sinr = v;
	if (json_num_in(srv, sig, "band", &v)) sig_band = (int)v;
	if (json_num_in(srv, sig, "pci", &v)) sig_pci = (int)v;
	if (json_num_in(srv, sig, "act", &v)) sig_act = (int)v;
	const char *ops = strstr(buf, "\"ops\"");
	if (ops) {
		json_str_in(ops, sig, "plmn", sig_plmn, sizeof sig_plmn);
		json_num_in(ops, sig, "act", &v);
		sig_act = (int)v;
	}
	if (json_num_in(srv, sig, "arfcn", &v)) sig_arfcn = (int)v;
	if (json_num_in(srv, sig, "bw_mhz", &v)) sig_bw = (int)v;
	if (json_num_in(srv, sig, "ul_mcs", &v)) sig_mcs_ul = (int)v;
	if (json_num_in(srv, sig, "dl_mcs", &v)) sig_mcs_dl = (int)v;
	json_str_in(srv, sig, "gnb", sig_gnb, sizeof sig_gnb);
	json_str_in(srv, sig, "cid", sig_cid, sizeof sig_cid);
	const char *reg = strstr(buf, "\"reg\"");
	if (reg) json_str_in(reg, sig, "tac", sig_tac, sizeof sig_tac);
	if (json_num_in(srv, sig, "ul_bler", &v)) sig_bler_ul = v;
	if (json_num_in(srv, sig, "dl_bler", &v)) sig_bler_dl = v;
	const char *nb = strstr(buf, "\"neighbours\"");
	sig_neigh = 0;
	nb_n = 0;
	if (nb) {
		const char *q = nb;
		while ((q = strstr(q, "\"pci\":"))) {
			sig_neigh++;
			if (nb_n < 4) {
				int pci = atoi(q + 6);
				const char *r = strstr(q, "\"rsrp\":");
				if (r && (r - q) < 240) {
					nb_pci[nb_n] = pci;
					nb_rsrp[nb_n] = atof(r + 7);
					nb_n++;
				}
			}
			q += 6;
		}
	}
}

static void sig_fetch_start(void)
{
	int fds[2];
	if (pipe(fds) != 0) return;
	pid_t pid = fork();
	if (pid == 0) {
		dup2(fds[1], 1);
		close(fds[0]); close(fds[1]);
		execl("/bin/sh", "sh", "-c", "ubus call changxing60net status", (char *)NULL);
		_exit(127);
	}
	close(fds[1]);
	if (pid < 0) { close(fds[0]); return; }
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	sig_pid = pid; sig_fd = fds[0]; sig_alen = 0;
}

static int sig_fetch_poll(void)
{
	if (sig_pid <= 0) return 0;
	for (;;) {
		ssize_t r = read(sig_fd, sig_abuf + sig_alen, sizeof sig_abuf - sig_alen - 1);
		if (r > 0) { sig_alen += r; continue; }
		if (r == 0) break;                       /* EOF: child closed the pipe */
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			int st;
			if (waitpid(sig_pid, &st, WNOHANG) == sig_pid) continue;  /* reaped: drain what is left */
			return 0;                            /* still running */
		}
		break;
	}
	close(sig_fd); sig_fd = -1;
	waitpid(sig_pid, NULL, WNOHANG);
	sig_pid = -1;
	sig_abuf[sig_alen] = 0;
	return sig_alen > 0;
}

static void sig_refresh(int force)
{
	static time_t last = 0;
	time_t now = time(NULL);
	if (sig_pid > 0) {
		if (sig_fetch_poll()) { sig_parse(sig_abuf); last = now; }
		return;
	}
	if (!force && now - last < 10) return;
	last = now;
	sig_fetch_start();
}

static void clients_refresh(int force)
{
	static time_t last = 0;
	time_t now = time(NULL);
	if (!force && now - last < 10) return;
	last = now;

	char buf[8192];
	if (run_cmd_all("ubus call hostapd.wlan0 get_clients", buf, sizeof buf) != 1) return;
	cli_n = 0;
	const char *p = buf;
	while (*p && cli_n < 8) {
		unsigned a, b, c, d, e, f;
		if (sscanf(p, "\"%2x:%2x:%2x:%2x:%2x:%2x\"", &a, &b, &c, &d, &e, &f) == 6) {
			snprintf(cli_mac[cli_n], sizeof cli_mac[cli_n], "%02x:%02x:%02x:%02x:%02x:%02x",
				 a, b, c, d, e, f);
			cli_n++;
			p += 18;
		} else {
			p++;
		}
	}
}

/* ---------- sms pool + latest message ---------- */
static int sms_total = 0, sms_unread = 0;
static char sms_id[16] = "", sms_from[32] = "", sms_time[32] = "", sms_code[16] = "";
static char sms_body[1024] = "";

/* The panel font is ASCII-only and a Chinese SMS would render as noise.  Keep printable ASCII,
 * turn everything else into a space, and let the digits of a verification code stand out. */
static void sms_sanitize(const char *in, char *out, size_t n)
{
	size_t o = 0;
	for (; *in && o + 1 < n; in++) {
		unsigned char c = (unsigned char)*in;
		if (c == '\r') continue;
		if (c == '\n' || c == '\t') out[o++] = ' ';
		else if (c < 0x20 || c == 0x7f) continue;
		else out[o++] = (char)c;   /* UTF-8 passes through; CJK glyphs are drawn by cjk.inc */
	}
	out[o] = 0;
}

static void sms_pick_code(const char *s, char *out, size_t n)
{
	size_t best = 0, bestpos = 0, run = 0, pos = 0;
	for (size_t i = 0; s[i]; i++) {
		if (s[i] >= '0' && s[i] <= '9') {
			if (run == 0) pos = i;
			run++;
			if (run > best && run >= 4 && run <= 8) { best = run; bestpos = pos; }
		} else {
			run = 0;
		}
	}
	if (best >= 4) snprintf(out, n, "%.*s", (int)best, s + bestpos);
	else out[0] = 0;
}

static void sms_refresh(int force)
{
	static time_t last = 0;
	time_t now = time(NULL);
	int on_page = (cur_page == 3);
	/* Off the SMS page only the unread badge is needed: one list every 30 s is enough, and
	 * skipping show saves a process spawn. On the page keep 10 s so a code appears quickly. */
	if (!force && now - last < (on_page ? 10 : 30)) return;
	last = now;

	char buf[4096];
	sms_total = sms_unread = 0;
	sms_id[0] = sms_from[0] = sms_time[0] = sms_code[0] = sms_body[0] = 0;

	if (run_cmd_all("/opt/changxing60/bin/changxing60-sms list 2>/dev/null", buf, sizeof buf) != 1)
		return;
	int a = 0, b = 0;
	if (sscanf(buf, "pool: %d message(s), %d unread", &a, &b) == 2) {
		sms_total = a; sms_unread = b;
	}

	/* list prints newest first: the first <id> token of the first message line is the latest one */
	char *line = strchr(buf, '\n');
	if (!line) return;
	line++;
	if (sscanf(line, "%15s", sms_id) != 1 || sms_id[0] < '0' || sms_id[0] > '9') {
		sms_id[0] = 0;
		return;
	}

	if (!on_page)
		return;                 /* counts are enough for the badge */
	if (!on_page)
		return;                 /* counts are enough for the badge */
	char cmd[128];
	snprintf(cmd, sizeof cmd, "/opt/changxing60/bin/changxing60-sms show %s 2>/dev/null", sms_id);
	if (run_cmd_all(cmd, buf, sizeof buf) != 1) return;

	char *p = strstr(buf, "from:");
	if (p) sscanf(p + 5, " %31s", sms_from);
	p = strstr(buf, "time:");
	if (p) {
		/* copy only the value: writing a NUL into buf here would cut off the body below */
		char *s = p + 5;
		while (*s == ' ' || *s == '\t') s++;
		char *e = s;
		while (*e && *e != '\n') e++;
		size_t n = (size_t)(e - s);
		if (n >= sizeof sms_time) n = sizeof sms_time - 1;
		memcpy(sms_time, s, n);
		sms_time[n] = 0;
	}

	/* the body starts after the "---" separator of `show` */
	p = strstr(buf, "\n---\n");
	if (p) {
		p += 5;
	} else if ((p = strstr(buf, "\n--\n")) != NULL) {
		p += 4;
	} else {
		p = strstr(buf, "\n\n");
		if (!p) return;
		p += 2;
	}
	sms_sanitize(p, sms_body, sizeof sms_body);
	char *e = sms_body + strlen(sms_body);
	while (e > sms_body && (e[-1] == ' ' || e[-1] == '\n')) *--e = 0;
	sms_pick_code(sms_body, sms_code, sizeof sms_code);
}

static void rate_refresh(void)
{
	long long now = (long long)time(NULL);
	if (rate_last_t == 0) {
		rate_last_rx = wan_rx; rate_last_tx = wan_tx; rate_last_t = now;
		return;
	}
	long long dt = now - rate_last_t;
	if (dt >= 1) {
		rate_rx_bps = (double)(wan_rx - rate_last_rx) * 8.0 / (double)dt;
		rate_tx_bps = (double)(wan_tx - rate_last_tx) * 8.0 / (double)dt;
		rate_last_rx = wan_rx; rate_last_tx = wan_tx; rate_last_t = now;
		for (int i = 0; i < 59; i++) {
			rate_hist[i] = rate_hist[i + 1];
			rate_hist_tx[i] = rate_hist_tx[i + 1];
		}
		rate_hist[59] = rate_rx_bps;
		rate_hist_tx[59] = rate_tx_bps;
	}
}
static void human_bps(double bps, char *out, size_t n)
{
	const char *u = "bps";
	double v = bps;
	if (v >= 1e9) { v /= 1e9; u = "Gbps"; }
	else if (v >= 1e6) { v /= 1e6; u = "Mbps"; }
	else if (v >= 1e3) { v /= 1e3; u = "Kbps"; }
	snprintf(out, n, "%.1f %s", v, u);
}

/* ---- buttons ---- */
static void add_btn(int x, int y, int w, int h, const char *cn, int action)
{
	if (nbtn >= 6) return;
	btns[nbtn].x = x; btns[nbtn].y = y; btns[nbtn].w = w; btns[nbtn].h = h;
	btns[nbtn].cn = cn; btns[nbtn].action = action;
	nbtn++;
}

static void draw_btn(struct btn *b, u32 fill, u32 fg)
{
	int down = b->action == press_action;
	if (down) { fill = 0x00505058; fg = 0x00FFFFFF; }
	fill_rrect(b->x, b->y, b->w, b->h, 12, fill);
	const struct cn_glyph *g = cn_find(b->cn);
	int w = g ? g->w : 0;
	/* one pixel lower while pressed: reads as the cap sinking into the card */
	draw_cn(b->x + (b->w - w) / 2, b->y + (b->h - 16) / 2 + (down ? 1 : 0), 1, b->cn, fg);
}

static int hit_btn(int x, int y)
{
	for (int i = 0; i < nbtn; i++)
		if (x >= btns[i].x && x < btns[i].x + btns[i].w &&
		    y >= btns[i].y && y < btns[i].y + btns[i].h)
			return i;
	return -1;
}

static void do_action(int action)
{
	if (action == A_BRIGHT) {
		/* Debounce: the panel's backlight node is written through the vendor PMIC path, and a burst
		 * of writes from fast taps is what preceded the reboots. One step per 2 s is plenty. */
		static time_t last_btn = 0;
		time_t now = time(NULL);
		if (now - last_btn < 2) return;
		last_btn = now;
		bl_index = (bl_index + 1) % 4;      /* 100/75/50/25; never 0 from the button */
		backlight_apply();
		render();
	} else if (action == A_SCTEST) {
		system("/usr/bin/changxing60-sc-pick >/dev/null 2>&1 &");
		render();
	} else if (action == A_SCRF) {
		system("/usr/bin/changxing60-sc-refresh >/dev/null 2>&1 &");
		render();
	} else if (action == A_SCUPD) {
		system("/usr/bin/changxing60-sc-update >/dev/null 2>&1 &");
	} else if (action == A_REBOOT) {
		system("sync; reboot");
	} else if (action == A_ANDROID) {
		confirm_android = 1;
		render();
	} else if (action == A_CANCEL) {
		confirm_android = 0;
		render();
	} else if (action == A_ANDROID_OK) {
		confirm_android = 0;
		system("/usr/bin/changxing60-boot-android");
	}
}

static void nav_bar(void)
{
	const char *items[] = { "概览", "信号", "流量", "短信", "设置" };
	int n = 5, W = fb_w;
	int y = fb_h - NAV_H;
	fill_rect(0, y, W, NAV_H, 0x00101010);
	fill_rect(0, y, W, 1, 0x001C1C1E);
	int cw = W / n;
	for (int i = 0; i < n; i++) {
		int cx = i * cw + cw / 2;
		u32 col = i == cur_page ? 0x000A84FF : 0x008E8E93;
		if (i == cur_page)
			fill_rrect(i * cw + 8, y + 8, cw - 16, NAV_H - 16, 18, 0x002C2C2E);
		const struct cn_glyph *g = cn_big_find(items[i]);
		int w = g ? g->w : 0;
		int h = g ? g->h : 16;
		draw_cn_big(cx - w / 2, y + (NAV_H - h) / 2, items[i], col);
		if (i == 3 && sms_unread > 0) {
			char nb[8];
			snprintf(nb, sizeof nb, "%d", sms_unread > 99 ? 99 : sms_unread);
			int bw = text_w(1, nb) + 10, bh = 18;
			int bx = cx + w / 2 + 6, by = y + 10;
			fill_rrect(bx, by, bw, bh, 9, 0x00FF453A);
			draw_text(bx + 5, by + 1, 1, 0x00FFFFFF, nb);
		}
		if (i == cur_page)
			fill_rrect(cx - 16, y + 14, 32, 4, 2, 0x000A84FF);
	}
}

static void cpu_refresh(void)
{
	FILE *f = fopen("/proc/stat", "r");
	if (!f) return;
	char line[256];
	if (!fgets(line, sizeof line, f)) { fclose(f); return; }
	fclose(f);
	unsigned long long v[8] = { 0 };
	int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
		&v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
	if (n < 4) return;
	unsigned long long idle = v[3] + (n > 4 ? v[4] : 0);
	unsigned long long total = 0;
	for (int i = 0; i < n; i++) total += v[i];

	if (cpu_prev_total == 0) {
		/* first call: take a second sample 200 ms later so the first paint is not always 0 */
		usleep(200000);
		f = fopen("/proc/stat", "r");
		if (f) {
			unsigned long long w[8] = { 0 };
			if (fgets(line, sizeof line, f)) {
				int m = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
					&w[0], &w[1], &w[2], &w[3], &w[4], &w[5], &w[6], &w[7]);
				if (m >= 4) {
					unsigned long long idle2 = w[3] + (m > 4 ? w[4] : 0);
					unsigned long long total2 = 0;
					for (int i = 0; i < m; i++) total2 += w[i];
					if (total2 > total) {
						cpu_pct = (int)((total2 - total - (idle2 - idle)) * 100 / (total2 - total));
						if (cpu_pct < 0) cpu_pct = 0;
						if (cpu_pct > 100) cpu_pct = 100;
					}
					total = total2;
					idle = idle2;
				}
			}
			fclose(f);
		}
	} else if (total > cpu_prev_total) {
		unsigned long long dt = total - cpu_prev_total;
		unsigned long long di = idle - cpu_prev_idle;
		cpu_pct = (int)((dt - di) * 100 / dt);
		if (cpu_pct > 100) cpu_pct = 100;
	}
	cpu_prev_total = total;
	cpu_prev_idle = idle;

	FILE *lf = fopen("/proc/loadavg", "r");
	if (lf) {
		double l1 = 0;
		if (fscanf(lf, "%lf", &l1) == 1) load_x100 = (int)(l1 * 100);
		fclose(lf);
	}
	FILE *sf = fopen("/proc/stat", "r");
	if (sf) {
		while (fgets(line, sizeof line, sf)) {
			if (sscanf(line, "procs_running %d", &run_n) == 1) continue;
			if (sscanf(line, "procs_blocked %d", &blk_n) == 1) continue;
		}
		fclose(sf);
	}
}

static void batt_eta_refresh(void)
{
	char path[128], buf[32];
	if (!batt_off_ua) {
		if (read_file_str("/tmp/batt-off", buf, sizeof buf) == 0) batt_off_ua = atoll(buf);
	}
	int cap = -1;
	long long cur = 0, full = 0;
	char st[32] = "";
	if (read_file_str("/sys/class/power_supply/battery/capacity", buf, sizeof buf) == 0) cap = atoi(buf);
	if (read_file_str("/sys/class/power_supply/battery/current_now", buf, sizeof buf) == 0) cur = atoll(buf);
	if (read_file_str("/sys/class/power_supply/battery/charge_full", buf, sizeof buf) == 0) full = atoll(buf);
	if (read_file_str("/sys/class/power_supply/bq2560x_charger/status", st, sizeof st) != 0)
		read_file_str("/sys/class/power_supply/battery/status", st, sizeof st);
	batt_charging = strcmp(st, "Charging") == 0 || strcmp(st, "Full") == 0;
	if (cur < 0) cur = -cur;
	/* The panel blanks after a minute, so almost all of the discharge happens with the backlight
	 * off.  Learn that basline while dark (EMA) and use it for the "left" estimate; otherwise
	 * the backlight's own draw would make the estimate far too pessimistic. */
	if (!batt_charging && cur > 20000 && (bl_idle_dark || !screen_on))
		batt_off_ua = batt_off_ua ? (batt_off_ua * 3 + cur) / 4 : cur;
	if (batt_charging && (cap >= 90 || cur < 50000)) {
		/* CV phase: the charger has tapered to a few mA, so a time estimate would be hours of
		 * nonsense.  Say "almost full" instead. */
		batt_eta_min = -2;
	} else {
		long long use = batt_charging ? cur : (batt_off_ua ? batt_off_ua : cur);
		if (batt_charging && use < 250000) use = 250000;   /* keep the CC-phase estimate sane */
		if (cap >= 0 && cap <= 100 && use > 20000 && full > 0) {
			long long uah = (long long)(batt_charging ? 100 - cap : cap) * full / 100;
			batt_eta_min = (int)(uah * 60 / use);
		} else {
			batt_eta_min = -1;
		}
	}
	{ FILE *df = fopen("/tmp/batt-off", "w"); if (df) { fprintf(df, "%lld\n", batt_off_ua); fclose(df); } }
}

/* 1.5x text: nearest-neighbour rescale, i.e. half a step between draw_text scale 1 and 2 */
static int draw_text_15(int x, int y, u32 col, const char *s)
{
	int ox = x;
	for (; *s; s++) {
		unsigned char ch = (unsigned char)*s;
		const unsigned char *g = font8x16 + (ch < 0x20 ? 0 : ch) * 16;
		for (int dy = 0; dy < 24; dy++) {
			int sy = dy * 2 / 3;
			for (int dx = 0; dx < 12; dx++) {
				int sx = dx * 2 / 3;
				if (g[sy] & (0x80 >> sx)) fill_rect(x + dx, y + dy, 1, 1, col);
			}
		}
		x += 12;
	}
	return x - ox;
}

static int draw_cn_15(int x, int y, const char *txt, u32 col)
{
	const struct cn_glyph *g = cn_find(txt);
	if (!g) return draw_text_15(x, y, col, txt);
	int stride = (g->w + 7) / 8;
	int ow = g->w * 3 / 2, oh = g->h * 3 / 2;
	for (int dy = 0; dy < oh; dy++) {
		int sy = dy * 2 / 3;
		for (int dx = 0; dx < ow; dx++) {
			int sx = dx * 2 / 3;
			if (g->bits[sy * stride + (sx >> 3)] & (0x80 >> (sx & 7)))
				fill_rect(x + dx, y + dy, 1, 1, col);
		}
	}
	return ow;
}

static void page_overview(int P, int CW)
{
	const int W = fb_w;
	const u32 TXT = 0x00FFFFFF, DIM = 0x008E8E93;
	const u32 OK = 0x0030D158, WARN = 0x00FF9F0A, BAD = 0x00FF453A;
	char b[64];

	card(P, 106, CW, 154);
	draw_cn(P + 18, 122, 1, "本次开机", DIM);
	human_bytes_short(wan_rx, b, sizeof b);
	draw_cn(P + 18, 150, 1, "下载", DIM);
	draw_text(P + 18, 172, 3, TXT, b);
	human_bytes_short(wan_tx, b, sizeof b);
	draw_cn(W / 2 + 10, 150, 1, "上传", DIM);
	draw_text(W / 2 + 10, 172, 3, TXT, b);
	{
		int gx = P + 18, gy = 222, gw = CW - 36, gh = 30;
		double mx = 1000;
		draw_text(W - 150, 122, 1, 0x000A84FF, "DL");
		draw_text(W - 90, 122, 1, 0x0030D158, "UL");
		fill_rect(gx, gy, gw, gh, 0x00102033);
		for (int i = 0; i < 60; i++) {
			if (rate_hist[i] > mx) mx = rate_hist[i];
			if (rate_hist_tx[i] > mx) mx = rate_hist_tx[i];
		}
		fill_rect(gx, gy + gh - 1, gw, 1, 0x00304766);
		for (int i = 0; i < 60; i++) {
			int bw = gw / 60 - 1;
			if (bw < 1) bw = 1;
			int hrx = (int)(rate_hist[i] / mx * gh);
			int htx = (int)(rate_hist_tx[i] / mx * gh);
			if (hrx > 0) fill_rect(gx + i * (gw / 60), gy + gh - hrx, bw, hrx, 0x000A84FF);
			if (htx > 0 && bw >= 2) fill_rect(gx + i * (gw / 60) + bw - 1, gy + gh - htx, 1, htx, 0x0030D158);
		}
	}

	card(P, 268, CW, 100);
	draw_cn(P + 18, 280, 1, "网络", DIM);
	snprintf(b, sizeof b, "%s %s", wan_if, wan_state);
	draw_text(P + 18, 302, 2, strcmp(wan_state, "up") == 0 ? OK : WARN, b);
	snprintf(b, sizeof b, "%s/%s", wan_ip, wan_mask);
	draw_text(P + 18, 332, 1, TXT, b);
	{
		const char *v6 = wan6_show[0] ? wan6_show : "无";
		snprintf(b, sizeof b, "IPv6 %s", v6);
		draw_text(P + 18, 350, 1, wan6_show[0] ? TXT : DIM, b);
	}

	card(P, 376, CW, 172);
	draw_cn(P + 18, 390, 1, "热点", DIM);
	draw_text(P + 18, 414, 2, TXT, ssid);
	snprintf(b, sizeof b, "%d client%s   ch%s", wifi_clients, wifi_clients == 1 ? "" : "s", wifi_ch);
	draw_text(P + 18, 448, 1, wifi_clients > 0 ? OK : WARN, b);
	draw_text(P + 18, 470, 1, DIM, wifi_addr);
	{
		unsigned char qrm[41 * 41];
		int qn = qr_encode_wifi(qrm);
		if (qn > 0) {
			int qpx = 124, qs = qpx / (qn + 4);
			if (qs < 1) qs = 1;
			qpx = (qn + 4) * qs;
			draw_qr(W - P - 18 - qpx, 392 + (122 - qpx) / 2, qs, qrm, qn);
		}
	}

	card(P, 556, CW, 116);
	draw_cn(P + 18, 570, 1, "电池", DIM);
	snprintf(b, sizeof b, "%d%%", batt_cap >= 0 ? batt_cap : 0);
	draw_text(P + 18, 594, 3, batt_cap <= 15 ? BAD : (batt_cap <= 30 ? WARN : OK), b);
	if (batt_eta_min == -2) {
		int lw = draw_cn_15(0, -200, "即将充满", 0);
		draw_cn_15(W - 18 - lw, 606, "即将充满", OK);
	} else if (batt_eta_min >= 0) {
		char tb[16];
		const char *lbl = batt_charging ? "充电" : "剩余";
		snprintf(tb, sizeof tb, "%dh%02dm", batt_eta_min / 60, batt_eta_min % 60);
		int lw = draw_cn_15(0, -200, lbl, 0);
		int tw = (int)strlen(tb) * 12;
		int x = W - 18 - (lw + 6 + tw);
		draw_cn_15(x, 606, lbl, batt_charging ? OK : DIM);
		draw_text_15(x + lw + 6, 606, batt_charging ? OK : DIM, tb);
	}
	snprintf(b, sizeof b, "%d.%02dV   %s", batt_volt / 1000, (batt_volt % 1000) / 10, batt_status);
	draw_text(P + 18, 638, 1, DIM, b);
	bar(P + 18, 658, CW - 36, 10, batt_cap, batt_cap <= 15 ? BAD : (batt_cap <= 30 ? WARN : OK));

	{
		int used_kb = mem_total_kb - mem_avail_kb;
		int mpct = mem_total_kb > 0 ? used_kb * 100 / mem_total_kb : 0;
		card(P, 680, CW, 88);
		draw_cn(P + 18, 688, 1, "系统", DIM);
		int half = (CW - 36 - 10) / 2;
		int rx = P + 18 + half + 10;
		snprintf(b, sizeof b, "CPU %d%%", cpu_pct);
		draw_text(P + 18, 704, 1, cpu_pct >= 85 ? BAD : TXT, b);
		snprintf(b, sizeof b, "MEM %d%%", mpct);
		draw_text(rx, 704, 1, mpct >= 90 ? BAD : (mpct >= 75 ? WARN : TXT), b);
		fill_rect(P + 18, 722, half, 8, 0x003A3A3C);
		bar(P + 18, 722, half, 8, cpu_pct, cpu_pct >= 85 ? BAD : (cpu_pct >= 60 ? WARN : 0x000A84FF));
		fill_rect(rx, 722, half, 8, 0x003A3A3C);
		bar(rx, 722, half, 8, mpct, mpct >= 90 ? BAD : (mpct >= 75 ? WARN : 0x000A84FF));
		snprintf(b, sizeof b, "run %d  blk %d", run_n, blk_n);
		draw_text(P + 18, 738, 1, DIM, b);
		snprintf(b, sizeof b, "%d/%d MB", used_kb / 1024, mem_total_kb / 1024);
		draw_text(rx, 738, 1, DIM, b);
	}
}

static void page_signal(int P, int CW)
{
	const u32 TXT = 0x00FFFFFF, DIM = 0x008E8E93, OK = 0x0030D158, WARN = 0x00FF9F0A, BAD = 0x00FF453A;
	char b[64];
	const char *rat = (sig_act == 11 || sig_act == 10) ? "NR5G" : (sig_act ? "LTE" : "--");

	card(P, 106, CW, 200);
	draw_cn(P + 18, 122, 1, "网络", DIM);
	snprintf(b, sizeof b, "PLMN %s   %s", sig_plmn, rat);
	draw_text(P + 18, 146, 1, TXT, b);
	snprintf(b, sizeof b, "band %d   %d MHz", sig_band, sig_bw);
	draw_text(P + 18, 170, 1, TXT, b);
	snprintf(b, sizeof b, "ARFCN %d   PCI %d", sig_arfcn, sig_pci);
	draw_text(P + 18, 194, 1, TXT, b);
	snprintf(b, sizeof b, "gNB %s   CID %s", sig_gnb, sig_cid);
	draw_text(P + 18, 218, 1, TXT, b);
	snprintf(b, sizeof b, "TAC %s", sig_tac);
	draw_text(P + 18, 242, 1, DIM, b);
	snprintf(b, sizeof b, "%s/%s", wan_ip, wan_mask);
	draw_text(P + 18, 266, 1, DIM, b);

	card(P, 318, CW, 170);
	draw_cn(P + 18, 334, 1, "信号", DIM);
	if (sig_ok) {
		snprintf(b, sizeof b, "RSRP %.0f dBm", sig_rsrp);
		draw_text(P + 18, 358, 2, sig_rsrp > -90 ? OK : (sig_rsrp > -105 ? WARN : BAD), b);
		snprintf(b, sizeof b, "RSRQ %.1f dB    SINR %.1f dB", sig_rsrq, sig_sinr);
		draw_text(P + 18, 392, 1, TXT, b);
	} else {
		draw_text(P + 18, 358, 1, DIM, "no engineering data (AT busy)");
	}
	snprintf(b, sizeof b, "MCS UL %d   DL %d", sig_mcs_ul, sig_mcs_dl);
	draw_text(P + 18, 416, 1, TXT, b);
	if (sig_bler_ul >= 0 || sig_bler_dl >= 0)
		snprintf(b, sizeof b, "BLER UL %.1f%%   DL %.1f%%", sig_bler_ul * 100, sig_bler_dl * 100);
	else
		snprintf(b, sizeof b, "BLER n/a");
	draw_text(P + 18, 440, 1, DIM, b);
	snprintf(b, sizeof b, "neighbours %d   SINR %s", sig_neigh,
		 sig_sinr > 10 ? "good" : (sig_sinr > 0 ? "ok" : "poor"));
	draw_text(P + 18, 464, 1, DIM, b);

	card(P, 500, CW, 160);
	draw_cn(P + 18, 516, 1, "邻区", DIM);
	if (nb_n == 0) {
		draw_text(P + 90, 516, 1, DIM, "no neighbour data");
	} else {
		for (int i = 0; i < nb_n; i++) {
			snprintf(b, sizeof b, "PCI %d  %.0f dBm", nb_pci[i], nb_rsrp[i]);
			draw_text(P + 18 + (i % 2) * 230, 544 + (i / 2) * 24, 1, TXT, b);
		}
	}
	snprintf(b, sizeof b, "%d client(s)   %s   ch%s", wifi_clients, ssid, wifi_ch);
	draw_text(P + 18, 596, 1, TXT, b);
	snprintf(b, sizeof b, "CPU %d.%dC   %s", temp_max / 1000, (temp_max % 1000) / 100, wan_if);
	draw_text(P + 18, 620, 1, DIM, b);
}

static void page_traffic(int P, int CW)
{
	const int W = fb_w;
	const u32 TXT = 0x00FFFFFF, DIM = 0x008E8E93, OK = 0x0030D158;
	char b[64], r[32];

	card(P, 106, CW, 144);
	draw_cn(P + 18, 120, 1, "实时速率", DIM);
	human_bps(rate_rx_bps, r, sizeof r);
	snprintf(b, sizeof b, "DL %s", r);
	draw_text(P + 18, 146, 2, OK, b);
	human_bps(rate_tx_bps, r, sizeof r);
	snprintf(b, sizeof b, "UL %s", r);
	draw_text(P + 18, 182, 2, TXT, b);
	snprintf(b, sizeof b, "rx %llu  tx %llu", wan_rx, wan_tx);
	draw_text(P + 18, 220, 1, DIM, b);

	card(P, 262, CW, 140);
	draw_cn(P + 18, 276, 1, "本次开机", DIM);
	draw_cn(P + 18, 306, 1, "下载", DIM);
	draw_cn(W / 2 + 10, 306, 1, "上传", DIM);
	human_bytes_short(wan_rx, b, sizeof b);
	draw_text(P + 18, 328, 3, TXT, b);
	human_bytes_short(wan_tx, b, sizeof b);
	draw_text(W / 2 + 10, 328, 3, TXT, b);

	card(P, 414, CW, 80);
	draw_cn(P + 18, 428, 1, "运行时间", DIM);
	snprintf(b, sizeof b, "%lldh %02lldm", sys_uptime / 3600, (sys_uptime / 60) % 60);
	draw_text(P + 18, 452, 2, TXT, b);

	card(P, 506, CW, 254);
	{
		int tw = draw_cn(P + 18, 522, 1, "终端流量", DIM);
		unsigned long long sum = 0;
		for (int i = 0; i < cli_use_n; i++) sum += cli_use[i].tot_u + cli_use[i].tot_d;
		char tb[24];
		human_bytes_short(sum, tb, sizeof tb);
		snprintf(b, sizeof b, " %d  ", cli_use_n);
		draw_text(P + 18 + tw, 522, 1, TXT, b);
		draw_text(P + 18 + tw + text_w(1, b) + 8, 522, 1, DIM, tb);
	}
	draw_text(P + CW - 100, 522, 1, DIM, "D/U");
	int rows = cli_use_n > 6 ? 6 : cli_use_n;
	for (int i = 0; i < rows; i++) {
		char td[24], tu[24];
		human_bytes_short(cli_use[i].tot_d, td, sizeof td);
		human_bytes_short(cli_use[i].tot_u, tu, sizeof tu);
		const char *nm = cli_use[i].name[0] ? cli_use[i].name : cli_use[i].ip;
		if (cli_use[i].name[0])
			snprintf(b, sizeof b, "%s %s", nm, cli_use[i].ip);
		else
			snprintf(b, sizeof b, "%s", nm);
		draw_text(P + 18, 552 + i * 30, 1, TXT, b);
		snprintf(b, sizeof b, "%s/%s", td, tu);
		draw_text(P + CW - 18 - text_w(1, b), 552 + i * 30, 1, DIM, b);
	}
	if (rows == 0)
		draw_text(P + 18, 552, 1, DIM, "no client traffic");
	else if (cli_use_n > rows) {
		snprintf(b, sizeof b, "+%d more", cli_use_n - rows);
		draw_text(P + CW - 18 - text_w(1, b), 552 + rows * 30, 1, DIM, b);
	}
}

/* ---------- optional GB2312 glyphs for SMS bodies ---------- */
static int utf8_next(const char *s, unsigned *cp)
{
	unsigned char c = (unsigned char)s[0];
	if (c < 0x80) { *cp = c; return 1; }
	if ((c & 0xE0) == 0xC0 && s[1]) { *cp = ((c & 0x1Fu) << 6) | (s[1] & 0x3Fu); return 2; }
	if ((c & 0xF0) == 0xE0 && s[1] && s[2]) {
		*cp = ((c & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
		return 3;
	}
	*cp = c;
	return (c & 0xF8) == 0xF0 ? 4 : 1;
}

static const unsigned char *cjk_glyph(unsigned cp)
{
	int lo = 0, hi = CJK_N - 1;
	while (lo <= hi) {
		int m = (lo + hi) / 2;
		unsigned v = cjk_cp[m];
		if (v == cp) return cjk_bits[m];
		if (v < cp) lo = m + 1;
		else hi = m - 1;
	}
	return NULL;
}

static int cjk_punct(unsigned cp)
{
	/* no line may start with closing punctuation: keep it on the previous line */
	static const unsigned short p[] = {0x3002,0xFF0C,0x3001,0xFF1B,0xFF1A,0xFF01,0xFF1F,0xFF09,0x3011,0x300D,0x300F,0x3015,0x2026,0xFF0E};
	for (unsigned i = 0; i < sizeof p / sizeof p[0]; i++) if (p[i] == cp) return 1;
	return 0;
}

static void draw_cjk(int x, int y, const unsigned char *g, u32 color)
{
	for (int r = 0; r < 16; r++) {
		unsigned bits = ((unsigned)g[r * 2] << 8) | g[r * 2 + 1];
		for (int c = 0; c < 16; c++)
			if (bits & (0x8000u >> c)) fill_rect(x + c, y + r, 1, 1, color);
	}
}

/* Mixed ASCII/CJK body text: ASCII stays 8 px, CJK glyphs are 16x16, wrapped at maxx. */
static void draw_body_utf8(int x, int y, int maxx, int bottom, const char *s, u32 color)
{
	int cx = x, cy = y;
	while (*s && cy < bottom) {
		unsigned cp;
		int n = utf8_next(s, &cp);
		s += n;
		if (cp == '\n' || cp == '\r') { cx = x; cy += 20; continue; }
		if (cp < 0x20) continue;
		if (cp < 0x80) {
			if (cx + 8 > maxx) { cx = x; cy += 20; if (cy >= bottom) break; }
			char b[2] = { (char)cp, 0 };
			draw_text(cx, cy, 1, color, b);
			cx += 8;
		} else {
			const unsigned char *g = cjk_glyph(cp);
			if (cx + 16 > maxx && cjk_punct(cp) && cx > x) {
				if (g) draw_cjk(cx, cy, g, color);
				cx = x; cy += 20; if (cy >= bottom) break;
				continue;
			}
			if (cx + 16 > maxx) { cx = x; cy += 20; if (cy >= bottom) break; }
			if (g) draw_cjk(cx, cy, g, color);
			else draw_text(cx, cy, 1, color, "?");
			cx += 16;
		}
	}
}

static void page_sms(int P, int CW)
{
	const u32 TXT = 0x00FFFFFF, DIM = 0x008E8E93, ACC = 0x000A84FF;
	char b[96];

	card(P, 106, CW, 156);
	draw_cn(P + 18, 122, 1, "短信", DIM);
	snprintf(b, sizeof b, "%d", sms_total);
	draw_text(P + 18, 146, 4, ACC, b);
	snprintf(b, sizeof b, "%d unread", sms_unread);
	draw_text(P + 18, 218, 1, sms_unread ? 0x00FF9F0A : DIM, b);
	if (sms_from[0]) {
		snprintf(b, sizeof b, "%s  %s", sms_from, sms_time);
		draw_text(P + 18, 240, 1, DIM, b);
	}

	card(P, 274, CW, 486);
	draw_cn(P + 18, 290, 1, "最新短信", DIM);
	int y = 316;
	if (sms_code[0]) {
		draw_text(P + 18, y, 1, DIM, "code");
		draw_text(P + 74, y - 10, 3, 0x00FF9F0A, sms_code);
		y += 56;
	}
	if (!sms_body[0]) {
		draw_text(P + 18, y, 1, DIM, sms_total ? "tap LuCI for the body" : "no messages");
		return;
	}

	draw_body_utf8(P + 18, y, P + CW - 18, 744, sms_body, TXT);
}

static void page_clients(int P, int CW)
{
	const u32 TXT = 0x00FFFFFF, DIM = 0x008E8E93;
	char b[64];

	card(P, 106, CW, 96);
	draw_cn(P + 18, 122, 1, "在线设备", DIM);
	snprintf(b, sizeof b, "%d", cli_n ? cli_n : wifi_clients);
	draw_text(P + 18, 146, 3, TXT, b);
	snprintf(b, sizeof b, "%s  ch%s", ssid, wifi_ch);
	draw_text(P + 18, 172, 1, DIM, b);

	card(P, 218, CW, 60 + 26 * (cli_n > 6 ? 6 : (cli_n ? cli_n : 1)));
	draw_cn(P + 18, 234, 1, "已连接", DIM);
	if (cli_n == 0) {
		draw_text(P + 18, 262, 1, DIM, "no clients");
	} else {
		int n = cli_n > 6 ? 6 : cli_n;
		for (int i = 0; i < n; i++)
			draw_text(P + 18, 262 + i * 26, 1, TXT, cli_mac[i]);
	}
}

/* ---- fractional mixed text (nearest-neighbour num/den): the node name sits at 3:2
 * (24px CJK / 12px ASCII), the delay number a step larger at 7:4 (28px / 14px). ---- */
static void draw_ascii_fx(int x, int y, u32 color, const char *s, int num, int den)
{
	int aw = 8 * num / den, ah = 16 * num / den;
	for (; *s; s++) {
		unsigned char ch = (unsigned char)*s;
		const unsigned char *g = font8x16 + (ch < 0x20 ? 0 : ch) * 16;
		for (int dy = 0; dy < ah; dy++) {
			int sy = dy * den / num;
			unsigned char bits = g[sy];
			for (int dx = 0; dx < aw; dx++)
				if (bits & (0x80 >> (dx * den / num)))
					fill_rect(x + dx, y + dy, 1, 1, color);
		}
		x += aw;
	}
}

static void draw_cjk_fx(int x, int y, const unsigned char *g, u32 color, int num, int den)
{
	int cw = 16 * num / den, ch = 16 * num / den;
	for (int dy = 0; dy < ch; dy++) {
		int sy = dy * den / num;
		unsigned bits = ((unsigned)g[sy * 2] << 8) | g[sy * 2 + 1];
		for (int dx = 0; dx < cw; dx++)
			if (bits & (0x8000u >> (dx * den / num)))
				fill_rect(x + dx, y + dy, 1, 1, color);
	}
}

static int draw_body_fx(int x, int y, int maxx, const char *s, u32 color, int num, int den)
{
	int aw = 8 * num / den, cw = 16 * num / den;
	int cx = x;
	while (*s) {
		unsigned cp;
		int n = utf8_next(s, &cp);
		s += n;
		if (cp < 0x20) continue;
		if (cp < 0x80) {
			if (cx + aw > maxx) break;
			char b[2] = { (char)cp, 0 };
			draw_ascii_fx(cx, y, color, b, num, den);
			cx += aw;
		} else if (cp >= 0x1F000 || cp == 0xFE0F) {
			continue;
		} else {
			const unsigned char *g = cjk_glyph(cp);
			if (cx + cw > maxx) break;
			if (g) draw_cjk_fx(cx, y, g, color, num, den);
			else draw_ascii_fx(cx, y, color, "?", num, den);
			cx += cw;
		}
	}
	return cx - x;
}

static int draw_body_15(int x, int y, int maxx, const char *s, u32 color)
{
	return draw_body_fx(x, y, maxx, s, color, 3, 2);
}

static void fmt_duration(int sec, char *out, size_t n)
{
	if (sec >= 3600) snprintf(out, n, "%dh%02dm", sec / 3600, (sec % 3600) / 60);
	else if (sec >= 60) snprintf(out, n, "%dm", sec / 60);
	else snprintf(out, n, "%ds", sec);
}

static void page_settings(int P, int CW)
{
	const u32 TXT = 0x00FFFFFF, DIM = 0x008E8E93, OK = 0x0030D158;
	const u32 WARN = 0x00FF9F0A, BAD = 0x00FF453A;
	const char *lv[] = { "100%", "75%", "50%", "25%", "0%" };
	char b[64], d[32], u[32];
	int used_kb = mem_total_kb - mem_avail_kb;
	int mpct = mem_total_kb > 0 ? used_kb * 100 / mem_total_kb : 0;

	card(P, 100, CW, 96);
	draw_cn(P + 18, 116, 1, "设置", DIM);
	snprintf(b, sizeof b, "brightness %s", lv[clamp_i(bl_index, 0, 4)]);
	draw_text(P + 18, 140, 1, OK, b);
	snprintf(b, sizeof b, "uptime %lldh   temp %d.%dC   mem %d%%", sys_uptime / 3600,
		 temp_max / 1000, (temp_max % 1000) / 100, mpct);
	draw_text(P + 18, 162, 1, DIM, b);

	add_btn(P, 214, 120, 56, "切安卓", A_ANDROID);
	draw_btn(&btns[nbtn - 1], 0x003A3A3C, TXT);
	add_btn(P + 130, 214, CW - 130, 56, "亮度", A_BRIGHT);
	draw_btn(&btns[nbtn - 1], 0x003A3A3C, TXT);
	add_btn(P, 284, CW, 56, "重启", A_REBOOT);
	draw_btn(&btns[nbtn - 1], 0x0033191C, 0x00FF453A);

	card(P, 356, CW, 320);
	draw_text(P + 18, 374, 1, DIM, "ShellCrash / mihomo");
	snprintf(b, sizeof b, "SC %s / %s", clash_scver, clash_ver);
	draw_text(P + CW - 18 - text_w(1, b), 374, 1, DIM, b);

	if (access("/tmp/sc-refreshing", F_OK) == 0) {
		draw_cn(P + 18, 404, 1, "刷新中", WARN);
		draw_text(P + 18, 432, 1, DIM, "subscription + core restart");
	} else if (!clash_up) {
		draw_cn(P + 18, 404, 1, "已停止", BAD);
		draw_text(P + 18, 432, 1, DIM, "/etc/init.d/shellcrash start");
	} else {
		/* status: state, pid, and how long the core has been up */
		draw_cn(P + 18, 402, 1, "运行中", OK);
		snprintf(b, sizeof b, "pid %s", clash_pid);
		draw_text(P + 88, 402, 1, DIM, b);
		if (clash_uptime > 0) {
			char up[16];
			fmt_duration(clash_uptime, up, sizeof up);
			snprintf(b, sizeof b, "up %s", up);
			draw_text(P + CW - 18 - text_w(1, b), 402, 1, DIM, b);
		}

		/* mode / controller port / RSS */
		draw_body_utf8(P + 18, 430, P + 60, 446, "模式", DIM);
		draw_text(P + 62, 430, 1, TXT, clash_mode);
		draw_body_utf8(P + 150, 430, P + 192, 446, "端口", DIM);
		snprintf(b, sizeof b, "%d", clash_port);
		draw_text(P + 194, 430, 1, TXT, b);
		draw_body_utf8(P + 270, 430, P + 312, 446, "内存", DIM);
		if (clash_mem_kb > 0) snprintf(b, sizeof b, "%dM", clash_mem_kb / 1024);
		else snprintf(b, sizeof b, "-");
		draw_text(P + 322, 430, 1, TXT, b);

		/* rule count / live connections */
		draw_cn(P + 18, 458, 1, "节点", DIM);
		snprintf(b, sizeof b, "%d", clash_proxies);
		draw_text(P + 70, 458, 1, TXT, b);
		draw_cn(P + 150, 458, 1, "连接", DIM);
		snprintf(b, sizeof b, "%d", clash_conns);
		draw_text(P + 202, 458, 1, TXT, b);

		/* traffic since the core started */
		draw_cn(P + 18, 486, 1, "下载", DIM);
		human_bytes_short((unsigned long long)clash_down, d, sizeof d);
		draw_text(P + 70, 486, 1, TXT, d);
		draw_cn(P + 200, 486, 1, "上传", DIM);
		human_bytes_short((unsigned long long)clash_up_b, u, sizeof u);
		draw_text(P + 252, 486, 1, TXT, u);

		/* rule-provider entries / TUN / core CPU */
		draw_body_utf8(P + 18, 514, P + 50, 530, "规则", DIM);
		if (clash_rules >= 1000) snprintf(b, sizeof b, "%dk", clash_rules / 1000);
		else snprintf(b, sizeof b, "%d", clash_rules);
		draw_text(P + 54, 514, 1, TXT, b);
		draw_text(P + 140, 514, 1, DIM, "TUN");
		draw_text(P + 172, 514, 1, clash_tun ? OK : DIM, clash_tun ? "on" : "off");
		draw_text(P + 240, 514, 1, DIM, "CPU");
		snprintf(b, sizeof b, "%d%%", clash_cpu);
		draw_text(P + 276, 514, 1, TXT, b);

		fill_rect(P + 18, 538, CW - 36, 1, 0x003A3A3C);

		/* which node is actually carrying the traffic: ask mihomo, at most every 10 s,
		 * off the draw path (cxnow writes /tmp/sc-now* and we just read the files) */
		static time_t nowq = 0;
		time_t tnow = time(NULL);
		if (tnow - nowq >= 10) { nowq = tnow; system("/usr/bin/cxnow >/dev/null 2>&1 &"); }
		read_file_str("/tmp/sc-now", sc_now, sizeof sc_now);
		read_file_str("/tmp/sc-now-delay", sc_now_delay, sizeof sc_now_delay);

		draw_body_utf8(P + 18, 548, P + 100, 564, "当前节点", DIM);
		if (sc_now[0]) draw_body_15(P + 18, 568, P + CW - 18, sc_now, TXT);
		else draw_text(P + 18, 568, 1, DIM, "-");

		draw_body_utf8(P + 18, 600, P + 60, 616, "延时", DIM);
		if (access("/tmp/sc-testing", F_OK) == 0)
			draw_body_fx(P + 18, 622, P + CW - 18, "testing...", WARN, 7, 4);
		else if (sc_now_delay[0] && strcmp(sc_now_delay, "-1") != 0) {
			snprintf(b, sizeof b, "%s ms", sc_now_delay);
			draw_body_fx(P + 18, 622, P + CW - 18, b, OK, 7, 4);
		} else
			draw_body_fx(P + 18, 622, P + CW - 18, "-", DIM, 7, 4);
	}

	add_btn(P, 700, (CW - 10) / 2, 56, "自动优选", A_SCTEST);
	draw_btn(&btns[nbtn - 1], 0x002C2C2E, 0x000A84FF);
	add_btn(P + (CW - 10) / 2 + 10, 700, (CW - 10) / 2, 56, "刷新节点", A_SCRF);
	draw_btn(&btns[nbtn - 1], 0x002C2C2E, 0x000A84FF);
}

/* ---------- confirmation overlay for the Android switch ---------- */
static int hit_rect(int x, int y, const int r[4])
{
	return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
}

static void confirm_overlay(int P, int CW)
{
	const u32 TXT = 0x00FFFFFF, DIM = 0x008E8E93;
	int bw = (CW - 50) / 2, by = 470, bh = 64;

	fill_rect(0, 0, fb_w, fb_h, 0x00000000);
	card(P, 250, CW, 300);
	draw_cn(P + 18, 274, 1, "切安卓", TXT);
	draw_text(P + 18, 304, 1, DIM, "switch to Android?");
	draw_text(P + 18, 326, 1, DIM, "the panel reboots into slot a");

	ov_yes[0] = P + 10; ov_yes[1] = by; ov_yes[2] = bw; ov_yes[3] = bh;
	ov_no[0] = P + CW - bw - 10; ov_no[1] = by; ov_no[2] = bw; ov_no[3] = bh;

	fill_rrect(ov_yes[0], by, bw, bh, 14, 0x0033191C);
	int w = draw_cn(0, -100, 1, "确认", 0x00FF453A);
	draw_cn(ov_yes[0] + (bw - w) / 2, by + (bh - 16) / 2, 1, "确认", 0x00FF453A);

	fill_rrect(ov_no[0], by, bw, bh, 14, 0x003A3A3C);
	w = draw_cn(0, -100, 1, "取消", TXT);
	draw_cn(ov_no[0] + (bw - w) / 2, by + (bh - 16) / 2, 1, "取消", TXT);
}

static void render_portrait(void)
{
	const int W = fb_w, P = 14, CW = W - 2 * P;
	if (getenv("CX_CONFIRM")) confirm_android = 1;   /* screenshot/debug hook */
	const u32 TXT = 0x00FFFFFF, DIM = 0x008E8E93, OK = 0x0030D158, WARN = 0x00FF9F0A, BAD = 0x00FF453A;
	const char *titles[] = { "概览", "信号", "流量", "短信", "设置" };
	char b[64];
	time_t now = time(NULL);
	struct tm tmv;

	static int tz_ready = 0;
	if (!tz_ready) {
		char z[32];
		if (read_file_str("/etc/TZ", z, sizeof z) == 0) { setenv("TZ", z, 1); tzset(); }
		tz_ready = 1;
	}

	static int page_ready = 0;
	if (!page_ready) {
		const char *e = getenv("CX_PAGE");
		if (e) cur_page = clamp_i(atoi(e), 0, 5);
		page_ready = 1;
	}

	if (!render_skip_refresh) {
		wan6_refresh();
		rate_refresh();
		clash_refresh(0);
		if (cur_page == 1) { sig_refresh(0); clients_refresh(0); }
		cpu_refresh();
		batt_eta_refresh();
		sms_refresh(0);
		if (cur_page == 2) usage_refresh();
	} else {
		render_skip_refresh = 0;
	}

	nbtn = 0;
	fill_rect(0, 0, W, fb_h, 0x00000000);

	fill_rect(0, 0, W, 46, 0x00000000);
	draw_text(P, 15, 1, TXT, "changxing60");
	localtime_r(&now, &tmv);
	snprintf(b, sizeof b, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
	draw_text(W - P - text_w(1, b), 15, 1, DIM, b);
	{
		int bx = W - P - text_w(1, b) - 76;
		fill_rrect(bx, 17, 24, 12, 3, DIM);
		fill_rect(bx + 25, 20, 3, 6, DIM);
		fill_rrect(bx + 2, 19, 20, 8, 2, 0x000A1526);
		int cap = batt_cap < 0 ? 0 : (batt_cap > 100 ? 100 : batt_cap);
		if (cap > 0) fill_rrect(bx + 2, 19, 20 * cap / 100, 8, 2, cap <= 15 ? BAD : (cap <= 30 ? WARN : OK));
		snprintf(b, sizeof b, "%d%%", batt_cap >= 0 ? batt_cap : 0);
		draw_text(bx + 32, 15, 1, cap <= 15 ? BAD : TXT, b);
	}

	draw_cn_big(P, 58, titles[cur_page], TXT);
	draw_text(W - P - text_w(1, "OpenWrt 25.12.5"), 64, 1, 0x008E8E93, "OpenWrt 25.12.5");

	switch (cur_page) {
	case 1: page_signal(P, CW); break;
	case 2: page_traffic(P, CW); break;
	case 3: page_sms(P, CW); break;
	case 4: page_settings(P, CW); break;
	case 5: page_clients(P, CW); break;
	default: page_overview(P, CW); break;
	}

	nav_bar();
	if (confirm_android) confirm_overlay(P, CW);
}

/* Finger down: light up the button under it right away, before the action runs on lift. Coordinates
 * arrive as raw panel units; map them the same way touch_tap() does. */
static void touch_press(void)
{
	if (bl_idle_dark || !screen_on || confirm_android)
		return;
	int x = touch_x, y = touch_y;
	if (touch_max_x > 0) x = touch_x * fb_w / touch_max_x;
	if (touch_max_y > 0) y = touch_y * fb_h / touch_max_y;
	if (x < 0 || y < 0 || y >= fb_h - NAV_H)
		return;
	int i = hit_btn(x, y);
	int action = i >= 0 ? btns[i].action : A_NONE;
	if (action == press_action)
		return;              /* moved inside the same button: already drawn */
	press_action = action;
	if (action != A_NONE)
		bl_idle_seen = time(NULL);
	render();
}

static void touch_tap(void)
{
	int armed = press_action;
	press_action = A_NONE;
	/* Tap-to-wake is deliberately gone: a dark panel ignores touch entirely and only a double
	 * press of the power key lights it again (handle_key). */
	if (bl_idle_dark || !screen_on)
		return;
	int x = touch_x, y = touch_y;
	if (touch_max_x > 0) x = touch_x * fb_w / touch_max_x;
	if (touch_max_y > 0) y = touch_y * fb_h / touch_max_y;
	if (x < 0 || y < 0) return;
	if (confirm_android) {
		if (hit_rect(x, y, ov_yes)) do_action(A_ANDROID_OK);
		else if (hit_rect(x, y, ov_no)) do_action(A_CANCEL);
		return;
	}
	if (y >= fb_h - NAV_H) {
		int p = x / (fb_w / 5);
		if (p >= 0 && p < 5) {
			cur_page = p;
			bl_idle_seen = time(NULL);
			render_skip_refresh = 1;   /* instant page switch; probes run on the next 2 s tick */
			render();
		}
		return;
	}
	int i = hit_btn(x, y);
	if (i >= 0) {
		bl_idle_seen = time(NULL);
		do_action(btns[i].action);
	} else if (armed) {
		render();   /* finger slid off the button: drop the highlight */
	}
}

/* ---- boot log mode: draw /dev/kmsg on the panel until the status service takes over ---------- */
#define BOOTLOG_ROWS 26
static char bootlog_ring[BOOTLOG_ROWS][100];
static int bootlog_n = 0;

static void bootlog_add(const char *line)
{
	char *dst = bootlog_ring[bootlog_n % BOOTLOG_ROWS];
	int n = 0;
	/* strip the "<pri,seq,usec>;" kmsg prefix */
	if (line[0] == '<') {
		const char *semi = strchr(line, ';');
		if (semi) line = semi + 1;
	}
	while (*line && n < 95) { dst[n++] = *line++; }
	dst[n] = 0;
	bootlog_n++;
}

static int bootlog_run(void)
{
	if (drm_init() < 0) return 1;
	if (backlight_find() == 0) { backlight_load(); backlight_apply(); }

	int kfd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
	time_t t0 = time(NULL);
	for (int i = 0; i < BOOTLOG_ROWS; i++) bootlog_ring[i][0] = 0;

	while (!stop_flag) {
		if (kfd >= 0) {
			char buf[512];
			ssize_t r;
			while ((r = read(kfd, buf, sizeof buf - 1)) > 0) {
				buf[r] = 0;
				char *nl = strchr(buf, '\n');
				if (nl) *nl = 0;
				bootlog_add(buf);
			}
		}

		fill_rect(0, 0, fb_w, fb_h, 0x00000000);
		char hdr[64];
		snprintf(hdr, sizeof hdr, "booting  +%lds", (long)(time(NULL) - t0));
		draw_text(10, 8, 1, 0x000A84FF, hdr);
		draw_text(10, 30, 1, 0x008E8E93, "kernel / init log  (/dev/kmsg)");

		int first = bootlog_n > BOOTLOG_ROWS ? bootlog_n - BOOTLOG_ROWS : 0;
		for (int i = 0; i < BOOTLOG_ROWS && first + i < bootlog_n; i++) {
			const char *line = bootlog_ring[(first + i) % BOOTLOG_ROWS];
			if (!line[0]) continue;
			int y = 58 + i * 24;
			if (y > fb_h - 40) break;
			draw_text(10, y, 1, 0x00FFFFFF, line);
		}
		present();

		if (access("/tmp/.bootlog-stop", F_OK) == 0) break;
		if (time(NULL) - t0 > 300) break;
		usleep(250000);
	}
	if (kfd >= 0) close(kfd);
	return 0;
}
