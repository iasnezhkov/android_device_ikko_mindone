#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define TAG "mindone_mux"
#define AID_RADIO 1001

#define MUX_FLAG 0xF9
#define ADDR_EA 0x01
#define ADDR_CR 0x02
#define CTRL_PF 0x10
#define CTRL_SABM 0x2F
#define CTRL_UA 0x63
#define CTRL_DM 0x0F
#define CTRL_DISC 0x43
#define CTRL_UIH 0xEF
#define CTRL_UI 0x03
#define CMD_CR 0x02
#define CMD_PN 0x81
#define CMD_PSC 0x41
#define CMD_CLD 0xC1
#define CMD_TEST 0x21
#define CMD_MSC 0xE1
#define CMD_NSC 0x11
#define MSC_FC 0x02
#define MSC_SIGNALS 0x8D

#define MAX_DLCI 64
#define MAX_SIMS 4
#define KINDS 11
#define DLCI_NWCMD 45
#define DLCI_NWURC 44
#define MAX_CHANS (MAX_SIMS * KINDS + 2)
#define MAX_FRAME 32767
#define FRAME_OVERHEAD 7
#define PTY_CHUNK 1024
#define PENDING_CAP 4096
#define TXQ_CAP 65536
#define RX_CAP 65536
#define CHAT_CAP 4096
#define TX_RETRY_MS 10
#define CHAT_TIMEOUT_MS 5000
#define SETUP_TIMEOUT_MS 30000
#define CLOSE_WAIT_MS 2000
#define EXIT_FLUSH_MS 500
#define RESTART_DELAY_MS 5000
#define REPORT_WAIT_MS 5000
#define EIND_WAIT_MS 20000
#define EIND_WAIT_USER_MS 10000

#define LOG(prio, ...) __android_log_print(prio, TAG, __VA_ARGS__)
#define LOGE(...) LOG(ANDROID_LOG_ERROR, __VA_ARGS__)
#define LOGW(...) LOG(ANDROID_LOG_WARN, __VA_ARGS__)
#define LOGI(...) LOG(ANDROID_LOG_INFO, __VA_ARGS__)
#define LOGD(...) do { if (verbose) LOG(ANDROID_LOG_DEBUG, __VA_ARGS__); } while (0)

enum mux_state { ST_SETUP, ST_RUN, ST_CLOSING, ST_PEER_CLOSING };

struct chan {
	uint8_t dlci;
	bool atci;
	char name[16];
	char link[PATH_MAX];
	char slave[64];
	int fd;
	bool sabm_sent;
	bool open;
	bool disc_sent;
	bool refused;
	bool refused_logged;
	bool peer_fc;
	bool local_fc;
	uint8_t *pending;
	size_t pending_len;
};

static const uint8_t sim_dlci[MAX_SIMS][KINDS] = {
	{ 1, 2, 3, 4, 5, 26, 61, 60, 59, 58, 43 },
	{ 6, 7, 8, 9, 10, 27, 57, 56, 55, 54, 42 },
	{ 11, 12, 13, 14, 15, 28, 53, 52, 51, 50, 41 },
	{ 16, 17, 18, 19, 20, 29, 49, 48, 47, 46, 40 },
};

static const char *const kind_name[KINDS] = {
	"cmd4", "noti", "cmd1", "cmd2", "cmd3", NULL, "cmd7", "cmd8", "cmd9", "cmd10", "cmd11",
};

static const char *serial_path = "/dev/ttyC0";
static const char *link_dir = "/dev/radio";
static int frame_size = 512;
static int baud_index = 5;
static int pin = -1;
static bool verbose;

static struct chan ctl;
static struct chan chans[MAX_CHANS];
static int nchans;
static struct chan *by_dlci[MAX_DLCI];

static int serial_fd = -1;
static uint8_t txq[TXQ_CAP];
static size_t txq_len;
static bool tx_blocked;
static uint8_t rxb[RX_CAP];
static size_t rx_len;
static char chat_buf[CHAT_CAP];
static size_t chat_len;
static uint8_t crc_table[256];

static enum mux_state state;
static bool pf_echo;
static int unresolved;
static bool setup_done;
static bool ril_started;
static int64_t setup_deadline;
static volatile sig_atomic_t term_signal;

static int64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void prop_get(const char *key, char *value, const char *def)
{
	if (__system_property_get(key, value) <= 0)
		snprintf(value, PROP_VALUE_MAX, "%s", def);
}

static void prop_set(const char *key, const char *value)
{
	if (__system_property_set(key, value))
		LOGE("setprop %s %s failed", key, value);
}

static bool user_build(void)
{
	char v[PROP_VALUE_MAX];

	prop_get("vendor.ril.emulation.userload", v, "0");
	if (v[0] == '1')
		return true;
	prop_get("ro.build.type", v, "");
	return !strcmp(v, "user");
}

