// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Simple TCP stack implementation
 */

#ifndef __NET_MTK_MTK_TCP_H__
#define __NET_MTK_MTK_TCP_H__

#ifdef __mips__
#undef sp /* MIPS register name collision with struct field 'sp' */
#endif

enum mtk_tcp_cb_status {
	MTK_TCP_CB_NONE,
	MTK_TCP_CB_NEW_CONN,
	MTK_TCP_CB_DATA_RCVD,
	MTK_TCP_CB_DATA_SENT,
	MTK_TCP_CB_REMOTE_CLOSING,
	MTK_TCP_CB_REMOTE_CLOSED,
	MTK_TCP_CB_CLOSING,
	MTK_TCP_CB_CLOSED,
	MTK_TCP_CB_POLL
};

struct mtk_tcp_cb_data {
	const void *conn;
	enum mtk_tcp_cb_status status;
	bool timedout;
	__be32 sip;
	__be16 sp;
	__be16 dp;
	void *pdata;
	void *data;
	uint32_t datalen;
};

typedef void (*mtk_tcp_conn_cb)(struct mtk_tcp_cb_data *cbd);

/* Initialize TCP subsystem */
void mtk_tcp_start(void);

/* Listen on specific port */
int mtk_tcp_listen(__be16 port, mtk_tcp_conn_cb cb);

/* Stop listening one porot */
int mtk_tcp_listen_stop(__be16 port);

/* Connect to remote port */
int mtk_tcp_connect(__be32 destip, __be16 port, mtk_tcp_conn_cb cb,
		    void *pdata);

/* Set connection's private data. return previous pdata */
void *mtk_tcp_conn_set_pdata(const void *conn, void *pdata);

/*
 * Set data buffer to be sent. Fail if previous data hasn't been fully sent.
 * Contents pointed by data shouldn't be changed before they're fully acked
 */
int mtk_tcp_send_data(const void *conn, const void *data, uint32_t size);

/* Close a connection */
int mtk_tcp_close_conn(const void *conn, int rst);

/* Close all connections and then exit net loop */
void mtk_tcp_close_all_conn(void);

/*
 * Close every connection still tracked for the given local port.
 *
 * Upper layers call this before releasing the state their per-connection
 * data points to, so that no stale connection survives into the next
 * session.
 */
void mtk_tcp_close_conn_by_port(__be16 port);

/* Reset all connections and then exit net loop */
void mtk_tcp_reset_all_conn(void);

/* Return 1 if connection is in ESTABLISHED state */
int mtk_tcp_conn_is_alive(const void *conn);

/* Called periodically to check the TCP status & send packets.
 * Returns 1 if all listeners and connections are done (can exit loop), 0 otherwise.
 */
int mtk_tcp_periodic_check(void);

/*
 * Out-of-band console abort (telnet Ctrl+C, web console "Abort" button).
 *
 * A console session that is blocked inside run_command() cannot reach the
 * serial Ctrl+C path, so it records a request here instead.  net_loop()
 * consumes it (mtk_tcp_abort_pending()) on the next iteration and takes
 * the exact same exit path as a serial Ctrl+C.
 */
void mtk_tcp_abort_request(void);
void mtk_tcp_abort_clear(void);
bool mtk_tcp_abort_pending(void);

#endif /* __NET_MTK_MTK_TCP_H__ */
