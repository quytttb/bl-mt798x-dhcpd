/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * Minimal telnet server for MediaTek web failsafe.
 */

#ifndef __NET_MTK_TELNETD_H__
#define __NET_MTK_TELNETD_H__

#include <stdbool.h>

/**
 * mtk_telnetd_env_port() - Resolve a TCP port from an environment variable
 *
 * @name: environment variable name (e.g. "telnet_port")
 * @def:  fallback port when @name is unset or holds an invalid value
 *
 * Return: a valid port in [1, 65535]
 */
u16 mtk_telnetd_env_port(const char *name, u16 def);

/**
 * mtk_telnetd_start() - Start the telnet server on a given port
 *
 * @port: TCP port number (host byte order)
 * Return: 0 on success, negative on error
 */
int mtk_telnetd_start(u16 port);

/**
 * mtk_telnetd_stop() - Stop the telnet server
 */
void mtk_telnetd_stop(void);
bool mtk_telnetd_is_running(void);

/**
 * mtk_telnetd_exec_pending() - a telnet command is queued but not started
 */
bool mtk_telnetd_exec_pending(void);

/**
 * mtk_telnetd_exec_active() - a telnet command is queued or running
 *
 * The web console uses this to refuse starting a second command while a
 * telnet command occupies the single execution slot.
 */
bool mtk_telnetd_exec_active(void);

/**
 * mtk_telnetd_poll() - run a queued telnet command
 *
 * MUST be called from the failsafe main poll loop, outside any eth_rx()
 * frame.  Executing run_command() (which may enter net_loop() for tftp,
 * ping, ...) from inside the eth_rx() → TCP callback chain would nest
 * eth_rx() over the same DMA RX ring and corrupt it.
 */
void mtk_telnetd_poll(void);

#endif /* __NET_MTK_TELNETD_H__ */