static int sim_count(void)
{
	char v[PROP_VALUE_MAX];

	prop_get("ro.boot.opt_sim_count", v, "0");
	if (v[0] >= '1' && v[0] <= '4')
		return v[0] - '0';
	prop_get("persist.radio.multisim.config", v, "ss");
	if (!strcmp(v, "dsds") || !strcmp(v, "dsda"))
		return 2;
	if (!strcmp(v, "tsts"))
		return 3;
	if (!strcmp(v, "qsqs"))
		return 4;
	return 1;
}

static void crc_init(void)
{
	for (int i = 0; i < 256; i++) {
		uint8_t c = (uint8_t)i;

		for (int b = 0; b < 8; b++)
			c = (c & 1) ? (uint8_t)((c >> 1) ^ 0xE0) : (uint8_t)(c >> 1);
		crc_table[i] = c;
	}
}

static uint8_t crc_update(uint8_t crc, const uint8_t *p, size_t n)
{
	while (n--)
		crc = crc_table[crc ^ *p++];
	return crc;
}

static void chan_init(struct chan *c, uint8_t dlci, const char *name, bool atci)
{
	memset(c, 0, sizeof(*c));
	c->dlci = dlci;
	c->atci = atci;
	c->fd = -1;
	snprintf(c->name, sizeof(c->name), "%s", name);
	snprintf(c->link, sizeof(c->link), "%s/%s", link_dir, name);
	by_dlci[dlci] = c;
}

static int build_channels(int sims)
{
	char name[16];

	chan_init(&ctl, 0, "control", false);
	for (int s = 0; s < sims; s++) {
		for (int k = 0; k < KINDS; k++) {
			if (!kind_name[k])
				snprintf(name, sizeof(name), "atci%d", s + 1);
			else if (s)
				snprintf(name, sizeof(name), "ptty%d%s", s + 1, kind_name[k]);
			else
				snprintf(name, sizeof(name), "ptty%s", kind_name[k]);
			chan_init(&chans[nchans++], sim_dlci[s][k], name, !kind_name[k]);
		}
	}
	chan_init(&chans[nchans++], DLCI_NWCMD, "pttynwcmd", false);
	chan_init(&chans[nchans++], DLCI_NWURC, "pttynwurc", false);
	for (int i = 0; i < nchans; i++) {
		chans[i].pending = malloc(PENDING_CAP + (size_t)frame_size);
		if (!chans[i].pending)
			return -1;
	}
	return 0;
}

static size_t txq_room(void)
{
	return sizeof(txq) - txq_len;
}

static bool tx_frame(uint8_t dlci, uint8_t ctrl, const uint8_t *data, size_t len)
{
	uint8_t hdr[5];
	size_t h = 0;
	uint8_t crc;

	if (len > (size_t)frame_size) {
		LOGE("refusing a %zu byte frame on dlci %u, frame size is %d", len, dlci, frame_size);
		return false;
	}
	if ((ctrl == CTRL_UIH || ctrl == CTRL_UI) && pf_echo && len && (data[0] & ~CMD_CR) == CMD_MSC) {
		ctrl |= CTRL_PF;
		pf_echo = false;
	}
	hdr[h++] = MUX_FLAG;
	hdr[h++] = (uint8_t)(dlci << 2 | ADDR_CR | ADDR_EA);
	hdr[h++] = ctrl;
	if (len < 128) {
		hdr[h++] = (uint8_t)(len << 1 | 1);
	} else {
		hdr[h++] = (uint8_t)(len << 1);
		hdr[h++] = (uint8_t)(len >> 7);
	}
	crc = crc_update(0xFF, hdr + 1, h - 1);
	if ((ctrl & ~CTRL_PF) == CTRL_UI)
		crc = crc_update(crc, data, len);
	if (txq_room() < h + len + 2) {
		LOGE("tx queue full, dropping a %zu byte frame on dlci %u", len, dlci);
		return false;
	}
	memcpy(txq + txq_len, hdr, h);
	txq_len += h;
	if (len)
		memcpy(txq + txq_len, data, len);
	txq_len += len;
	txq[txq_len++] = (uint8_t)~crc;
	txq[txq_len++] = MUX_FLAG;
	LOGD("tx dlci %u ctrl 0x%02x len %zu", dlci, ctrl, len);
	return true;
}

static int tx_flush(void)
{
	while (txq_len) {
		ssize_t n = write(serial_fd, txq, txq_len);

		if (n > 0) {
			memmove(txq, txq + n, txq_len - (size_t)n);
			txq_len -= (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EBUSY)) {
			tx_blocked = true;
			return 0;
		}
		if (n < 0 && (errno == ETXTBSY || errno == ENODEV)) {
			LOGE("modem is not accepting data (%s), dropping %zu queued bytes", strerror(errno), txq_len);
			prop_set("vendor.ril.mux.ee.md1", "1");
			txq_len = 0;
			break;
		}
		LOGE("write %s: %s", serial_path, n < 0 ? strerror(errno) : "nothing written");
		return -1;
	}
	tx_blocked = false;
	return 0;
}

