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

#include <errno.h>
#include <malloc.h>
#include <limits.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <vsprintf.h>
#include <net/mtk_httpd.h>

#ifdef CONFIG_MTD
#include <linux/mtd/mtd.h>
#include "../../board/mediatek/common/mtd_helper.h"
#endif

#ifdef CONFIG_MTK_BOOTMENU_MMC
#include "../../board/mediatek/common/mmc_helper.h"
#endif

#ifdef CONFIG_PARTITIONS
#include <part.h>
#endif

#include <failsafe/fs.h>
#include <failsafe/helpers.h>

/* ------------------------------------------------------------------ */
/*  HTTP response helpers                                              */
/* ------------------------------------------------------------------ */

void failsafe_http_reply_text(struct httpd_response *response,
			      int code, const char *text)
{
	response->status = HTTP_RESP_STD;
	response->data = text ? text : "";
	response->size = strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
}

void failsafe_http_reply_json(struct httpd_response *response,
			      int code, const char *json)
{
	response->status = HTTP_RESP_STD;
	response->data = json ? json : "{}";
	response->size = strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
}

void failsafe_http_reply_json_alloc(struct httpd_response *response,
				    int code, const char *json,
				    void *session_data)
{
	response->status = HTTP_RESP_STD;
	response->data = json ? json : "{}";
	response->size = strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
	response->session_data = session_data;
}

/* ------------------------------------------------------------------ */
/*  Form value extraction                                              */
/* ------------------------------------------------------------------ */

int failsafe_get_form_value(struct httpd_request *request,
			    const char *key, char **out,
			    size_t max_len, bool allow_empty,
			    bool allow_missing)
{
	struct httpd_form_value *v;
	char *buf;
	size_t n;

	if (!request || !key || !out)
		return -EINVAL;

	v = httpd_request_find_value(request, key);
	if (!v || !v->data) {
		if (allow_missing) {
			*out = NULL;
			return 0;
		}
		if (allow_empty) {
			buf = strdup("");
			if (!buf)
				return -ENOMEM;
			*out = buf;
			return 0;
		}
		return -EINVAL;
	}

	n = v->size;
	if (!allow_empty && !n)
		return -EINVAL;
	if (n > max_len)
		return -E2BIG;

	buf = malloc(n + 1);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, v->data, n);
	buf[n] = '\0';
	*out = buf;
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Session lifecycle                                                  */
/* ------------------------------------------------------------------ */

void failsafe_free_session(enum httpd_uri_handler_status status,
			   struct httpd_response *response)
{
	if (status != HTTP_CB_CLOSED)
		return;

	if (response->session_data) {
		free(response->session_data);
		response->session_data = NULL;
	}
}

/* ------------------------------------------------------------------ */
/*  Embedded file output                                               */
/* ------------------------------------------------------------------ */

