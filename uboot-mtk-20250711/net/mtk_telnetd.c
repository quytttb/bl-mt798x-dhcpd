// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * Telnet server for MediaTek web failsafe (RFC 854).
 *
 * Architecture (single-file, layered):
 *   1. Constants & data structures
 *   2. Protocol layer   — IAC negotiation, greeting banner
 *   3. Editing engine    — cursor movement, insert/delete, redraw, echo
 *   4. History           — ring-buffer command recall
 *   5. Command execution — console capture, run_command, output delivery
 *   6. Input processor   — per-byte dispatch (IAC → edit → exec)
 *   7. TCP callback      — session lifecycle (new/data/sent/closed)
 *   8. Public API        — mtk_telnetd_start/stop
 */

#include <command.h>
#include <console.h>
#include <env.h>
#include <errno.h>
#include <malloc.h>
#include <membuf.h>
#include <net.h>
#include <net/mtk_tcp.h>
#include <net/mtk_telnetd.h>
#include <version_string.h>
#include <vsprintf.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * Implemented in failsafe/failsafe_core.c.  That directory is only built
 * when CONFIG_WEBUI_FAILSAFE is enabled, so a weak fallback keeps telnetd
 * linkable on its own.
 */
void __weak failsafe_notify_network_cmd_done(void)
{
}

/* ================================================================== */
/*  1. Constants                                                       */
/* ================================================================== */

/* --- Telnet protocol (RFC 854) --- */
#define IAC		255
#define WILL		251
#define WONT		252
#define DO		253
#define DONT		254
#define SB		250
#define SE		240

#define TELOPT_ECHO	1
#define TELOPT_SGA	3
#define TELOPT_NAWS	31

/* --- Buffer sizes --- */
#define TELNETD_INBUF_SIZE	2048
#define TELNETD_OUTBUF_SIZE	8192
#define TELNETD_CMD_MAX		512
#define TELNETD_EDIT_BUF_SIZE	512
#define TELNETD_HIST_MAX	16

/*
 * Maximum number of captured console bytes pushed to the client per
 * MTK_TCP_CB_POLL tick.  Kept well below the TCP send window so a tick
 * never blocks on retransmission while a transfer is in progress.
 */
#define TELNETD_STREAM_CHUNK	1024

/* --- Fallback defaults --- */
#ifndef TELNET_GIT_HASH
#define TELNET_GIT_HASH "unknown"
#endif
#ifndef TELNET_GIT_DIRTY
#define TELNET_GIT_DIRTY 0
#endif

/* --- Cursor movement direction --- */
enum telnetd_cursor_dir {
	CURSOR_LEFT,
	CURSOR_RIGHT,
	CURSOR_HOME,
	CURSOR_END,
};

/* ================================================================== */
/*  2. Data structures                                                 */
/* ================================================================== */

enum telnetd_state {
	TELNETD_S_IDLE = 0,
	TELNETD_S_RESPONDING,
};

/*
 * One queued output chunk.
 *
 * The TCP layer accepts a single buffer per connection at a time, so
 * everything we produce goes through this FIFO: command output, streamed
 * progress and the trailing prompt.  Ordering is therefore preserved even
 * when a chunk has to wait for the previous one to be ACKed.
 */
struct telnetd_obuf {
	struct telnetd_obuf *next;
	char *data;
	u32   len;
};

struct telnetd_pdata {
	/* Connection state */
	enum telnetd_state state;
	bool executing;		 /* guard against re-entrant execute() */
	bool streaming;		 /* executing + private capture buffer installed */
	bool dead;		 /* client disconnected while a command ran */
	bool skip_lf;		 /* LF-after-CR suppression */

	/* Input buffer (raw TCP bytes) */
	char inbuf[TELNETD_INBUF_SIZE];
	u32  inbuf_size;

	/* Command line */
	char cmdbuf[TELNETD_CMD_MAX];
	u32  cmdlen;
	u32  cmdpos;		 /* cursor position, 0..cmdlen */

	/* Out-of-band output (malloc'd, deferred send) */
	struct telnetd_obuf *oq_head;
	struct telnetd_obuf *oq_tail;
	char *outbuf;		 /* buffer currently owned by the TCP layer */
	u32   outbuf_len;

	/* In-band edit responses (echo, backspace, cursor, IAC) */
	char edit_outbuf[TELNETD_EDIT_BUF_SIZE];
	u32  edit_outbuf_len;

	/* History ring buffer */
	char history[TELNETD_HIST_MAX][TELNETD_CMD_MAX];
	u32  hist_count;
	u32  hist_head;
	s32  hist_cur;		 /* -1 = not navigating */
	char hist_saved[TELNETD_CMD_MAX];
};

/* Global instance */
static struct {
	u16  port;
	bool running;
} telnetd_inst;

/*
 * pdata of a session that went away while a command was still executing.
 *
 * telnetd_execute() is several frames up the stack and still dereferences
 * the pointer, so it cannot be freed from the CLOSED callback.  It is
 * parked here instead and released on the next telnetd_callback() entry.
 * Only one command can run at a time, so a single slot is sufficient.
 */
static struct telnetd_pdata *telnetd_orphan;

/*
 * Deferred command execution.
 *
 * run_command() MUST NOT run from inside an eth_rx() → TCP callback: the
 * failsafe poll loop's outer eth_rx() frame is still suspended below and
 * still owns an RX-ring descriptor (free_pkt runs only after the packet
 * handler returns).  A command that enters net_loop() (tftp, ping, ...)
 * would then drive eth_rx() recursively over the same DMA ring, corrupting
 * descriptor ownership — which surfaces as a hard hang when such a command
 * is aborted mid-transfer (eth_halt()/eth_init() reset the ring).
 *
 * Commands are therefore queued here and executed from the failsafe main
 * poll loop via mtk_telnetd_poll(), i.e. outside any eth_rx() frame.
 * Only one command may be queued or running at a time.
 */
static struct {
	struct telnetd_pdata *pdata;
	const void *conn;
	char cmd[TELNETD_CMD_MAX];
	bool pending;
	bool active;
} telnetd_exec_req;

/*
 * The web console is executing a command (defined in
 * failsafe/modules/console.c).  Weak so telnetd links without failsafe.
 */
bool __weak failsafe_console_is_busy(void)
{
	return false;
}

static void telnetd_reap_orphan(void)
{
	free(telnetd_orphan);
	telnetd_orphan = NULL;
}

/* ================================================================== */
/*  3. Protocol layer                                                  */
/* ================================================================== */

static const char *telnetd_get_prompt(void)
{
	const char *p = env_get("prompt");
	if (p && p[0])
		return p;
#ifdef CONFIG_SYS_PROMPT
	return CONFIG_SYS_PROMPT;
#else
	return "MTK> ";
#endif
}