static int tx_drain(int64_t deadline)
{
	while (txq_len) {
		struct pollfd p = { .fd = serial_fd, .events = POLLOUT };
		int64_t left = deadline - now_ms();

		if (tx_flush())
			return -1;
		if (!txq_len)
			break;
		if (left <= 0)
			return -1;
		poll(&p, 1, left < TX_RETRY_MS ? (int)left : TX_RETRY_MS);
	}
	return 0;
}

static void send_msc(struct chan *c, bool fc)
{
	uint8_t v[4] = {
		CMD_MSC | CMD_CR,
		2 << 1 | 1,
		(uint8_t)(c->dlci << 2 | ADDR_CR | ADDR_EA),
		(uint8_t)(MSC_SIGNALS | (fc ? MSC_FC : 0)),
	};

	tx_frame(0, CTRL_UIH | CTRL_PF, v, sizeof(v));
}

static void chan_close_pty(struct chan *c)
{
	if (c->fd >= 0) {
		unlink(c->link);
		close(c->fd);
		c->fd = -1;
	}
	c->slave[0] = 0;
	c->pending_len = 0;
	c->peer_fc = false;
	c->local_fc = false;
}

static int chan_open_pty(struct chan *c)
{
	struct termios t;
	int fd = open("/dev/ptmx", O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0) {
		LOGE("%s: open /dev/ptmx: %s", c->name, strerror(errno));
		return -1;
	}
	if (ptsname_r(fd, c->slave, sizeof(c->slave))) {
		LOGE("%s: ptsname: %s", c->name, strerror(errno));
		goto fail;
	}
	if (tcgetattr(fd, &t)) {
		LOGE("%s: tcgetattr: %s", c->name, strerror(errno));
		goto fail;
	}
	t.c_iflag = IGNPAR;
	t.c_oflag = 0;
	t.c_cflag = CS8 | CREAD | CLOCAL;
	t.c_lflag = 0;
	memset(t.c_cc, 0, sizeof(t.c_cc));
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	cfsetispeed(&t, B460800);
	cfsetospeed(&t, B460800);
	if (tcflush(fd, TCIFLUSH) || tcsetattr(fd, TCSANOW, &t)) {
		LOGE("%s: termios: %s", c->name, strerror(errno));
		goto fail;
	}
	if (grantpt(fd) || unlockpt(fd)) {
		LOGE("%s: unlock %s: %s", c->name, c->slave, strerror(errno));
		goto fail;
	}
	if (chown(c->slave, AID_RADIO, AID_RADIO))
		LOGD("%s: chown %s: %s", c->name, c->slave, strerror(errno));
	if (chmod(c->slave, 0660)) {
		LOGE("%s: chmod %s: %s", c->name, c->slave, strerror(errno));
		goto fail;
	}
	if (unlink(c->link) && errno != ENOENT)
		LOGW("%s: unlink %s: %s", c->name, c->link, strerror(errno));
	if (symlink(c->slave, c->link)) {
		LOGE("%s: symlink %s -> %s: %s", c->name, c->link, c->slave, strerror(errno));
		goto fail;
	}
	c->fd = fd;
	c->pending_len = 0;
	c->peer_fc = false;
	c->local_fc = false;
	LOGD("%s: dlci %u on %s", c->name, c->dlci, c->slave);
	return 0;
fail:
	close(fd);
	c->slave[0] = 0;
	return -1;
}

static void close_all_ptys(void)
{
	for (int i = 0; i < nchans; i++) {
		chan_close_pty(&chans[i]);
		chans[i].open = false;
		chans[i].sabm_sent = false;
		chans[i].disc_sent = false;
		chans[i].refused = false;
		chans[i].refused_logged = false;
	}
	ctl.open = false;
	ctl.sabm_sent = false;
	ctl.disc_sent = false;
}

static void close_serial(void)
{
	if (serial_fd >= 0)
		close(serial_fd);
	serial_fd = -1;
	txq_len = 0;
	rx_len = 0;
	chat_len = 0;
	tx_blocked = false;
	pf_echo = false;
}

static void on_signal(int sig)
{
	term_signal = sig;
}

static void wait_ms(int ms)
{
	int64_t end = now_ms() + ms;
	int64_t left;

	while (!term_signal && (left = end - now_ms()) > 0)
		poll(NULL, 0, (int)left);
}

static void report_and_exit(const char *why)
{
	LOGE("%s; asking muxreport to recover the modem", why);
	close_all_ptys();
	close_serial();
	prop_set("vendor.ril.mux.report.case", "1");
	prop_set("vendor.ril.muxreport", "1");
	wait_ms(REPORT_WAIT_MS);
	if (!term_signal) {
		prop_set("vendor.ril.mux.report.case", "2");
		prop_set("vendor.ril.muxreport", "1");
	}
	exit(0);
}

