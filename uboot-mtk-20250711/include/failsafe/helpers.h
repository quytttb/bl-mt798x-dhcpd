/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Common HTTP helper functions shared by all failsafe modules
 */

#ifndef _FAILSAFE_MODULES_HELPERS_H_
#define _FAILSAFE_MODULES_HELPERS_H_

#include <net/mtk_httpd.h>
#include <linux/types.h>
#include <stdbool.h>
#ifdef CONFIG_PARTITIONS
#include <part.h>
#endif

/* Storage source types (shared between backup and flash modules) */
enum failsafe_storage_src {
	FAILSAFE_SRC_MTD = 0,
	FAILSAFE_SRC_MMC = 1,
};

/* Flash target descriptor (shared between backup and flash modules) */
struct flash_target {
	enum failsafe_storage_src src;
	u64 base;
	u64 size;
#ifdef CONFIG_MTD
	struct mtd_info *mtd;
#endif
#ifdef CONFIG_MTK_BOOTMENU_MMC
	struct mmc *mmc;
	struct disk_partition dpart;
#endif
};

/**
 * failsafe_http_reply_text - send a plain-text HTTP response
 * @response: HTTP response structure
 * @code: HTTP status code (200, 400, 405, 500, etc.)
 * @text: response body (may be NULL, treated as "")
 */
void failsafe_http_reply_text(struct httpd_response *response,
			      int code, const char *text);

/**
 * failsafe_http_reply_json - send a JSON HTTP response (static string)
 * @response: HTTP response structure
 * @code: HTTP status code
 * @json: JSON string (may be NULL, treated as "{}")
 */
void failsafe_http_reply_json(struct httpd_response *response,
			      int code, const char *json);

/**
 * failsafe_http_reply_json_alloc - send JSON response with session-owned buffer
 * @response: HTTP response structure
 * @code: HTTP status code
 * @json: JSON string (may be NULL)
 * @session_data: heap-allocated buffer to be freed on HTTP_CB_CLOSED (may be NULL)
 *
 * Use this when the JSON buffer is dynamically allocated and must survive
 * until the HTTP session closes.  The caller must NOT free @json after
 * passing it here; the framework will free @session_data on close.
 */
void failsafe_http_reply_json_alloc(struct httpd_response *response,
				    int code, const char *json,
				    void *session_data);

/**
 * failsafe_get_form_value - extract and duplicate a form/query value
 * @request: HTTP request
 * @key: form field name
 * @out: receives heap-allocated copy of the value (caller must free)
 * @max_len: maximum accepted value length
 * @allow_empty: if true, missing or zero-length values produce an empty string
 * @allow_missing: if true, missing keys return *out = NULL with success
 *
 * Returns 0 on success, -EINVAL if key missing/empty (when not allowed),
 * -E2BIG if value exceeds max_len, -ENOMEM on allocation failure.
 */
int failsafe_get_form_value(struct httpd_request *request,
			    const char *key, char **out,
			    size_t max_len, bool allow_empty,
			    bool allow_missing);

/**
 * failsafe_free_session - free session_data on HTTP_CB_CLOSED
 * @status: handler status enum
 * @response: HTTP response (session_data freed if non-NULL)
 *
 * Call this at the top of every handler that allocates session_data.
 */
void failsafe_free_session(enum httpd_uri_handler_status status,
			   struct httpd_response *response);

/**
 * failsafe_output_file - serve an embedded (gzip-compressed) file
 * @response: HTTP response structure
 * @filename: path in the embedded filesystem (e.g. "index.html")
 * @content_type: MIME type (NULL defaults to "text/html")
 *
 * Sets response->info.content_encoding = "gzip" when the file exists.
 * Returns 0 on success, 1 if the file was not found.
 */
int failsafe_output_file(struct httpd_response *response,
			 const char *filename,
			 const char *content_type);

/**
 * failsafe_str_sanitize - replace non-alphanumeric chars with '_'
 * @s: string to sanitize in-place (may be NULL)
 *
 * Keeps '-', '_', '.' intact.  Useful for generating safe filenames
 * from user-supplied partition or device names.
 */
