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

#endif /* __NET_MTK_TELNETD_H__ */