static void modem_exception_exit(void)
{
	LOGE("modem is in exception, leaving (%s)", strerror(errno));
	prop_set("vendor.ril.mux.ee.md1", "1");
	close_all_ptys();
	close_serial();
	exit(0);
}

static int serial_wait(int64_t deadline)
{
	for (;;) {
		struct pollfd p = { .fd = serial_fd, .events = POLLIN };
		int64_t left = deadline - now_ms();
		int r;

		if (term_signal)
			return -1;
		if (left <= 0)
			return 0;
		r = poll(&p, 1, (int)left);
		if (r > 0)
			return 1;
		if (r < 0 && errno != EINTR) {
			LOGE("poll %s: %s", serial_path, strerror(errno));
			return -1;
		}
	}
}

static void log_text(const char *dir, const char *p, size_t n)
{
	char out[256];
	size_t o = 0;

	for (size_t i = 0; i < n && o + 5 < sizeof(out); i++) {
		unsigned char ch = (unsigned char)p[i];

		if (ch == '\r')
			o += (size_t)snprintf(out + o, sizeof(out) - o, "\\r");
		else if (ch == '\n')
			o += (size_t)snprintf(out + o, sizeof(out) - o, "\\n");
		else if (ch >= 0x20 && ch < 0x7F)
			out[o++] = (char)ch;
		else
			o += (size_t)snprintf(out + o, sizeof(out) - o, "\\x%02x", ch);
	}
	out[o] = 0;
	LOGI("%s %s%s", dir, out, o + 5 >= sizeof(out) ? "..." : "");
}

static int chat_read(void)
{
	ssize_t n;

	if (chat_len >= sizeof(chat_buf) - 1) {
		memmove(chat_buf, chat_buf + sizeof(chat_buf) / 2, chat_len - sizeof(chat_buf) / 2);
		chat_len -= sizeof(chat_buf) / 2;
	}
	n = read(serial_fd, chat_buf + chat_len, sizeof(chat_buf) - 1 - chat_len);
	if (n < 0 && (errno == EAGAIN || errno == EINTR))
		return 0;
	if (n <= 0) {
		LOGE("read %s: %s", serial_path, n ? strerror(errno) : "end of file");
		return -1;
	}
	chat_len += (size_t)n;
	chat_buf[chat_len] = 0;
	for (size_t i = 0; i < chat_len; i++)
		if (!chat_buf[i])
			chat_buf[i] = ' ';
	return 0;
}

static void chat_clear(void)
{
	chat_len = 0;
	chat_buf[0] = 0;
}

static void chat_log(void)
{
	log_text("<", chat_buf, chat_len);
	chat_clear();
}

static void wait_modem_ready(int ms)
{
	int64_t deadline = now_ms() + ms;

	LOGI("waiting up to %d s for +EIND: 128 on %s", ms / 1000, serial_path);
	while (!strstr(chat_buf, "+EIND: 128")) {
		if (serial_wait(deadline) <= 0 || chat_read()) {
			chat_log();
			LOGW("no +EIND: 128, continuing");
			return;
		}
	}
	chat_log();
}

static int chat_write(const char *cmd)
{
	char line[128];
	int len = snprintf(line, sizeof(line), "%s\r", cmd);
	int off = 0;
	int64_t deadline = now_ms() + CHAT_TIMEOUT_MS;

	log_text(">", line, (size_t)len);
	while (off < len) {
		ssize_t n = write(serial_fd, line + off, (size_t)(len - off));

		if (n > 0) {
			off += (int)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == ETXTBSY || errno == ENODEV))
			modem_exception_exit();
		if (n < 0 && (errno == EAGAIN || errno == EBUSY) && now_ms() < deadline) {
			poll(NULL, 0, TX_RETRY_MS);
			continue;
		}
		LOGE("write %s to %s: %s", cmd, serial_path, n < 0 ? strerror(errno) : "nothing written");
		return -1;
	}
	return 0;
}

static int chat(const char *cmd)
{
	chat_clear();
	if (chat_write(cmd))
		return -1;
	for (;;) {
		int r = serial_wait(now_ms() + CHAT_TIMEOUT_MS);

		if (r <= 0) {
			chat_log();
			LOGE("%s: %s", cmd, r ? "interrupted" : "no answer");
			return -1;
		}
		if (chat_read())
			return -1;
		if (strstr(chat_buf, "OK")) {
			chat_log();
			return 0;
		}
		if (strstr(chat_buf, "ERROR")) {
			chat_log();
			LOGE("%s: ERROR", cmd);
			return -1;
		}
	}
}