static int telnetd_ensure_recording(void)
{
	int ret;

	if (!gd)
		return -ENODEV;
	if (!gd->console_out.start) {
		ret = console_record_init();
		if (ret)
			return ret;
	}
	gd->flags |= GD_FLG_RECORD;
	return 0;
}

/*
 * Normalize LF → CRLF for telnet clients.
 * Allocates up to len*2+1 bytes; caller must free.
 */
static char *telnetd_normalize_output(const char *src, size_t len,
				      size_t *out_len)
{
	char *dst;
	size_t i, di = 0;
	bool last_cr = false;

	if (!src || !len)
		return NULL;

	dst = malloc(len * 2 + 1);
	if (!dst)
		return NULL;

	for (i = 0; i < len; i++) {
		unsigned char c = src[i];

		if (c == '\n') {
			if (!last_cr)
				dst[di++] = '\r';
			dst[di++] = '\n';
			last_cr = false;
		} else {
			last_cr = (c == '\r');
			dst[di++] = c;
		}
	}
	if (out_len)
		*out_len = di;
	return dst;
}

/* --- Telnet IAC parsing --- */

static u32 telnetd_iac_skip(const char *buf, u32 buflen)
{
	unsigned char cmd = buf[1];

	if (cmd == IAC)
		return 2;		/* literal 0xff */

	if (cmd == SB) {		/* sub-negotiation */
		u32 pos = 2;
		while (pos + 1 < buflen) {
			if ((unsigned char)buf[pos] == IAC &&
			    (unsigned char)buf[pos + 1] == SE)
				return pos + 2;
			pos++;
		}
		return 0;		/* incomplete */
	}

	if (cmd >= 240 && cmd <= 249)
		return 2;		/* NOP, AYT, etc. */

	if ((cmd == WILL || cmd == WONT || cmd == DO || cmd == DONT)) {
		if (buflen >= 3)
			return 3;
		return 0;		/* incomplete */
	}

	return 2;			/* unknown 2-byte */
}

static void telnetd_process_iac(struct telnetd_pdata *pdata,
				const char *buf, u32 buflen)
{
	unsigned char cmd = buf[1];
	unsigned char opt = buf[2];
	unsigned char resp = 0;

	if (buflen < 3)
		return;
	if (cmd != WILL && cmd != WONT && cmd != DO && cmd != DONT)
		return;

	switch (cmd) {
	case DO:
		resp = (opt == TELOPT_SGA || opt == TELOPT_ECHO) ? WILL : WONT;
		break;
	case DONT:
		resp = WONT;
		break;
	case WILL:
		resp = (opt == TELOPT_SGA || opt == TELOPT_NAWS) ? DO : DONT;
		break;
	case WONT:
		resp = DONT;
		break;
	}

	if (resp && pdata->edit_outbuf_len + 3 <= TELNETD_EDIT_BUF_SIZE) {
		pdata->edit_outbuf[pdata->edit_outbuf_len++] = IAC;
		pdata->edit_outbuf[pdata->edit_outbuf_len++] = resp;
		pdata->edit_outbuf[pdata->edit_outbuf_len++] = opt;
	}
}

/* --- Greeting banner --- */

static const char telnet_iac_nego[] = {
	IAC, WILL, TELOPT_ECHO,
	IAC, WILL, TELOPT_SGA,
	IAC, DO,   TELOPT_NAWS,
};

static const char telnet_greeting_prefix[] =
	"\r\nU-Boot Telnet Console\r\n";

static const char telnet_fallback_text[] =
	"U-Boot Telnet Console\r\n"
	"Author: Yuzhii0718\r\n\r\n"
	"MTK> ";

static size_t telnetd_build_greeting(char *buf, size_t sz)
{
	const char *hash = TELNET_GIT_HASH;
	const char *variant = NULL;
	const char *prompt = telnetd_get_prompt();
	bool dirty = !!TELNET_GIT_DIRTY;
	size_t off = 0;
	int n;

	if (!buf || sz < 64)
		return 0;
	if (!hash || !hash[0])
		hash = "unknown";
#ifdef CONFIG_WEBUI_FAILSAFE_BUILD_VARIANT
	variant = CONFIG_WEBUI_FAILSAFE_BUILD_VARIANT;
	if (variant && !variant[0])
		variant = NULL;
#endif

	memcpy(buf + off, telnet_iac_nego, sizeof(telnet_iac_nego));
	off += sizeof(telnet_iac_nego);

	memcpy(buf + off, telnet_greeting_prefix,
	       sizeof(telnet_greeting_prefix) - 1);
	off += sizeof(telnet_greeting_prefix) - 1;

	n = snprintf(buf + off, sz - off,
		     "Version: %s\r\nGit Hash: %s%s\r\n%s%s%s\r\n",
		     version_string, hash, dirty ? " (dirty)" : "",
		     variant ? "Build: " : "",
		     variant ? variant : "",
		     variant ? "\r\n" : "");
	if (n < 0 || (size_t)n >= sz - off)
		return 0;
	off += n;

	n = snprintf(buf + off, sz - off,
		     "Author: Yuzhii0718\r\n\r\n%s", prompt);
	if (n < 0 || (size_t)n >= sz - off)
		return 0;
	off += n;

	return off;
}

/* ================================================================== */
/*  4. Editing engine                                                  */
/* ================================================================== */

/* --- Low-level edit_outbuf helpers --- */

static bool edit_append_raw(struct telnetd_pdata *pdata,
			    const char *data, u32 len)
{
	if (pdata->edit_outbuf_len + len > TELNETD_EDIT_BUF_SIZE)
		return false;
	memcpy(pdata->edit_outbuf + pdata->edit_outbuf_len, data, len);
	pdata->edit_outbuf_len += len;
	return true;
}

static void edit_backspace(struct telnetd_pdata *pdata)
{
	edit_append_raw(pdata, "\b \b", 3);
}

static void edit_echo(struct telnetd_pdata *pdata, char c)
{
	if (pdata->edit_outbuf_len < TELNETD_EDIT_BUF_SIZE)
		pdata->edit_outbuf[pdata->edit_outbuf_len++] = c;
}

/* --- Cursor movement --- */

static void edit_cursor(struct telnetd_pdata *pdata,
			enum telnetd_cursor_dir dir)
{
	switch (dir) {
	case CURSOR_LEFT:
		if (pdata->cmdpos > 0) {
			pdata->cmdpos--;
			edit_append_raw(pdata, "\x1b[D", 3);
		}
		break;
	case CURSOR_RIGHT:
		if (pdata->cmdpos < pdata->cmdlen) {
			pdata->cmdpos++;
			edit_append_raw(pdata, "\x1b[C", 3);
		}
		break;
	case CURSOR_HOME:
		while (pdata->cmdpos > 0) {
			pdata->cmdpos--;
			edit_append_raw(pdata, "\x1b[D", 3);
		}
		break;
	case CURSOR_END:
		while (pdata->cmdpos < pdata->cmdlen) {
			pdata->cmdpos++;
			edit_append_raw(pdata, "\x1b[C", 3);
		}
		break;
	}
}

