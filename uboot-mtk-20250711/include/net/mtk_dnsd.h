// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * Minimal DNS server for MediaTek web failsafe.
 * Resolves a configurable domain name to the device's IP address,
 * allowing users to access the failsafe UI via a friendly hostname.
 */

#ifndef __NET_MTK_DNSD_H__
#define __NET_MTK_DNSD_H__

#include <stdbool.h>

int mtk_dnsd_start(void);
void mtk_dnsd_stop(void);
bool mtk_dnsd_is_running(void);

#endif /* __NET_MTK_DNSD_H__ */