static int chat_cmux(const char *cmd)
{
	bool ready = false;

	chat_clear();
	if (chat_write(cmd))
		return -1;
	for (;;) {
		int r = serial_wait(now_ms() + CHAT_TIMEOUT_MS);

		if (r <= 0) {
			chat_log();
			LOGE("%s: %s", cmd, r ? "interrupted" : "no answer");
			return -1;
		}
		if (chat_read())
			return -1;
		if (strstr(chat_buf, "OK")) {
			chat_log();
			if (!ready)
				wait_ms(1000);
			return 0;
		}
		if (strstr(chat_buf, "+CMUX: READY")) {
			ready = true;
			chat_log();
			continue;
		}
		if (strstr(chat_buf, "ERROR")) {
			chat_log();
			LOGE("%s: ERROR", cmd);
			return -1;
		}
	}
}

static void send_close_down(void)
{
	static const uint8_t cld[2] = { CMD_CLD | CMD_CR, 1 };

	tx_frame(0, CTRL_UIH, cld, sizeof(cld));
}

static int open_serial(void)
{
	serial_fd = open(serial_path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	if (serial_fd < 0) {
		LOGE("open %s: %s", serial_path, strerror(errno));
		return -1;
	}
	return 0;
}

static int modem_handshake(void)
{
	char cmd[64];

	wait_modem_ready(user_build() ? EIND_WAIT_USER_MS : EIND_WAIT_MS);
	if (chat("AT") && chat("AT"))
		return -1;
	if (chat("ATZ") || chat("ATE0"))
		return -1;
	if (pin >= 0) {
		snprintf(cmd, sizeof(cmd), "AT+CPIN=%04d", pin);
		if (chat(cmd))
			return -1;
	}
	snprintf(cmd, sizeof(cmd), "AT+CMUX=0,0,%d,%d", baud_index, frame_size);
	LOGI("starting mux mode: %s", cmd);
	return chat_cmux(cmd);
}

static void drop_root(void)
{
	if (getuid() != 0)
		return;
	prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);
	if (setuid(AID_RADIO))
		LOGE("setuid radio: %s", strerror(errno));
	else
		LOGI("switched to user radio");
}

static void setup_complete(void)
{
	int refused = 0;

	for (int i = 0; i < nchans; i++)
		refused += chans[i].refused;
	setup_done = true;
	state = ST_RUN;
	LOGI("mux channel setup finished: %d channels, %d refused by the modem", nchans, refused);
	if (ril_started)
		return;
	ril_started = true;
	prop_set("vendor.ril.mux.start", "1");
	prop_set("vendor.ril.mtk", "1");
	LOGI("vendor.ril.mtk=1, the RIL may start");
	drop_root();
}

static void resolve(void)
{
	if (unresolved > 0 && !--unresolved && !setup_done)
		setup_complete();
}

static int mux_open(void)
{
	setup_done = false;
	state = ST_SETUP;
	unresolved = nchans + 1;
	setup_deadline = now_ms() + SETUP_TIMEOUT_MS;
	ctl.sabm_sent = true;
	tx_frame(0, CTRL_SABM | CTRL_PF, NULL, 0);
	for (int i = 0; i < nchans; i++) {
		if (chan_open_pty(&chans[i]))
			return -1;
		chans[i].sabm_sent = true;
		tx_frame(chans[i].dlci, CTRL_SABM | CTRL_PF, NULL, 0);
	}
	return tx_flush();
}

static void chan_reopen(struct chan *c)
{
	bool had_fc = c->local_fc;

	LOGI("%s: the application closed %s, recreating it", c->name, c->link);
	chan_close_pty(c);
	if (had_fc)
		send_msc(c, false);
	if (chan_open_pty(c))
		report_and_exit("cannot recreate a channel pty");
}