/* --- Redraw tail (after mid-line edit) --- */

static void edit_redraw_tail(struct telnetd_pdata *pdata,
			     u32 from, s32 cursor_ofs)
{
	u32 tail = pdata->cmdlen - from;
	u32 i;

	/* Space for: tail chars + ESC[K (4B) + cursor escape (max 8B) */
	if (pdata->edit_outbuf_len + tail + 4 + 8 > TELNETD_EDIT_BUF_SIZE)
		return;

	/* Reprint chars from 'from' to end */
	for (i = from; i < pdata->cmdlen; i++)
		pdata->edit_outbuf[pdata->edit_outbuf_len++] =
			(unsigned char)pdata->cmdbuf[i];

	/*
	 * Clear to end of line.  When the edited line is shorter than
	 * the original (delete), stale characters beyond the new end
	 * must be erased.  ESC [ K clears from cursor to EOL.
	 */
	pdata->edit_outbuf[pdata->edit_outbuf_len++] = '\x1b';
	pdata->edit_outbuf[pdata->edit_outbuf_len++] = '[';
	pdata->edit_outbuf[pdata->edit_outbuf_len++] = 'K';

	/* Reposition cursor */
	{
		s32 newpos = (s32)from + cursor_ofs;
		s32 back = (s32)pdata->cmdlen - newpos;
		char buf[8];

		if (back > 0 && back <= 999) {
			int n = snprintf(buf, sizeof(buf), "\x1b[%dD",
					 (int)back);
			if (n > 0) {
				memcpy(pdata->edit_outbuf +
				       pdata->edit_outbuf_len, buf,
				       (u32)n);
				pdata->edit_outbuf_len += (u32)n;
			}
		}
	}
}

/* --- Character insertion (end-of-line or mid-line) --- */

static bool edit_putc(struct telnetd_pdata *pdata, char c)
{
	if (pdata->cmdlen >= TELNETD_CMD_MAX - 1)
		return false;

	if (pdata->cmdpos == pdata->cmdlen) {
		/* Append at end */
		pdata->cmdbuf[pdata->cmdlen++] = c;
		pdata->cmdpos++;
		edit_echo(pdata, c);
	} else {
		/* Insert mid-line: shift, insert, redraw from insertion */
		memmove(pdata->cmdbuf + pdata->cmdpos + 1,
			pdata->cmdbuf + pdata->cmdpos,
			pdata->cmdlen - pdata->cmdpos);
		pdata->cmdbuf[pdata->cmdpos] = c;
		pdata->cmdlen++;
		pdata->cmdpos++;
		edit_redraw_tail(pdata, pdata->cmdpos - 1, 1);
	}
	return true;
}

/* --- Character deletion (backspace) --- */

static void edit_del(struct telnetd_pdata *pdata)
{
	if (pdata->cmdpos == 0)
		return;

	if (pdata->cmdpos == pdata->cmdlen) {
		/* Delete at end */
		pdata->cmdlen--;
		pdata->cmdpos--;
		edit_backspace(pdata);
	} else {
		/* Delete mid-line: remove char before cursor, shift, redraw */
		u32 delpos = pdata->cmdpos - 1;

		memmove(pdata->cmdbuf + delpos,
			pdata->cmdbuf + pdata->cmdpos,
			pdata->cmdlen - pdata->cmdpos);
		pdata->cmdlen--;
		pdata->cmdpos--;
		edit_redraw_tail(pdata, delpos, 0);
	}
}

/* --- Flush accumulated edit bytes to TCP --- */

static void edit_flush(struct mtk_tcp_cb_data *cbd,
		       struct telnetd_pdata *pdata)
{
	if (!pdata->edit_outbuf_len)
		return;

	if (!mtk_tcp_send_data(cbd->conn, pdata->edit_outbuf,
			       pdata->edit_outbuf_len))
		pdata->edit_outbuf_len = 0;
}

/* ================================================================== */
/*  5. History management                                              */
/* ================================================================== */

static void hist_init(struct telnetd_pdata *pdata)
{
	pdata->hist_cur = -1;
}

static u32 hist_idx(struct telnetd_pdata *pdata, s32 n)
{
	return (pdata->hist_head + TELNETD_HIST_MAX - 1 -
		(u32)(pdata->hist_count - 1 - n)) % TELNETD_HIST_MAX;
}

static void hist_save(struct telnetd_pdata *pdata, const char *cmd)
{
	u32 idx;

	if (!cmd[0])
		return;

	idx = pdata->hist_head;
	strncpy(pdata->history[idx], cmd, TELNETD_CMD_MAX - 1);
	pdata->history[idx][TELNETD_CMD_MAX - 1] = '\0';

	pdata->hist_head = (idx + 1) % TELNETD_HIST_MAX;
	if (pdata->hist_count < TELNETD_HIST_MAX)
		pdata->hist_count++;

	pdata->hist_cur = -1;
}

/*
 * Redraw the full line (used when history navigation replaces the
 * entire command text).  Sends \r + ESC[K + prompt + command.
 */
static void hist_redraw_line(struct telnetd_pdata *pdata,
			     const char *prompt)
{
	u32 plen = strlen(prompt);
	u32 total = 4 + plen + pdata->cmdlen;

	if (pdata->edit_outbuf_len + total + 8 > TELNETD_EDIT_BUF_SIZE)
		return;

	pdata->edit_outbuf[pdata->edit_outbuf_len++] = '\r';
	pdata->edit_outbuf[pdata->edit_outbuf_len++] = '\x1b';
	pdata->edit_outbuf[pdata->edit_outbuf_len++] = '[';
	pdata->edit_outbuf[pdata->edit_outbuf_len++] = 'K';
	memcpy(pdata->edit_outbuf + pdata->edit_outbuf_len, prompt, plen);
	pdata->edit_outbuf_len += plen;
	if (pdata->cmdlen) {
		memcpy(pdata->edit_outbuf + pdata->edit_outbuf_len,
		       pdata->cmdbuf, pdata->cmdlen);
		pdata->edit_outbuf_len += pdata->cmdlen;
	}
}

static void hist_prev(struct mtk_tcp_cb_data *cbd,
		      struct telnetd_pdata *pdata)
{
	const char *prompt = telnetd_get_prompt();

	if (pdata->hist_count == 0)
		return;