void failsafe_str_sanitize(char *s);

/**
 * json_escape - escape a string for JSON output
 * @dst: destination buffer
 * @dst_sz: destination buffer size
 * @src: source string
 *
 * Returns the number of characters written (excluding null terminator).
 */
size_t json_escape(char *dst, size_t dst_sz, const char *src);

/**
 * buf_appendf - append a formatted string to a buffer
 * @buf: destination buffer
 * @size: total size of @buf
 * @len: current used length of @buf
 * @fmt: format string
 * ...
 *
 * Appends the formatted output at @buf + @len.  Uses vscnprintf() so the
 * returned value is the number of characters actually written (clamped to
 * @size - 1 - @len); feeding it back as @len keeps @buf within bounds.
 *
 * Returns the new used length.
 */
int buf_appendf(char *buf, int size, int len, const char *fmt, ...)
	__attribute__((format(printf, 4, 5)));

/**
 * failsafe_mmc_present - check if MMC device is present
 *
 * Returns true if the configured MMC device is present and valid.
 */
bool failsafe_mmc_present(void);

/* ------------------------------------------------------------------ */
/*  Storage target helpers (shared by backup and flash modules)        */
/* ------------------------------------------------------------------ */

/**
 * failsafe_mtd_part_exists - check if an MTD partition with the given name exists
 * @name: partition name
 *
 * Returns true if the partition exists, false otherwise.
 */
bool failsafe_mtd_part_exists(const char *name);

/**
 * parse_u64_len - parse a string into a u64 with optional size suffix
 * @s: input string (decimal or hex, optionally followed by "k"/"kb"/"kib")
 * @out: receives parsed value
 *
 * Returns 0 on success, -EINVAL on parse error.
 */
int parse_u64_len(const char *s, u64 *out);

/**
 * flash_open_target - open an MTD or MMC storage target
 * @storage_sel: storage type ("mtd", "mmc", or "auto")
 * @target_name: partition or device name
 * @t: receives target descriptor (caller must call flash_close_target)
 *
 * Returns 0 on success, negative errno on failure.
 */
int flash_open_target(const char *storage_sel, const char *target_name,
		      struct flash_target *t);

/**
 * flash_close_target - release resources from flash_open_target
 * @t: target descriptor (may be NULL, safe to call multiple times)
 */
void flash_close_target(struct flash_target *t);

/**
 * flash_parse_storage_target - extract and normalize storage/target from request
 * @request: HTTP request
 * @storage_sel: receives storage type ("mtd", "mmc", or "auto")
 * @storage_sz: size of storage_sel buffer
 * @target_name: receives target name (with "mtd:"/"mmc:" prefix stripped)
 * @target_sz: size of target_name buffer
 *
 * If target_name has a "mtd:" or "mmc:" prefix, it is stripped and
 * storage_sel is overridden accordingly.
 *
 * Returns 0 on success, -EINVAL if neither storage nor target provided.
 */
int flash_parse_storage_target(struct httpd_request *request,
			       char *storage_sel, size_t storage_sz,
			       char *target_name, size_t target_sz);

/* ------------------------------------------------------------------ */
/*  MMC vendor helper                                                  */
/* ------------------------------------------------------------------ */

/**
 * failsafe_mmc_vendor_pretty - convert MMC vendor string to human-readable
 * @vendor: raw vendor string in format "Man XXXXXX Snr YYYYYYYY"
 * @dst: output buffer
 * @dst_sz: output buffer size
 *
 * Extracts the manufacturer ID from the vendor string and appends the
 * manufacturer name in parentheses, e.g. "Man 000015(Samsung) Snr 01234567".
 * If the MID is not in the lookup table, the original string is returned
 * unchanged.
 */
void failsafe_mmc_vendor_pretty(const char *vendor, char *dst, size_t dst_sz);

#endif /* _FAILSAFE_MODULES_HELPERS_H_ */