int failsafe_output_file(struct httpd_response *response,
			 const char *filename,
			 const char *content_type)
{
	const struct fs_desc *file;

	file = fs_find_file(filename);

	response->status = HTTP_RESP_STD;

	if (file) {
		response->data = file->data;
		response->size = file->size;
		/* embedded assets are gzip-compressed at build time */
		response->info.content_encoding = "gzip";
		response->info.code = 200;
	} else {
		response->data = "Error: file not found";
		response->size = strlen(response->data);
		response->info.content_encoding = NULL;
		response->info.code = 404;
	}

	response->info.connection_close = 1;
	response->info.content_type = content_type ? content_type : "text/html";

	return file ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/*  String utilities                                                   */
/* ------------------------------------------------------------------ */

void failsafe_str_sanitize(char *s)
{
	char *p;

	if (!s)
		return;

	for (p = s; *p; p++) {
		unsigned char c = *p;

		if (isalnum(c) || c == '-' || c == '_' || c == '.')
			continue;

		*p = '_';
	}
}

/* ------------------------------------------------------------------ */
/*  JSON escape                                                        */
/* ------------------------------------------------------------------ */

size_t json_escape(char *dst, size_t dst_sz, const char *src)
{
	size_t di = 0;
	const unsigned char *s = (const unsigned char *)src;

	if (!dst || !dst_sz)
		return 0;

	if (!src) {
		dst[0] = '\0';
		return 0;
	}

	while (*s && di + 2 < dst_sz) {
		unsigned char c = *s++;

		if (c == '"' || c == '\\') {
			if (di + 2 >= dst_sz)
				break;
			dst[di++] = '\\';
			dst[di++] = (char)c;
			continue;
		}

		if (c == '\n' || c == '\r' || c == '\t') {
			if (di + 2 >= dst_sz)
				break;
			dst[di++] = '\\';
			dst[di++] = (c == '\n') ? 'n' : (c == '\r') ? 'r' : 't';
			continue;
		}

		if (c < 0x20) {
			/* skip other control chars */
			dst[di++] = ' ';
			continue;
		}

		dst[di++] = (char)c;
	}

	dst[di] = '\0';
	return di;
}

/* ------------------------------------------------------------------ */
/*  Safe buffer append (JSON payload builders)                         */
/* ------------------------------------------------------------------ */

int buf_appendf(char *buf, int size, int len, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (len >= size)
		return len;

	va_start(ap, fmt);
	n = vscnprintf(buf + len, size - len, fmt, ap);
	va_end(ap);

	if (n > 0)
		return len + n;
	return len;
}

/* ------------------------------------------------------------------ */
/*  MMC presence check                                                 */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_MTK_BOOTMENU_MMC
bool failsafe_mmc_present(void)
{
	struct mmc *mmc;
	struct blk_desc *bd;

	mmc = _mmc_get_dev(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0, false);
	bd = mmc ? mmc_get_blk_desc(mmc) : NULL;

	return mmc && bd && bd->type != DEV_TYPE_UNKNOWN;
}
#else
bool failsafe_mmc_present(void)
{
	return false;
}
#endif

/* ------------------------------------------------------------------ */
/*  Storage target helpers                                             */
/* ------------------------------------------------------------------ */

bool failsafe_mtd_part_exists(const char *name)
{
#ifdef CONFIG_MTD
	struct mtd_info *mtd;

	if (!name || !*name)
		return false;

	gen_mtd_probe_devices();
	mtd = get_mtd_device_nm(name);
	if (IS_ERR(mtd))
		return false;

	put_mtd_device(mtd);
	return true;
#else
	(void)name;
	return false;
#endif
}

int parse_u64_len(const char *s, u64 *out)
{
	char *end;
	unsigned long long v;

	if (!s || !*s || !out)
		return -EINVAL;

	v = simple_strtoull(s, &end, 0);
	if (end == s)
		return -EINVAL;

	while (*end == ' ' || *end == '\t')
		end++;

	if (!*end) {
		*out = (u64)v;
		return 0;
	}

	if (!strcasecmp(end, "k") || !strcasecmp(end, "kb") ||
	    !strcasecmp(end, "kib")) {
		*out = (u64)v * 1024ULL;
		return 0;
	}

	return -EINVAL;
}

int flash_open_target(const char *storage_sel, const char *target_name,
		      struct flash_target *t)
{

	if (!storage_sel || !target_name || !t)
		return -EINVAL;

	memset(t, 0, sizeof(*t));

	if (!strcasecmp(storage_sel, "mtd") ||
	    (!strcasecmp(storage_sel, "auto") && failsafe_mtd_part_exists(target_name))) {
#ifdef CONFIG_MTD
		static bool mtd_probed;

		if (!mtd_probed) {
			gen_mtd_probe_devices();
			mtd_probed = true;
		}
		t->mtd = get_mtd_device_nm(target_name);
		if (IS_ERR(t->mtd)) {
			t->mtd = NULL;
			return -ENODEV;
		}

		t->src = FAILSAFE_SRC_MTD;
		t->base = 0;
		t->size = t->mtd->size;
		return 0;
#else
		return -ENODEV;
#endif
	}

#ifdef CONFIG_MTK_BOOTMENU_MMC
	t->mmc = _mmc_get_dev(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0, false);
	if (!t->mmc)
		return -ENODEV;

	t->src = FAILSAFE_SRC_MMC;
	if (!strcmp(target_name, "raw")) {
		t->base = 0;
		t->size = t->mmc->capacity_user;
		return 0;
	}

	if (_mmc_find_part(t->mmc, target_name, &t->dpart, true))
		return -ENODEV;

	t->base = (u64)t->dpart.start * t->dpart.blksz;
	t->size = (u64)t->dpart.size * t->dpart.blksz;
	return 0;
#else
	return -ENODEV;
#endif
}

void flash_close_target(struct flash_target *t)
{
	if (!t)
		return;
#ifdef CONFIG_MTD
	if (t->mtd)
		put_mtd_device(t->mtd);
#endif
}

/* ------------------------------------------------------------------ */
/*  MMC manufacturer ID lookup                                         */
/* ------------------------------------------------------------------ */

struct mmc_mid_entry {
	unsigned int mid;
	const char *name;
};

static const struct mmc_mid_entry mmc_mid_table[] = {
	{ 0x00, "SanDisk/Spansion" },
	{ 0x01, "Samsung" },
	{ 0x02, "Kingston/SanDisk" },
	{ 0x03, "Toshiba" },
	{ 0x05, "Unknown" },
	{ 0x06, "Unknown"},
	{ 0x11, "Toshiba" },
	{ 0x13, "Micron" },
	{ 0x15, "Samsung/SanDisk/LG" },
	{ 0x18, "Swissbit" },
	{ 0x2c, "HIKSEM/Kingston" },
	{ 0x2f, "Konsemi" },
	{ 0x30, "SMART Modular" },
	{ 0x32, "Qimonda" },
	{ 0x37, "KingMax"},
	{ 0x44, "ATP/Transcend" },
	{ 0x45, "SanDisk/WesternDigital" },
	{ 0x70, "Kingston" },
	{ 0x88, "Longsys" },
	{ 0x90, "SK hynix" },
	{ 0x9b, "YMTC" },
	{ 0x9c, "ATP" },
	{ 0xce, "Samsung" },
	{ 0xd6, "Longsys" },
	{ 0xdf, "SCY" },
	{ 0xea, "Kowin/SiliconGo/SPeMMC" },
	{ 0xec, "ATO/Rayson" },
	{ 0xf4, "BIWIN" },
	{ 0xfe, "Foresee/Micron" },
};

static const char *mmc_mid_lookup(unsigned int mid)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(mmc_mid_table); i++) {
		if (mmc_mid_table[i].mid == mid)
			return mmc_mid_table[i].name;
	}

	return NULL;
}