	if (pdata->hist_cur < 0) {
		/* First press: save current line */
		strncpy(pdata->hist_saved, pdata->cmdbuf,
			TELNETD_CMD_MAX - 1);
		pdata->hist_saved[TELNETD_CMD_MAX - 1] = '\0';
		pdata->hist_cur = (s32)pdata->hist_count - 1;
	} else {
		pdata->hist_cur--;
		if (pdata->hist_cur < 0)
			pdata->hist_cur = (s32)pdata->hist_count - 1;
	}

	{
		u32 idx = hist_idx(pdata, pdata->hist_cur);

		strncpy(pdata->cmdbuf, pdata->history[idx],
			TELNETD_CMD_MAX - 1);
		pdata->cmdbuf[TELNETD_CMD_MAX - 1] = '\0';
	}
	pdata->cmdlen = strlen(pdata->cmdbuf);
	pdata->cmdpos = pdata->cmdlen;
	hist_redraw_line(pdata, prompt);
}

static void hist_next(struct mtk_tcp_cb_data *cbd,
		      struct telnetd_pdata *pdata)
{
	const char *prompt = telnetd_get_prompt();

	if (pdata->hist_cur < 0)
		return;

	pdata->hist_cur++;
	if (pdata->hist_cur >= (s32)pdata->hist_count) {
		/* Past end: restore saved */
		pdata->hist_cur = -1;
		strncpy(pdata->cmdbuf, pdata->hist_saved,
			TELNETD_CMD_MAX - 1);
		pdata->cmdbuf[TELNETD_CMD_MAX - 1] = '\0';
	} else {
		u32 idx = hist_idx(pdata, pdata->hist_cur);

		strncpy(pdata->cmdbuf, pdata->history[idx],
			TELNETD_CMD_MAX - 1);
		pdata->cmdbuf[TELNETD_CMD_MAX - 1] = '\0';
	}
	pdata->cmdlen = strlen(pdata->cmdbuf);
	pdata->cmdpos = pdata->cmdlen;
	hist_redraw_line(pdata, prompt);
}

/* ================================================================== */
/*  6. Command execution                                               */
/* ================================================================== */

/* --- Output FIFO --------------------------------------------------- */

static void telnetd_oq_purge(struct telnetd_pdata *pdata)
{
	struct telnetd_obuf *o;

	while (pdata->oq_head) {
		o = pdata->oq_head;
		pdata->oq_head = o->next;
		free(o->data);
		free(o);
	}
	pdata->oq_tail = NULL;
}

/*
 * Hand the next queued chunk to the TCP layer.
 *
 * At most one chunk may be in flight per connection, so we stop as soon
 * as one has been submitted.  A chunk that cannot be submitted yet stays
 * at the head of the FIFO and is retried from MTK_TCP_CB_DATA_SENT or the
 * next MTK_TCP_CB_POLL tick — no output is ever dropped.
 */
static void telnetd_oq_pump(struct mtk_tcp_cb_data *cbd,
			    struct telnetd_pdata *pdata)
{
	struct telnetd_obuf *o = pdata->oq_head;

	if (!o || pdata->outbuf)
		return;

	if (mtk_tcp_send_data(cbd->conn, o->data, o->len))
		return;		/* previous chunk still in flight */

	pdata->oq_head = o->next;
	if (!pdata->oq_head)
		pdata->oq_tail = NULL;

	/* TCP layer owns o->data now; freed on MTK_TCP_CB_DATA_SENT */
	pdata->outbuf = o->data;
	pdata->outbuf_len = o->len;
	free(o);
	pdata->state = TELNETD_S_RESPONDING;
}

/*
 * Queue a malloc'd buffer for transmission and kick the pump.
 * Ownership of @buf is transferred (also on failure).
 */
static void telnetd_emit(struct mtk_tcp_cb_data *cbd,
			 struct telnetd_pdata *pdata,
			 char *buf, u32 len)
{
	struct telnetd_obuf *o;

	if (!buf || !len) {
		free(buf);
		return;
	}

	o = malloc(sizeof(*o));
	if (!o) {
		free(buf);
		return;
	}

	o->data = buf;
	o->len = len;
	o->next = NULL;

	if (pdata->oq_tail)
		pdata->oq_tail->next = o;
	else
		pdata->oq_head = o;
	pdata->oq_tail = o;

	telnetd_oq_pump(cbd, pdata);
}

/* --- Live streaming ------------------------------------------------ */

/*
 * Ask net_loop() to abort the running network command.
 *
 * ctrlc() only polls the serial console, so a telnet client could never
 * interrupt a transfer.  The request is recorded here and consumed by
 * net_loop() itself (mtk_tcp_abort_pending() in the MTK_TCP block of the
 * main loop).  The loop then takes the exact same exit path as a serial
 * Ctrl+C: net_cleanup_loop() + eth_halt() + -EINTR, all executed at loop
 * level — never inside the eth_rx() → TCP callback chain we are running
 * in, which would corrupt the DMA receive-descriptor state.
 */
static void telnetd_abort_netloop(void)
{
	mtk_tcp_abort_request();
}

/*
 * Look for Ctrl+C in the input buffer while a command is running.
 *
 * telnetd_process_input() (the line editor) is suspended during execution,
 * so incoming bytes sit in the raw input buffer.  Two representations of
 * "Ctrl+C" are recognized:
 *
 *   - a raw ^C byte (0x03): sent by clients that do no telnet processing
 *   - "IAC IP" (0xff 0xf4, Interrupt Process): sent by clients (e.g. the
 *     Windows telnet client) that follow RFC 854 for an interrupt
 *
 * This is called from MTK_TCP_CB_DATA_RCVD as soon as the bytes arrive
 * (and redundantly from the MTK_TCP_CB_POLL tick).
 */
static void telnetd_abort_check(struct mtk_tcp_cb_data *cbd)
{
	struct telnetd_pdata *pdata = cbd->pdata;
	u32 i = 0;
	bool abort = false;

	if (!pdata || !pdata->executing || !pdata->inbuf_size)
		return;

	while (i < pdata->inbuf_size) {
		if (pdata->inbuf[i] == '\x03') {
			abort = true;
			i++;
			continue;
		}

		/*
		 * IAC IP (0xff 0xf4).  Command execution never reaches the
		 * IAC parser (telnetd_process_iac), so the sequence is still
		 * verbatim in the buffer.
		 */
		if ((unsigned char)pdata->inbuf[i] == IAC &&
		    i + 1 < pdata->inbuf_size &&
		    (unsigned char)pdata->inbuf[i + 1] == 244 /* IP */) {
			abort = true;
			i += 2;
			continue;
		}

		i++;
	}

	if (!abort)
		return;

	/* Drop whatever else was typed during the command */
	pdata->inbuf_size = 0;
	pdata->inbuf[0] = '\0';

	/*
	 * Tell the client, then record the abort request.
	 *
	 * The ^C marker goes through the console capture buffer (like the web
	 * console abort does) instead of being pushed to the TCP layer right
	 * here: we are inside the eth_rx() → TCP RX callback of the running
	 * command's own net_loop(), and touching the TX path of that very
	 * connection from within its RX processing is the one thing that is
	 * guaranteed to differ from the (working) web-console abort.  The
	 * marker is streamed out by the regular drain once the command stops,
	 * together with the "\nAbort\n" the net_loop() abort path prints.
	 */
	if (gd && (gd->flags & GD_FLG_RECORD))
		printf("\n^C\n");

	telnetd_abort_netloop();
}