static void chan_drain(struct chan *c)
{
	while (c->pending_len) {
		ssize_t n = write(c->fd, c->pending, c->pending_len);

		if (n > 0) {
			memmove(c->pending, c->pending + n, c->pending_len - (size_t)n);
			c->pending_len -= (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && errno == EAGAIN)
			return;
		chan_reopen(c);
		return;
	}
	if (c->local_fc) {
		c->local_fc = false;
		send_msc(c, false);
		LOGD("%s: drained, flow control released", c->name);
	}
}

static void deliver(struct chan *c, const uint8_t *data, size_t len)
{
	ssize_t n = 0;

	if (c->fd < 0) {
		LOGW("%s: %zu bytes for a channel without a pty, dropped", c->name, len);
		return;
	}
	if (c->pending_len) {
		if (c->pending_len + len > PENDING_CAP) {
			LOGE("%s: application is not reading, dropping %zu bytes", c->name, len);
			return;
		}
		memcpy(c->pending + c->pending_len, data, len);
		c->pending_len += len;
		return;
	}
	while (len) {
		n = write(c->fd, data, len);
		if (n > 0) {
			data += n;
			len -= (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		break;
	}
	if (!len)
		return;
	if (n < 0 && errno != EAGAIN) {
		LOGW("%s: write %s: %s", c->name, c->slave, strerror(errno));
		chan_reopen(c);
		return;
	}
	memcpy(c->pending, data, len);
	c->pending_len = len;
	if (!c->local_fc) {
		c->local_fc = true;
		send_msc(c, true);
		LOGD("%s: application is slow, asking the modem to pause", c->name);
	}
}

static void reply_echo(const uint8_t *data, size_t len)
{
	uint8_t r[MAX_FRAME];

	memcpy(r, data, len);
	r[0] &= (uint8_t)~CMD_CR;
	tx_frame(0, CTRL_UIH, r, len);
}

static void apply_msc(uint8_t dlci, uint8_t signals)
{
	struct chan *c = dlci < MAX_DLCI ? by_dlci[dlci] : NULL;
	bool fc = signals & MSC_FC;

	if (!c || !c->open)
		return;
	if (fc != c->peer_fc)
		LOGD("%s: modem %s", c->name, fc ? "paused the channel" : "resumed the channel");
	c->peer_fc = fc;
}

static void control_command(const uint8_t *data, size_t len)
{
	size_t tl = 0;
	size_t i;
	uint8_t type;

	while (tl < len && !(data[tl] & 1))
		tl++;
	if (tl >= len) {
		LOGW("malformed control message of %zu bytes", len);
		return;
	}
	tl++;
	type = data[0];
	i = tl;
	while (i < len && !(data[i++] & 1))
		;
	if (!(type & CMD_CR)) {
		if ((type & ~CMD_CR) == CMD_NSC)
			LOGW("modem does not support a command we sent");
		else
			LOGD("modem acknowledged command 0x%02x", type);
		return;
	}
	switch (type & ~CMD_CR) {
	case CMD_TEST:
	case CMD_PSC:
	case CMD_PN:
		reply_echo(data, len);
		break;
	case CMD_CLD:
		LOGI("modem closed the multiplexer");
		reply_echo(data, len);
		state = ST_PEER_CLOSING;
		break;
	case CMD_MSC:
		if (len - i >= 2)
			apply_msc(data[i] >> 2, data[i + 1]);
		else
			LOGW("modem status command without a channel and signals");
		reply_echo(data, len);
		break;
	default: {
		uint8_t r[2 + 8];

		if (tl > 8)
			tl = 8;
		LOGW("unsupported control command 0x%02x", type);
		r[0] = CMD_NSC;
		r[1] = (uint8_t)(tl << 1 | 1);
		memcpy(r + 2, data, tl);
		tx_frame(0, CTRL_UIH, r, 2 + tl);
		break;
	}
	}
}

static void on_ua(struct chan *c)
{
	if (c->open) {
		if (c->disc_sent) {
			c->disc_sent = false;
			c->open = false;
			LOGD("%s: closed", c->name);
		}
		return;
	}
	if (c->sabm_sent) {
		c->sabm_sent = false;
		c->open = true;
		LOGD("%s: dlci %u open", c->name, c->dlci);
		resolve();
		return;
	}
	c->disc_sent = false;
}

static void on_dm(struct chan *c)
{
	if (c->open) {
		c->open = false;
		c->disc_sent = false;
		LOGI("%s: modem closed dlci %u", c->name, c->dlci);
		if (c == &ctl)
			state = ST_PEER_CLOSING;
		else
			chan_close_pty(c);
		return;
	}
	if (c == &ctl) {
		LOGE("modem refused the control channel");
		state = ST_CLOSING;
		return;
	}
	if (!c->sabm_sent)
		return;
	c->sabm_sent = false;
	c->refused = true;
	LOGE("%s: modem refused dlci %u", c->name, c->dlci);
	if (c->atci)
		prop_set("vendor.lackof.atci.channel", "1");
	resolve();
}

static void on_disc(uint8_t dlci, struct chan *c)
{
	if (!c || !c->open) {
		tx_frame(dlci, CTRL_DM | CTRL_PF, NULL, 0);
		return;
	}
	c->open = false;
	tx_frame(dlci, CTRL_UA | CTRL_PF, NULL, 0);
	if (c == &ctl) {
		LOGI("modem disconnected the control channel");
		state = ST_PEER_CLOSING;
	} else {
		LOGI("%s: modem disconnected dlci %u", c->name, dlci);
		chan_close_pty(c);
	}
}

static void handle_frame(uint8_t dlci, uint8_t ctrl, const uint8_t *data, size_t len)
{
	struct chan *c = by_dlci[dlci];

	LOGD("rx dlci %u ctrl 0x%02x len %zu", dlci, ctrl, len);
	switch (ctrl & ~CTRL_PF) {
	case CTRL_UIH:
	case CTRL_UI:
		if (ctrl & CTRL_PF)
			pf_echo = true;
		if (!dlci)
			control_command(data, len);
		else if (c)
			deliver(c, data, len);
		else
			LOGW("%zu bytes on unknown dlci %u, dropped", len, dlci);
		break;
	case CTRL_UA:
		if (c)
			on_ua(c);
		break;
	case CTRL_DM:
		if (c)
			on_dm(c);
		break;
	case CTRL_SABM:
		if (c)
			c->open = true;
		tx_frame(dlci, CTRL_UA | CTRL_PF, NULL, 0);
		break;
	case CTRL_DISC:
		on_disc(dlci, c);
		break;
	default:
		LOGW("frame with control 0x%02x on dlci %u ignored", ctrl, dlci);
		break;
	}
}

static void rx_parse(void)
{
	size_t i = 0;

	for (;;) {
		size_t p, hl, len;
		uint8_t ctrl, crc;

		while (i < rx_len && rxb[i] != MUX_FLAG)
			i++;
		while (i + 1 < rx_len && rxb[i + 1] == MUX_FLAG)
			i++;
		if (rx_len - i < 4)
			break;
		p = i + 1;
		ctrl = rxb[p + 1];
		len = rxb[p + 2] >> 1;
		hl = 3;
		if (!(rxb[p + 2] & 1)) {
			if (rx_len - p < 4)
				break;
			len |= (size_t)rxb[p + 3] << 7;
			hl = 4;
		}
		if (len > (size_t)frame_size) {
			LOGE("dropping a frame on dlci %u: length %zu over %d", rxb[p] >> 2, len, frame_size);
			i = p;
			continue;
		}
		if (rx_len - p < hl + len + 2)
			break;
		crc = crc_update(0xFF, rxb + p, hl);
		if ((ctrl & ~CTRL_PF) == CTRL_UI)
			crc = crc_update(crc, rxb + p + hl, len);
		if ((uint8_t)(crc ^ rxb[p + hl + len]) != 0xFF) {
			LOGE("dropping a frame on dlci %u: FCS mismatch", rxb[p] >> 2);
			i = p;
			continue;
		}
		if (rxb[p + hl + len + 1] != MUX_FLAG) {
			LOGE("dropping a frame on dlci %u: no closing flag", rxb[p] >> 2);
			i = p;
			continue;
		}
		handle_frame(rxb[p] >> 2, ctrl, rxb + p + hl, len);
		i = p + hl + len + 1;
	}
	memmove(rxb, rxb + i, rx_len - i);
	rx_len -= i;
}

static ssize_t serial_read(void)
{
	ssize_t total = 0;

	for (;;) {
		ssize_t n = read(serial_fd, rxb + rx_len, sizeof(rxb) - rx_len);

		if (n > 0) {
			rx_len += (size_t)n;
			total += n;
			rx_parse();
			if (rx_len == sizeof(rxb)) {
				LOGE("%zu bytes without a complete frame, dropped", rx_len);
				rx_len = 0;
			}
			continue;
		}
		if (n < 0 && errno == EAGAIN)
			return total;
		if (n < 0 && errno == EINTR)
			continue;
		LOGE("read %s: %s", serial_path, n ? strerror(errno) : "end of file");
		return -1;
	}
}

static void chan_read(struct chan *c)
{
	uint8_t buf[PTY_CHUNK];
	ssize_t n = read(c->fd, buf, sizeof(buf));

	if (n < 0 && (errno == EAGAIN || errno == EINTR))
		return;
	if (n <= 0) {
		chan_reopen(c);
		return;
	}
	if (c->refused) {
		if (!c->refused_logged)
			LOGW("%s: the modem refused this channel, discarding what the application writes", c->name);
		c->refused_logged = true;
		return;
	}
	for (ssize_t off = 0; off < n; off += frame_size) {
		size_t chunk = (size_t)(n - off) < (size_t)frame_size ? (size_t)(n - off) : (size_t)frame_size;

		tx_frame(c->dlci, CTRL_UIH, buf + off, chunk);
	}
}

static bool can_take_pty_data(void)
{
	return txq_room() >= PTY_CHUNK + (PTY_CHUNK / (size_t)frame_size + 1) * FRAME_OVERHEAD;
}

static void close_mux(void)
{
	int64_t deadline = now_ms() + CLOSE_WAIT_MS;
	bool waiting = false;

	for (int i = 0; i < nchans; i++) {
		if (!chans[i].open)
			continue;
		chans[i].disc_sent = true;
		waiting = true;
		tx_frame(chans[i].dlci, CTRL_DISC | CTRL_PF, NULL, 0);
	}
	tx_drain(deadline);
	while (waiting && !term_signal) {
		waiting = false;
		for (int i = 0; i < nchans; i++)
			waiting |= chans[i].disc_sent;
		if (!waiting || serial_wait(deadline) <= 0 || serial_read() < 0)
			break;
	}
	send_close_down();
	tx_drain(now_ms() + EXIT_FLUSH_MS);
}

static void graceful_exit(void)
{
	LOGI("signal %d, leaving", (int)term_signal);
	close_all_ptys();
	close_serial();
	exit(0);
}

static void run(void)
{
	struct pollfd pfd[1 + MAX_CHANS];
	struct chan *pc[1 + MAX_CHANS];

	for (;;) {
		int n = 0;
		int timeout = -1;
		int r;

		if (term_signal)
			graceful_exit();
		if (state == ST_CLOSING || state == ST_PEER_CLOSING)
			return;
		pfd[n].fd = serial_fd;
		pfd[n].events = POLLIN | (txq_len && !tx_blocked ? POLLOUT : 0);
		pc[n++] = NULL;
		for (int i = 0; i < nchans; i++) {
			struct chan *c = &chans[i];

			if (c->fd < 0)
				continue;
			pfd[n].fd = c->fd;
			pfd[n].events = 0;
			if (setup_done && (c->open || c->refused) && !c->peer_fc && can_take_pty_data())
				pfd[n].events |= POLLIN;
			if (c->pending_len)
				pfd[n].events |= POLLOUT;
			pc[n++] = c;
		}
		if (txq_len)
			timeout = TX_RETRY_MS;
		if (!setup_done) {
			int64_t left = setup_deadline - now_ms();

			if (left <= 0)
				report_and_exit("channel setup did not finish");
			if (timeout < 0 || left < timeout)
				timeout = (int)left;
		}
		r = poll(pfd, (nfds_t)n, timeout);
		if (r < 0 && errno != EINTR)
			report_and_exit("poll failed");
		if (r > 0 && pfd[0].revents & POLLNVAL)
			report_and_exit("the modem port is not open");
		if (r > 0 && pfd[0].revents & (POLLIN | POLLERR | POLLHUP)) {
			ssize_t got = serial_read();

			if (got < 0)
				report_and_exit("reading the modem port failed");
			if (!got && !(pfd[0].revents & POLLIN))
				report_and_exit("the modem port reports an error");
		}
		for (int k = 1; r > 0 && k < n; k++) {
			struct chan *c = pc[k];
			short ev = pfd[k].revents;

			if (c->fd != pfd[k].fd || !ev)
				continue;
			if (ev & POLLOUT)
				chan_drain(c);
			if (c->fd != pfd[k].fd)
				continue;
			if (ev & POLLIN)
				chan_read(c);
			else if (ev & (POLLHUP | POLLERR | POLLNVAL))
				chan_reopen(c);
		}
		if (txq_len && tx_flush())
			report_and_exit("writing the modem port failed");
	}
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [-s port] [-f frame_size] [-n ports] [-m basic] [-b baud_index] [-P pin] [-r link_dir] [-v]\n"
		"GSM 07.10 basic-mode multiplexer for the MediaTek modem AT port. Channels and their DLCIs follow the\n"
		"SIM count (ro.boot.opt_sim_count, persist.radio.multisim.config); -n is accepted and ignored.\n"
		"After every channel is up it sets vendor.ril.mux.start=1 and vendor.ril.mtk=1, which starts the RIL.\n",
		argv0);
	exit(2);
}

int main(int argc, char **argv)
{
	struct sigaction sa;
	char v[PROP_VALUE_MAX];
	int opt;

	while ((opt = getopt(argc, argv, "s:f:n:m:b:P:r:vdo")) != -1) {
		switch (opt) {
		case 's':
			serial_path = optarg;
			break;
		case 'f':
			frame_size = atoi(optarg);
			break;
		case 'n':
		case 'd':
		case 'o':
			break;
		case 'm':
			if (strcmp(optarg, "basic")) {
				fprintf(stderr, "only -m basic is supported\n");
				return 2;
			}
			break;
		case 'b':
			baud_index = atoi(optarg);
			break;
		case 'P':
			pin = atoi(optarg);
			break;
		case 'r':
			link_dir = optarg;
			break;
		case 'v':
			verbose = true;
			break;
		default:
			usage(argv[0]);
		}
	}
	if (frame_size < 1 || frame_size > MAX_FRAME)
		usage(argv[0]);

	prop_get("ro.vendor.mtk_mipc_support", v, "0");
	if (v[0] == '1') {
		LOGI("MIPC is in use, the AT multiplexer is not needed");
		return 0;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);
	umask(0);

	prop_set("vendor.ril.muxreport.run", "0");
	prop_set("vendor.ril.mux.ee.md1", "0");
	crc_init();
	if (build_channels(sim_count())) {
		LOGE("out of memory");
		return 1;
	}
	LOGI("%s on %s: %d channels, frame size %d", argv[0], serial_path, nchans, frame_size);

	for (;;) {
		if (open_serial())
			report_and_exit("cannot open the modem port");
		if (modem_handshake()) {
			if (term_signal)
				graceful_exit();
			report_and_exit("the modem did not enter mux mode");
		}
		if (mux_open())
			report_and_exit("cannot set up the channels");
		run();
		if (state == ST_CLOSING)
			close_mux();
		else
			tx_drain(now_ms() + EXIT_FLUSH_MS);
		close_all_ptys();
		close_serial();
		LOGI("multiplexer closed, starting again in %d s", RESTART_DELAY_MS / 1000);
		wait_ms(RESTART_DELAY_MS);
		if (term_signal)
			graceful_exit();
	}
}