/**
 * failsafe_mmc_vendor_pretty() - append manufacturer name to MMC vendor string
 *
 * Input format:  "Man 00002c Snr 02e9a8c9"
 * Output format: "Man 00002c(HIKSEM) Snr 02e9a8c9"
 *
 * If the MID is unknown, the raw vendor string is copied unchanged.
 */
void failsafe_mmc_vendor_pretty(const char *vendor, char *dst, size_t dst_sz)
{
	const char *p, *snr;
	unsigned int mid;
	const char *name;

	if (!vendor || !vendor[0] || !dst || !dst_sz)
		return;

	dst[0] = '\0';

	/* Only transform strings starting with "Man " */
	if (strncmp(vendor, "Man ", 4))
		goto copy_raw;

	/* Parse 6-char hex MID: "Man XXXXXX" */
	p = vendor + 4;
	if (strlen(p) < 6)
		goto copy_raw;

	/* sscanf with %6x for safety */
	{
		char hex[7];
		memcpy(hex, p, 6);
		hex[6] = '\0';

		/* Validate hex */
		{
			int j;
			for (j = 0; j < 6; j++) {
				if (!isxdigit((unsigned char)hex[j]))
					goto copy_raw;
			}
		}

		mid = simple_strtoul(hex, NULL, 16);
	}

	/* Find " Snr" after the MID */
	snr = strstr(p + 6, " Snr");
	if (!snr)
		goto copy_raw;

	/* Lookup manufacturer name */
	name = mmc_mid_lookup(mid);

	/* Build output: "Man %06x(NAME) Snr ..." or raw copy */
	if (name) {
		snprintf(dst, dst_sz, "Man %06x(%s)%s",
			 mid, name, snr);
	} else {
		goto copy_raw;
	}

	return;

copy_raw:
	strlcpy(dst, vendor ? vendor : "", dst_sz);
}

int flash_parse_storage_target(struct httpd_request *request,
			       char *storage_sel, size_t storage_sz,
			       char *target_name, size_t target_sz)
{
	struct httpd_form_value *storage, *target;

	if (!request || !storage_sel || !target_name)
		return -EINVAL;

	storage = httpd_request_find_value(request, "storage");
	target = httpd_request_find_value(request, "target");

	if (storage && storage->data)
		strlcpy(storage_sel, storage->data, storage_sz);

	if (target && target->data)
		strlcpy(target_name, target->data, target_sz);

	/* allow overriding storage by target prefix: mtd:<name> / mmc:<name> */
	if (!strncmp(target_name, "mtd:", 4)) {
		memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
		strlcpy(storage_sel, "mtd", storage_sz);
	} else if (!strncmp(target_name, "mmc:", 4)) {
		memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
		strlcpy(storage_sel, "mmc", storage_sz);
	}

	return target_name[0] ? 0 : -EINVAL;
}