/*
 * Push whatever the running command has printed so far to the client.
 *
 * Called from MTK_TCP_CB_POLL, i.e. from inside the net_loop() of the very
 * command we are waiting for.  Without this the whole output of e.g.
 * "tftp" (the '#' progress marks) would only be flushed after the
 * transfer finished, making the session look hung.
 *
 * MTK_TCP_CB_POLL is only issued when the connection has no chunk in
 * flight, so mtk_tcp_send_data() normally succeeds; if it does not, the
 * chunk stays in the FIFO and is retried on the next tick.
 */
static void telnetd_stream_tick(struct mtk_tcp_cb_data *cbd)
{
	struct telnetd_pdata *pdata = cbd->pdata;
	struct membuf *mb = (struct membuf *)&gd->console_out;
	int avail, got;
	char *raw, *norm;
	size_t norm_len;

	if (!pdata || !pdata->streaming)
		return;

	if (!gd || !(gd->flags & GD_FLG_RECORD) || !mb->start)
		return;

	avail = membuf_avail(mb);
	if (avail <= 0)
		return;
	if (avail > TELNETD_STREAM_CHUNK)
		avail = TELNETD_STREAM_CHUNK;

	raw = malloc(avail);
	if (!raw)
		return;

	got = membuf_get(mb, raw, avail);
	if (got <= 0) {
		free(raw);
		return;
	}

	norm = telnetd_normalize_output(raw, got, &norm_len);
	free(raw);

	telnetd_emit(cbd, pdata, norm, norm_len);
}

/*
 * Execute a U-Boot command on behalf of a telnet client.
 *
 * Captures console output, normalizes line endings to CRLF, and
 * delivers the result back to the client.  While the command runs,
 * MTK_TCP_CB_POLL drains the capture buffer so progress output is
 * streamed live instead of arriving in one burst at the end.
 */
static void telnetd_execute(struct mtk_tcp_cb_data *cbd,
			    const char *cmd)
{
	struct telnetd_pdata *pdata = cbd->pdata;
	const char *prompt = telnetd_get_prompt();
	struct membuf saved_out;
	struct membuf private_out;
	bool use_private = false;
	bool was_net;
	int avail;
	char *raw_out = NULL;

	/* Empty command → just reprint prompt */
	if (!cmd[0]) {
		char *buf = malloc(strlen(prompt) + 3);

		if (buf) {
			buf[0] = '\r'; buf[1] = '\n';
			memcpy(buf + 2, prompt, strlen(prompt));
			telnetd_emit(cbd, pdata, buf,
					      strlen(prompt) + 2);
		}
		return;
	}

	/* Ensure console recording */
	if (telnetd_ensure_recording()) {
		char *err = malloc(64);

		if (err) {
			snprintf(err, 64,
				 "Error: console recording unavailable\r\n");
			telnetd_emit(cbd, pdata, err, strlen(err));
		}
		return;
	}

	/* Set up private console_out to isolate output */
	saved_out = gd->console_out;
	if (!membuf_new(&private_out, TELNETD_OUTBUF_SIZE)) {
		gd->console_out = private_out;
		use_private = true;
	}
	console_record_reset();

	was_net = strstr(cmd, "tftp") || strstr(cmd, "ping") ||
		  strstr(cmd, "dhcp") || strstr(cmd, "bootp") ||
		  strstr(cmd, "nfs")  || strstr(cmd, "rarp") ||
		  strstr(cmd, "wget") || strstr(cmd, "tcp");

	/*
	 * Terminate the line the client has just typed (the client echoes
	 * its own input, we only owe it the newline), then enable live
	 * streaming so the rest arrives as it is produced.
	 */
	{
		char *nl = malloc(2);

		if (nl) {
			nl[0] = '\r'; nl[1] = '\n';
			telnetd_emit(cbd, pdata, nl, 2);
		}
	}

	/*
	 * executing guard: net_loop() calls mtk_tcp_periodic_check()
	 * which can re-enter this callback.  Block re-entry.
	 *
	 * streaming additionally enables telnetd_stream_tick(); it is only
	 * safe when we own a private capture buffer.
	 */
	pdata->executing = true;
	pdata->streaming = use_private;

	/*
	 * Drop any Ctrl+C request made while the command was still queued
	 * (before it started executing) so it cannot abort the fresh run.
	 */
	mtk_tcp_abort_clear();

	run_command(cmd, 0);
	pdata->streaming = false;
	pdata->executing = false;

	/*
	 * A Ctrl+C abort request is normally consumed by the command's own
	 * net_loop().  If the command did not go through net_loop() (or the
	 * request lost the race against loop exit), drop any stale request
	 * so it cannot abort a later, unrelated command.
	 */
	mtk_tcp_abort_clear();

	/*
	 * The client disconnected while the command was running: the
	 * connection (and cbd->conn) is already gone, so stop here and
	 * let telnetd_orphan handle the memory.
	 */
	if (pdata->dead) {
		if (use_private) {
			membuf_dispose(&gd->console_out);
			gd->console_out = saved_out;
		}
		free(raw_out);
		cbd->pdata = NULL;
		return;
	}

	hist_save(pdata, cmd);

	/*
	 * A network command leaves eth halted (net_loop() → eth_halt()).
	 * Let the failsafe poll loop bring it back up: eth_init() must not
	 * be called from inside the eth_rx() → TCP callback chain.
	 */
	if (was_net)
		failsafe_notify_network_cmd_done();

	/* Print trailing prompt into console capture buffer */
	if (prompt[0] != '\n')
		printf("\n%s", prompt);
	else
		printf("%s", prompt);

	/* Read and deliver whatever is left of the captured output */
	avail = membuf_avail(&gd->console_out);
	if (avail > TELNETD_OUTBUF_SIZE)
		avail = TELNETD_OUTBUF_SIZE;

	if (avail > 0) {
		size_t norm_len = 0;
		int got;

		raw_out = malloc(avail);
		if (raw_out) {
			got = membuf_get(&gd->console_out, raw_out, avail);
			{
				char *norm = telnetd_normalize_output(
					raw_out, got, &norm_len);
				if (norm) {
					free(raw_out);
					raw_out = NULL;
					telnetd_emit(cbd, pdata,
						     norm, norm_len);
				} else {
					telnetd_emit(cbd, pdata,
						     raw_out, got);
					raw_out = NULL;
				}
			}
		}
	} else {
		/* No output → send prompt to show completion */
		char *buf = malloc(strlen(prompt) + 1);

		if (buf) {
			memcpy(buf, prompt, strlen(prompt));
			telnetd_emit(cbd, pdata, buf, strlen(prompt));
		}
	}

	if (use_private) {
		membuf_dispose(&gd->console_out);
		gd->console_out = saved_out;
	}
	free(raw_out);
}

