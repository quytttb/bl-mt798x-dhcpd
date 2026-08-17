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

extern void failsafe_notify_network_cmd_done(void);

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

/* --- Fallback defaults --- */
#ifndef WEBUI_FAILSAFE_GIT_HASH
#define WEBUI_FAILSAFE_GIT_HASH "unknown"
#endif
#ifndef WEBUI_FAILSAFE_GIT_DIRTY
#define WEBUI_FAILSAFE_GIT_DIRTY 0
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

struct telnetd_pdata {
	/* Connection state */
	enum telnetd_state state;
	bool executing;		 /* guard against re-entrant execute() */
	bool skip_lf;		 /* LF-after-CR suppression */

	/* Input buffer (raw TCP bytes) */
	char inbuf[TELNETD_INBUF_SIZE];
	u32  inbuf_size;

	/* Command line */
	char cmdbuf[TELNETD_CMD_MAX];
	u32  cmdlen;
	u32  cmdpos;		 /* cursor position, 0..cmdlen */

	/* Out-of-band output (malloc'd, deferred send) */
	char *outbuf;
	u32   outbuf_len;
	bool  outbuf_pending;

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
	const char *hash = WEBUI_FAILSAFE_GIT_HASH;
	const char *variant = NULL;
	const char *prompt = telnetd_get_prompt();
	bool dirty = !!WEBUI_FAILSAFE_GIT_DIRTY;
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

/*
 * Queue or immediately send an out-of-band buffer.
 * On failure, saves pdata->outbuf for retry on MTK_TCP_CB_DATA_SENT.
 * Caller transfers ownership of @buf to this function.
 */
static void telnetd_send_or_queue(struct mtk_tcp_cb_data *cbd,
				  struct telnetd_pdata *pdata,
				  char *buf, u32 len)
{
	if (!buf || !len)
		return;

	if (mtk_tcp_send_data(cbd->conn, buf, len)) {
		/* Send failed → queue for retry */
		pdata->outbuf = buf;
		pdata->outbuf_len = len;
		pdata->outbuf_pending = true;
		pdata->state = TELNETD_S_RESPONDING;
		return;
	}

	/* Sent successfully — will free on DATA_SENT */
	pdata->outbuf = buf;
	pdata->outbuf_len = len;
	pdata->outbuf_pending = false;
	pdata->state = TELNETD_S_RESPONDING;
}

/*
 * Execute a U-Boot command on behalf of a telnet client.
 *
 * Captures console output, normalizes line endings to CRLF, and
 * delivers the result back to the client (or queues it if the TCP
 * send buffer is full).
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
			telnetd_send_or_queue(cbd, pdata, buf,
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
			telnetd_send_or_queue(cbd, pdata, err, strlen(err));
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
	 * executing guard: net_loop() calls mtk_tcp_periodic_check()
	 * which can re-enter this callback.  Block re-entry.
	 */
	pdata->executing = true;
	run_command(cmd, 0);
	pdata->executing = false;

	hist_save(pdata, cmd);

	/* Schedule eth reinit outside the callback chain */
	if (was_net)
		failsafe_notify_network_cmd_done();

	/* Print trailing prompt into console capture buffer */
	if (prompt[0] != '\n')
		printf("\n%s", prompt);
	else
		printf("%s", prompt);

	/* Read and deliver captured output */
	avail = membuf_avail(&gd->console_out);
	if (avail > TELNETD_OUTBUF_SIZE)
		avail = TELNETD_OUTBUF_SIZE;

	if (avail > 0) {
		size_t norm_len = 0;
		int got;

		raw_out = malloc(avail + 2);
		if (raw_out) {
			raw_out[0] = '\r'; raw_out[1] = '\n';
			got = membuf_get(&gd->console_out, raw_out + 2, avail);
			{
				char *norm = telnetd_normalize_output(
					raw_out, got + 2, &norm_len);
				if (norm) {
					free(raw_out);
					raw_out = NULL;
					telnetd_send_or_queue(cbd, pdata,
							      norm, norm_len);
				} else {
					telnetd_send_or_queue(cbd, pdata,
							      raw_out,
							      got + 2);
					raw_out = NULL;
				}
			}
		}
	} else {
		/* No output → send prompt to show completion */
		char *buf = malloc(strlen(prompt) + 3);

		if (buf) {
			buf[0] = '\r'; buf[1] = '\n';
			memcpy(buf + 2, prompt, strlen(prompt));
			telnetd_send_or_queue(cbd, pdata, buf,
					      strlen(prompt) + 2);
		}
	}

	if (use_private) {
		membuf_dispose(&gd->console_out);
		gd->console_out = saved_out;
	}
	free(raw_out);
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
		telnetd_execute(cbd, pdata->cmdbuf);
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
	edit_flush(cbd, pdata);

	/* Remove consumed bytes */
	if (i > 0) {
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
			telnetd_send_or_queue(cbd, pdata, greeting, len);
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
			telnetd_send_or_queue(cbd, pdata, fb, nego + text);
		} else {
			/* Last resort: send separately */
			mtk_tcp_send_data(cbd->conn, telnet_iac_nego,
					 sizeof(telnet_iac_nego));
			mtk_tcp_send_data(cbd->conn, telnet_fallback_text,
					 sizeof(telnet_fallback_text) - 1);
			pdata->outbuf = NULL;
			pdata->outbuf_len = 0;
			pdata->outbuf_pending = false;
			pdata->state = TELNETD_S_RESPONDING;
		}
	}
}

static void telnetd_callback(struct mtk_tcp_cb_data *cbd)
{
	struct telnetd_pdata *pdata;
	u8 sip[4];

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

		/* Process only when idle and not re-entered from net_loop() */
		if (pdata->state == TELNETD_S_IDLE && !pdata->executing)
			telnetd_process_input(cbd);
		break;

	case MTK_TCP_CB_DATA_SENT:
		pdata = cbd->pdata;
		if (!pdata)
			break;

		if (pdata->state != TELNETD_S_RESPONDING)
			break;

		/* Retry queued output if pending */
		if (pdata->outbuf_pending) {
			if (!mtk_tcp_send_data(cbd->conn, pdata->outbuf,
					       pdata->outbuf_len)) {
				pdata->outbuf_pending = false;
				return;
			}
			pdata->outbuf_pending = false;
		}

		free(pdata->outbuf);
		pdata->outbuf = NULL;
		pdata->outbuf_len = 0;
		pdata->state = TELNETD_S_IDLE;

		/* Process buffered input that arrived while sending */
		if (pdata->inbuf_size > 0 && !pdata->executing)
			telnetd_process_input(cbd);
		break;

	case MTK_TCP_CB_REMOTE_CLOSED:
	case MTK_TCP_CB_CLOSED:
		pdata = cbd->pdata;
		if (pdata) {
			free(pdata->outbuf);
			free(pdata);
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
