/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Internal interfaces for Failsafe Web UI modules
 */

#ifndef _FAILSAFE_INTERNAL_H_
#define _FAILSAFE_INTERNAL_H_

#include <net/mtk_httpd.h>
#include <linux/types.h>
#include <failsafe/fw_type.h>
#include <failsafe/helpers.h>

/* ------------------------------------------------------------------ */
/*  Core weak functions (defined in failsafe.c, used by modules)       */
/* ------------------------------------------------------------------ */

int failsafe_validate_image(const void *data, size_t size,
			    failsafe_fw_t fw);
int failsafe_write_image(const void *data, size_t size,
			 failsafe_fw_t fw);

/**
 * failsafe_notify_network_cmd_done() - signal that a network command finished
 *
 * Called from telnetd after executing a network command (tftp, ping, etc.)
 * whose inner net_loop() calls eth_halt() on exit.
 *
 * The poll loop responds by calling eth_init() OUTSIDE the eth_rx() →
 * TCP callback chain, avoiding DMA receive-descriptor corruption that
 * occurs when eth_init() is called inline from within a TCP callback.
 * It also re-registers the DHCP UDP handler that net_clear_handlers()
 * removed.
 */
void failsafe_notify_network_cmd_done(void);

/* ------------------------------------------------------------------ */
/*  Handler declarations (used by failsafe.c for URI registration)     */
/* ------------------------------------------------------------------ */

/* ---- core handlers (misc.c) ---- */
void version_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void sysinfo_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void reboot_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void reboot_failsafe_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void not_found_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void index_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void style_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void js_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void html_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);

/* ---- upgrade handlers (upgrade.c) ---- */
void upload_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void result_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void mtd_layout_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);

/* ---- sub-module handlers ---- */

/* theme */
void picture_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);

#ifdef CONFIG_WEBUI_FAILSAFE_CONSOLE
int failsafe_webconsole_ensure_recording(void);
extern bool webconsole_exec_busy;
void webconsole_poll_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void webconsole_exec_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void webconsole_clear_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
#endif

#ifdef CONFIG_WEBUI_FAILSAFE_ENV
void env_list_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void env_set_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void env_unset_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void env_reset_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void env_restore_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void env_size_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void theme_get_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void theme_set_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
#endif

#ifdef CONFIG_WEBUI_FAILSAFE_BACKUP
void backupinfo_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void backup_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
#endif

#ifdef CONFIG_WEBUI_FAILSAFE_FLASH
void flash_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
#endif

#ifdef CONFIG_WEBUI_FAILSAFE_UBI
void ubi_info_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void ubi_volumes_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void ubi_attach_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void ubi_detach_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void ubi_create_vol_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void ubi_remove_vol_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void ubi_rename_vol_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void ubi_mtd_list_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
void ubi_backup_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response);
#endif

/* ------------------------------------------------------------------ */
/*  Module registration functions                                      */
/* ------------------------------------------------------------------ */

/* Always compiled */
void misc_register_handlers(struct httpd_instance *inst);
void upgrade_register_handlers(struct httpd_instance *inst);

/* Conditionally compiled */
#ifdef CONFIG_WEBUI_FAILSAFE_BACKUP
void backup_register_handlers(struct httpd_instance *inst);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_FLASH
void flash_register_handlers(struct httpd_instance *inst);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_ENV
void env_register_handlers(struct httpd_instance *inst);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_UI_BOOTSTRAP
void theme_register_handlers(struct httpd_instance *inst);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_UBI
void ubi_register_handlers(struct httpd_instance *inst);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_CONSOLE
void console_register_handlers(struct httpd_instance *inst);
#endif

#endif /* _FAILSAFE_INTERNAL_H_ */