/* --- Deferred execution (queue from callback, run from main loop) --- */

/*
 * Queue a command for later execution by mtk_telnetd_poll().
 *
 * Called from the line editor (inside a TCP RX callback).  We must NOT
 * call run_command() here — see telnetd_exec_req above — so we only store
 * the request and block further editing of this session.
 */
static void telnetd_request_execute(struct mtk_tcp_cb_data *cbd,
				    const char *cmd)
{
	struct telnetd_pdata *pdata = cbd->pdata;
	const char *prompt = telnetd_get_prompt();

	/* Empty command → just reprint prompt */
	if (!cmd[0]) {
		char *buf = malloc(strlen(prompt) + 3);

		if (buf) {
			buf[0] = '\r'; buf[1] = '\n';
			memcpy(buf + 2, prompt, strlen(prompt));
			telnetd_emit(cbd, pdata, buf, strlen(prompt) + 2);
		}
		return;
	}

	/* Ensure console recording */
	if (telnetd_ensure_recording()) {
		char *err = malloc(64);

		if (err) {
			snprintf(err, 64,
				 "Error: console recording unavailable\r\n");
			telnetd_emit(cbd, pdata, err, strlen(err));
		}
		return;
	}

	if (pdata->executing || telnetd_exec_req.pending ||
	    telnetd_exec_req.active || failsafe_console_is_busy()) {
		/* Another command is queued or running somewhere */
		char *err = malloc(32);

		if (err) {
			snprintf(err, 32, "\r\nbusy\r\n");
			telnetd_emit(cbd, pdata, err, strlen(err));
		}
		return;
	}

	strncpy(telnetd_exec_req.cmd, cmd, TELNETD_CMD_MAX - 1);
	telnetd_exec_req.cmd[TELNETD_CMD_MAX - 1] = '\0';
	telnetd_exec_req.pdata = pdata;
	telnetd_exec_req.conn = cbd->conn;
	telnetd_exec_req.pending = true;

	/* Suspend this session's line editor until the command finishes */
	pdata->executing = true;
}

bool mtk_telnetd_exec_pending(void)
{
	return telnetd_exec_req.pending;
}

bool mtk_telnetd_exec_active(void)
{
	return telnetd_exec_req.pending || telnetd_exec_req.active;
}

/*
 * Run the queued command.
 *
 * Called from the failsafe main poll loop, OUTSIDE any eth_rx() frame, so
 * a network command (tftp, ...) can enter net_loop() and be aborted there
 * exactly like on the serial console without corrupting the RX ring.
 */
void mtk_telnetd_poll(void)
{
	struct mtk_tcp_cb_data cbd = {};
	struct telnetd_pdata *pdata = telnetd_exec_req.pdata;

	if (!telnetd_exec_req.pending)
		return;

	/* The session may have gone away while the command was queued */
	if (!pdata || pdata->dead) {
		telnetd_exec_req.pending = false;
		telnetd_exec_req.pdata = NULL;
		return;
	}

	cbd.conn = telnetd_exec_req.conn;
	cbd.pdata = pdata;

	telnetd_exec_req.pending = false;
	telnetd_exec_req.active = true;
	telnetd_execute(&cbd, telnetd_exec_req.cmd);
	telnetd_exec_req.active = false;

	/*
	 * telnetd_execute() clears cbd->pdata when the session was closed
	 * during the run.  The pdata itself is parked in telnetd_orphan
	 * and freed on the next callback; never dereference it here.
	 */
	telnetd_exec_req.pdata = NULL;
}

/* ================================================================== */
/*  7. Input processor                                                 */
/* ================================================================== */

/*
 * Reset the command line to empty state and exit history navigation.
 * Single point of truth — replaces 8 scattered reset blocks.
 */
static void line_reset(struct telnetd_pdata *pdata)
{
	pdata->hist_cur = -1;
	pdata->cmdlen = 0;
	pdata->cmdpos = 0;
	pdata->cmdbuf[0] = '\0';
}

/* Clear line for new input (keeps hist_cur reset) */
static void line_clear(struct telnetd_pdata *pdata)
{
	pdata->hist_cur = -1;
	pdata->cmdlen = 0;
	pdata->cmdpos = 0;
	pdata->cmdbuf[0] = '\0';
}

/*
 * Process one raw byte from the input buffer.
 * Returns true if the byte was consumed, false if processing should
 * stop (e.g. we entered RESPONDING state).
 */
static bool input_process_byte(struct mtk_tcp_cb_data *cbd,
			       struct telnetd_pdata *pdata,
			       unsigned char c)
{
	/* ---- Telnet IAC ---- */
	if (c == IAC) {
		/* Handled in the main loop with skip calculation */
		return true;
	}

	/* ---- Line terminators ---- */
	if (c == '\r' || c == '\n') {
		if (c == '\r')
			pdata->skip_lf = true;
		pdata->cmdbuf[pdata->cmdlen] = '\0';
		telnetd_request_execute(cbd, pdata->cmdbuf);
		/*
		 * The client may have disconnected while the command was
		 * queued/running: the executor then releases the session and
		 * clears cbd->pdata.  @pdata is dangling from here on.
		 */
		if (!cbd->pdata)
			return false;
		line_reset(pdata);
		return (pdata->state == TELNETD_S_IDLE);
	}

	/* ---- ANSI CSI (ESC [ ...) ---- */
	if (c == '\x1b')
		return true; /* handled in main loop */

	/* ---- Backspace / DEL ---- */
	if (c == '\b' || c == 0x7f) {
		pdata->hist_cur = -1;
		edit_del(pdata);
		return true;
	}

	/* ---- Ctrl+C: clear line + new prompt ---- */
	if (c == '\x03') {
		const char *prompt = telnetd_get_prompt();
		u32 plen = strlen(prompt);

		line_clear(pdata);
		if (pdata->edit_outbuf_len + 6 + plen <=
		    TELNETD_EDIT_BUF_SIZE) {
			memcpy(pdata->edit_outbuf +
			       pdata->edit_outbuf_len, "^C\r\n", 4);
			pdata->edit_outbuf_len += 4;
			memcpy(pdata->edit_outbuf +
			       pdata->edit_outbuf_len, prompt, plen);
			pdata->edit_outbuf_len += plen;
		}
		return true;
	}

	/* ---- Ctrl+U: clear line ---- */
	if (c == '\x15') {
		/* Backspace-erase each visible char */
		while (pdata->cmdlen > 0 &&
		       pdata->edit_outbuf_len + 3 <=
		       TELNETD_EDIT_BUF_SIZE) {
			pdata->cmdlen--;
			edit_backspace(pdata);
		}
		line_clear(pdata);
		return true;
	}

	/* ---- Ctrl+W: delete word ---- */
	if (c == '\x17') {
		pdata->hist_cur = -1;
		while (pdata->cmdlen > 0 &&
		       pdata->cmdbuf[pdata->cmdlen - 1] == ' ') {
			if (pdata->edit_outbuf_len + 3 >
			    TELNETD_EDIT_BUF_SIZE)
				break;
			pdata->cmdlen--;
			edit_backspace(pdata);
		}
		while (pdata->cmdlen > 0 &&
		       pdata->cmdbuf[pdata->cmdlen - 1] != ' ') {
			if (pdata->edit_outbuf_len + 3 >
			    TELNETD_EDIT_BUF_SIZE)
				break;
			pdata->cmdlen--;
			edit_backspace(pdata);
		}
		pdata->cmdpos = pdata->cmdlen;
		return true;
	}

	/* ---- TAB (no completion) ---- */
	if (c == '\t')
		return true;

	/* ---- Other control chars: ignore ---- */
	if (c < 0x20)
		return true;

	/* ---- Regular printable character ---- */
	pdata->hist_cur = -1;
	edit_putc(pdata, (char)c);
	return true;
}

/*
 * Handle a complete ANSI CSI sequence found at &pdata->inbuf[i].
 * @i:   index of ESC
 * @end: index after the terminator byte (i + seq_len)
 * @term: the terminator character (e.g. 'A', 'D')
 */
static void input_handle_csi(struct mtk_tcp_cb_data *cbd,
			     struct telnetd_pdata *pdata,
			     unsigned char term)
{
	switch (term) {
	case 'D': /* Left */
		pdata->hist_cur = -1;
		edit_cursor(pdata, CURSOR_LEFT);
		break;
	case 'C': /* Right */
		pdata->hist_cur = -1;
		edit_cursor(pdata, CURSOR_RIGHT);
		break;
	case 'A': /* Up */
		hist_prev(cbd, pdata);
		break;
	case 'B': /* Down */
		hist_next(cbd, pdata);
		break;
	case 'H': /* Home (ESC [ H) */
		pdata->hist_cur = -1;
		edit_cursor(pdata, CURSOR_HOME);
		break;
	case 'F': /* End (ESC [ F) */
		pdata->hist_cur = -1;
		edit_cursor(pdata, CURSOR_END);
		break;
	}
}

/*
 * Main input processing loop.
 * Consumes bytes from pdata->inbuf, dispatching each to the
 * appropriate handler (IAC negotiation, ANSI CSI, or line editing).
 */
static void telnetd_process_input(struct mtk_tcp_cb_data *cbd)
{
	struct telnetd_pdata *pdata = cbd->pdata;
	u32 i = 0;

	while (i < pdata->inbuf_size) {
		unsigned char c = pdata->inbuf[i];

		/* LF-after-CR suppression */
		if (pdata->skip_lf) {
			pdata->skip_lf = false;
			if (c == '\0' || c == '\n') {
				i++;
				continue;
			}
		}

		/* --- Telnet IAC --- */
		if (c == IAC) {
			u32 skip;

			if (i + 1 >= pdata->inbuf_size)
				break; /* incomplete */

			skip = telnetd_iac_skip(&pdata->inbuf[i],
					       pdata->inbuf_size - i);
			if (!skip)
				break; /* incomplete sub-negotiation */

			/* IAC IAC → literal 0xff */
			if ((unsigned char)pdata->inbuf[i + 1] == IAC) {
				if (pdata->cmdlen < TELNETD_CMD_MAX - 1)
					pdata->cmdbuf[pdata->cmdlen++] = IAC;
				i += skip;
				continue;
			}

			telnetd_process_iac(pdata, &pdata->inbuf[i], skip);
			i += skip;
			continue;
		}

		/* --- ANSI CSI (ESC [ ... terminator) --- */
		if (c == '\x1b') {
			if (i + 1 < pdata->inbuf_size &&
			    pdata->inbuf[i + 1] == '[') {
				u32 j = i + 2;
				unsigned char term = 0;

				while (j < pdata->inbuf_size) {
					unsigned char t = pdata->inbuf[j];
					if (t >= 0x40 && t <= 0x7e) {
						term = t; j++;
						break;
					}
					if (t < 0x20 || t > 0x2f)
						break;
					j++;
				}

				if (!term)
					break; /* incomplete */

				/* 3-byte CSI: ESC [ X */
				if (j == i + 3)
					input_handle_csi(cbd, pdata, term);

				i = j;
				continue;
			}
			/* Lone ESC — skip */
			i++;
			continue;
		}

		/* --- Dispatch regular byte --- */
		i++;
		if (!input_process_byte(cbd, pdata, c))
			break;
	}

	/* Flush accumulated edit responses */
	if (cbd->pdata)
		edit_flush(cbd, pdata);

	/* Remove consumed bytes */
	if (i > 0 && cbd->pdata) {
		u32 rem = pdata->inbuf_size - i;

		if (rem > 0)
			memmove(pdata->inbuf, pdata->inbuf + i, rem);
		pdata->inbuf_size = rem;
		pdata->inbuf[rem] = '\0';
	}
}

/* ================================================================== */
/*  8. TCP callback (session lifecycle)                                */
/* ================================================================== */

static void telnetd_send_greeting(struct mtk_tcp_cb_data *cbd,
				  struct telnetd_pdata *pdata)
{
	char *greeting = malloc(512);
	size_t len;

	if (greeting) {
		len = telnetd_build_greeting(greeting, 512);
		if (len) {
			telnetd_emit(cbd, pdata, greeting, len);
			return;
		}
		free(greeting);
	}

	/* Fallback: static greeting */
	{
		size_t nego = sizeof(telnet_iac_nego);
		size_t text = sizeof(telnet_fallback_text) - 1;
		char *fb = malloc(nego + text);

		if (fb) {
			memcpy(fb, telnet_iac_nego, nego);
			memcpy(fb + nego, telnet_fallback_text, text);
			telnetd_emit(cbd, pdata, fb, nego + text);
		} else {
			/* Last resort: send separately */
			mtk_tcp_send_data(cbd->conn, telnet_iac_nego,
					 sizeof(telnet_iac_nego));
			mtk_tcp_send_data(cbd->conn, telnet_fallback_text,
					 sizeof(telnet_fallback_text) - 1);
			telnetd_oq_purge(pdata);
			pdata->outbuf = NULL;
			pdata->outbuf_len = 0;
			pdata->state = TELNETD_S_RESPONDING;
		}
	}
}

static void telnetd_callback(struct mtk_tcp_cb_data *cbd)
{
	struct telnetd_pdata *pdata;
	u8 sip[4];

	telnetd_reap_orphan();

	switch (cbd->status) {

	case MTK_TCP_CB_NEW_CONN:
		pdata = calloc(1, sizeof(*pdata));
		if (!pdata) {
			mtk_tcp_close_conn(cbd->conn, 1);
			break;
		}
		hist_init(pdata);
		cbd->pdata = pdata;
		mtk_tcp_conn_set_pdata(cbd->conn, pdata);

		memcpy(sip, &cbd->sip, 4);
		printf("Telnet connection from %d.%d.%d.%d:%d\n",
		       sip[0], sip[1], sip[2], sip[3], ntohs(cbd->sp));
		telnetd_send_greeting(cbd, pdata);
		break;

	case MTK_TCP_CB_DATA_RCVD:
		pdata = cbd->pdata;
		if (!pdata)
			break;

		/* Buffer incoming data */
		if (cbd->datalen) {
			u32 space = TELNETD_INBUF_SIZE - pdata->inbuf_size - 1;
			u32 n = min_t(u32, cbd->datalen, space);

			memcpy(pdata->inbuf + pdata->inbuf_size, cbd->data, n);
			pdata->inbuf_size += n;
			pdata->inbuf[pdata->inbuf_size] = '\0';
			cbd->datalen = 0;
		}

		/*
		 * While a command is running the line editor is suspended;
		 * the only input worth acting on is Ctrl+C.  The bytes only
		 * reach us because the command's own net_loop() is pumping
		 * eth_rx(), so issuing the abort from here takes effect
		 * immediately — no need to wait for a POLL tick.
		 */
		if (pdata->executing) {
			telnetd_abort_check(cbd);
			break;
		}

		/* Process only when idle */
		if (pdata->state == TELNETD_S_IDLE)
			telnetd_process_input(cbd);
		break;

	case MTK_TCP_CB_DATA_SENT:
		pdata = cbd->pdata;
		if (!pdata)
			break;

		if (pdata->state != TELNETD_S_RESPONDING)
			break;

		/* The TCP layer is done with the previous chunk */
		free(pdata->outbuf);
		pdata->outbuf = NULL;
		pdata->outbuf_len = 0;
		pdata->state = TELNETD_S_IDLE;

		/* Submit whatever else is queued (streamed output, prompt) */
		telnetd_oq_pump(cbd, pdata);

		/* Process buffered input that arrived while sending */
		if (pdata->inbuf_size > 0 && !pdata->executing)
			telnetd_process_input(cbd);
		break;

	case MTK_TCP_CB_POLL:
		pdata = cbd->pdata;
		if (!pdata)
			break;

		/*
		 * Runs from net_loop() while a command is executing, so the
		 * client sees progress output as it is produced and can
		 * abort with Ctrl+C.
		 */
		if (pdata->executing) {
			telnetd_abort_check(cbd);
			telnetd_stream_tick(cbd);
			break;
		}

		if (pdata->state == TELNETD_S_IDLE)
			telnetd_oq_pump(cbd, pdata);
		break;

	case MTK_TCP_CB_REMOTE_CLOSED:
	case MTK_TCP_CB_CLOSED:
		pdata = cbd->pdata;
		if (pdata) {
			/*
			 * A connection can go away while a command is still
			 * running (long tftp, flash erase, ...).  Stop all output
			 * immediately and, if telnetd_execute() is still on the
			 * stack, defer the free — see telnetd_orphan.
			 */
			pdata->streaming = false;
			telnetd_oq_purge(pdata);
			free(pdata->outbuf);
			pdata->outbuf = NULL;
			pdata->outbuf_len = 0;

			if (pdata->executing) {
				pdata->dead = true;
				telnetd_orphan = pdata;
			} else {
				free(pdata);
			}
			}
		memcpy(sip, &cbd->sip, 4);
		printf("Telnet connection closed %d.%d.%d.%d:%d\n",
		       sip[0], sip[1], sip[2], sip[3], ntohs(cbd->sp));
		break;

	default:
		break;
	}
}

/* ================================================================== */
/*  9. Public API                                                      */
/* ================================================================== */

u16 mtk_telnetd_env_port(const char *name, u16 def)
{
	const char *ep = env_get(name);
	unsigned long p;

	if (!ep)
		return def;

	p = simple_strtoul(ep, NULL, 10);
	if (p < 1 || p > 65535)
		return def;

	return (u16)p;
}

int mtk_telnetd_start(u16 port)
{
	if (telnetd_inst.running)
		return -EALREADY;

	if (mtk_tcp_listen(htons(port), telnetd_callback))
		return -EIO;

	telnetd_inst.port = port;
	telnetd_inst.running = true;

	printf("Telnet server started on port %d\n", port);
	return 0;
}

void mtk_telnetd_stop(void)
{
	if (!telnetd_inst.running)
		return;

	mtk_tcp_listen_stop(htons(telnetd_inst.port));
	telnetd_inst.running = false;

	printf("Telnet server stopped\n");
}

bool mtk_telnetd_is_running(void)
{
	return telnetd_inst.running;
}

static int do_telnetd(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	if (argc < 2)
		return CMD_RET_USAGE;

	if (!strcmp(argv[1], "start")) {
		u16 port = mtk_telnetd_env_port("telnet_port", 23);

		if (argc > 2) {
			unsigned long p = simple_strtoul(argv[2], NULL, 10);
			if (p >= 1 && p <= 65535)
				port = (u16)p;
		}

		if (mtk_telnetd_start(port))
			printf("Failed to start telnet server\n");
		return CMD_RET_SUCCESS;
	}

	if (!strcmp(argv[1], "stop")) {
		mtk_telnetd_stop();
		return CMD_RET_SUCCESS;
	}

	return CMD_RET_USAGE;
}

U_BOOT_CMD(telnetd, 3, 0, do_telnetd,
	"Control telnet server",
	"start [port] - start telnet server (default port 23, or $telnet_port)\n"
	"telnetd stop - stop telnet server\n\n"
	"Environment:\n"
	"  telnet_port   - default port for telnetd\n"
	"  telnetd_enable - auto-start on failsafe entry\n"
	"                   (set 0 to disable)"
);
