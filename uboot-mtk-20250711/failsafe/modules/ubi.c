/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe UBI volume management
 */

#include <errno.h>
#include <malloc.h>
#include <memalign.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <net/mtk_httpd.h>
#include <mtd.h>
#include <nand.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/err.h>
#include <ubi_uboot.h>
#include <linux/errno.h>
#include <vsprintf.h>
#include <command.h>

#ifdef CONFIG_CMD_UBIFS
#include <ubifs_uboot.h>
#endif

#include <failsafe/internal.h>

/* Max buffer size for JSON response */
#define UBI_JSON_BUF_SZ		16384

/* Max volume name length */
#define UBI_VOL_NAME_MAX_LEN	128

/* Max MTD partition name length */
#define UBI_MTD_NAME_MAX_LEN	64

/**
 * ubi_info_handler - GET /ubi/info
 *
 * Returns JSON with UBI device information:
 * {"mtd_name":"...","flash_size":0,"peb_size":0,"leb_size":0,...}
 */
void ubi_info_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *buf;
	int len = 0;
	int left = UBI_JSON_BUF_SZ;

	failsafe_free_session(status, response);

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_GET) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	buf = malloc(left);
	if (!buf) {
		failsafe_http_reply_json(response, 500, "{\"error\":\"oom\"}");
		return;
	}

	/* Check if UBI is attached */
	struct ubi_device *ubi = ubi_devices[0];

	if (!ubi) {
		len = buf_appendf(buf, left, len,
			"{\"error\":\"no ubi device\",\"attached\":false}");
		goto done;
	}

	len = buf_appendf(buf, left, len,
		"{\"attached\":true,"
		"\"mtd_name\":\"%s\","
		"\"ubi_num\":%d,"
		"\"flash_size\":%llu,"
		"\"peb_size\":%d,"
		"\"leb_size\":%d,"
		"\"good_peb_count\":%d,"
		"\"bad_peb_count\":%d,"
		"\"min_io_size\":%d,"
		"\"max_vol_count\":%d,"
		"\"vol_count\":%d,"
		"\"avail_pebs\":%d,"
		"\"rsvd_pebs\":%d,"
		"\"beb_rsvd_pebs\":%d,"
		"\"max_ec\":%d,"
		"\"mean_ec\":%d}",
		ubi->mtd ? ubi->mtd->name : "unknown",
		ubi->ubi_num,
		(unsigned long long)ubi->flash_size,
		ubi->peb_size,
		ubi->leb_size,
		ubi->good_peb_count,
		ubi->bad_peb_count,
		ubi->min_io_size,
		ubi->vtbl_slots,
		ubi->vol_count - UBI_INT_VOL_COUNT,
		ubi->avail_pebs,
		ubi->rsvd_pebs,
		ubi->beb_rsvd_pebs,
		ubi->max_ec,
		ubi->mean_ec);

done:
	failsafe_http_reply_json_alloc(response, 200, buf, buf);
}

/**
 * ubi_volumes_handler - GET /ubi/volumes
 *
 * Returns JSON array of UBI volumes:
 * {"volumes":[{"id":0,"name":"...","size":0,"type":"dynamic",...},...]}
 */
void ubi_volumes_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *buf;
	int len = 0;
	int left = UBI_JSON_BUF_SZ;
	bool first = true;

	failsafe_free_session(status, response);

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_GET) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	buf = malloc(left);
	if (!buf) {
		failsafe_http_reply_json(response, 500, "{\"error\":\"oom\"}");
		return;
	}

	struct ubi_device *ubi = ubi_devices[0];

	if (!ubi) {
		len = buf_appendf(buf, left, len,
			"{\"error\":\"no ubi device\",\"volumes\":[]}");
		goto done;
	}

	len = buf_appendf(buf, left, len, "{\"volumes\":[");

	for (int i = 0; i < ubi->vtbl_slots && len < left - 256; i++) {
		struct ubi_volume *vol = ubi->volumes[i];

		if (!vol)
			continue;
		if (vol->vol_id >= UBI_INTERNAL_VOL_START)
			continue;

		len = buf_appendf(buf, left, len,
			"%s{\"id\":%d,\"name\":\"%s\","
			"\"size\":%llu,\"used_bytes\":%llu,"
			"\"type\":\"%s\","
			"\"corrupted\":%d,\"upd_marker\":%d,"
			"\"skip_check\":%d,"
			"\"reserved_peb\":%d,\"alignment\":%d,"
			"\"data_pad\":%d,\"usable_leb_size\":%d}",
			first ? "" : ",",
			vol->vol_id,
			vol->name,
			(unsigned long long)vol->reserved_pebs * ubi->leb_size,
			(unsigned long long)vol->used_bytes,
			vol->vol_type == UBI_DYNAMIC_VOLUME ? "dynamic" : "static",
			vol->corrupted,
			vol->upd_marker,
			vol->skip_check,
			vol->reserved_pebs,
			vol->alignment,
			vol->data_pad,
			vol->usable_leb_size);

		first = false;
	}

	len = buf_appendf(buf, left, len, "]}");

done:
	failsafe_http_reply_json_alloc(response, 200, buf, buf);
}

/**
 * ubi_attach_handler - POST /ubi/attach
 *
 * Form parameters:
 *   mtd_name - MTD partition name to attach
 *
 * Returns JSON: {"ok":true} or {"error":"..."}
 */
void ubi_attach_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *mtd_name = NULL;
	char *json_out;
	int ret;

	failsafe_free_session(status, response);

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	/* Get MTD name */
	ret = failsafe_get_form_value(request, "mtd_name", &mtd_name,
		UBI_MTD_NAME_MAX_LEN, false, false);
	if (ret || !mtd_name || !mtd_name[0]) {
		json_out = strdup("{\"error\":\"missing mtd_name\"}");
		failsafe_http_reply_json_alloc(response, 400,
			json_out ? json_out : "{\"error\":\"missing mtd_name\"}",
			json_out);
		return;
	}

	/* Detach existing UBI first */
	ubi_detach();

	/* Attach to new partition */
	ret = ubi_part(mtd_name, NULL);
	free(mtd_name);

	if (ret) {
		json_out = malloc(128);
		if (json_out)
			snprintf(json_out, 128,
				"{\"error\":\"attach failed: %d\"}", ret);
		failsafe_http_reply_json_alloc(response, 500,
			json_out ? json_out : "{\"error\":\"attach failed\"}",
			json_out);
		return;
	}

	json_out = strdup("{\"ok\":true}");
	failsafe_http_reply_json_alloc(response, 200,
		json_out ? json_out : "{\"ok\":true}", json_out);
}

/**
 * ubi_detach_handler - POST /ubi/detach
 *
 * Returns JSON: {"ok":true} or {"error":"..."}
 */
void ubi_detach_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *json_out;
	int ret;

	failsafe_free_session(status, response);

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	ret = ubi_detach();

	if (ret) {
		json_out = malloc(128);
		if (json_out)
			snprintf(json_out, 128,
				"{\"error\":\"detach failed: %d\"}", ret);
		failsafe_http_reply_json_alloc(response, 500,
			json_out ? json_out : "{\"error\":\"detach failed\"}",
			json_out);
		return;
	}

	json_out = strdup("{\"ok\":true}");
	failsafe_http_reply_json_alloc(response, 200,
		json_out ? json_out : "{\"ok\":true}", json_out);
}

/**
 * ubi_create_vol_handler - POST /ubi/create
 *
 * Form parameters:
 *   name - Volume name
 *   size - Volume size in bytes (0 or empty for maximum)
 *   type - Volume type: "dynamic" or "static"
 *   skipcheck - Skip CRC check: "1" or "0"
 *
 * Returns JSON: {"ok":true} or {"error":"..."}
 */
void ubi_create_vol_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *name = NULL;
	char *size_str = NULL;
	char *type_str = NULL;
	char *skipcheck_str = NULL;
	char *json_out;
	int64_t size = 0;
	int dynamic = 1;
	bool skipcheck = false;
	int ret;

	failsafe_free_session(status, response);

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	struct ubi_device *ubi = ubi_devices[0];

	if (!ubi) {
		json_out = strdup("{\"error\":\"no ubi device attached\"}");
		failsafe_http_reply_json_alloc(response, 400,
			json_out ? json_out : "{\"error\":\"no ubi device\"}",
			json_out);
		return;
	}

	/* Get volume name */
	ret = failsafe_get_form_value(request, "name", &name,
		UBI_VOL_NAME_MAX_LEN, false, false);
	if (ret || !name || !name[0]) {
		json_out = strdup("{\"error\":\"missing volume name\"}");
		failsafe_http_reply_json_alloc(response, 400,
			json_out ? json_out : "{\"error\":\"missing name\"}",
			json_out);
		return;
	}

	/* Get size (optional) */
	ret = failsafe_get_form_value(request, "size", &size_str, 32, false, true);
	if (ret == 0 && size_str && size_str[0]) {
		size = simple_strtoull(size_str, NULL, 0);
	}
	free(size_str);

	/* Get type (optional, default dynamic) */
	ret = failsafe_get_form_value(request, "type", &type_str, 16, false, true);
	if (ret == 0 && type_str) {
		if (strncmp(type_str, "s", 1) == 0)
			dynamic = 0;
	}
	free(type_str);

	/* Get skipcheck (optional) */
	ret = failsafe_get_form_value(request, "skipcheck", &skipcheck_str, 4, false, true);
	if (ret == 0 && skipcheck_str) {
		skipcheck = (skipcheck_str[0] == '1');
	}
	free(skipcheck_str);

	/* Use maximum available size if not specified */
	if (size <= 0) {
		size = (int64_t)ubi->avail_pebs * ubi->leb_size;
	}

	/* Create volume */
	ret = ubi_create_vol(name, size, dynamic, UBI_VOL_NUM_AUTO, skipcheck);
	free(name);

	if (ret) {
		json_out = malloc(128);
		if (json_out)
			snprintf(json_out, 128,
				"{\"error\":\"create failed: %d\"}", ret);
		failsafe_http_reply_json_alloc(response, 500,
			json_out ? json_out : "{\"error\":\"create failed\"}",
			json_out);
		return;
	}

	json_out = strdup("{\"ok\":true}");
	failsafe_http_reply_json_alloc(response, 200,
		json_out ? json_out : "{\"ok\":true}", json_out);
}

/**
 * ubi_remove_vol_handler - POST /ubi/remove
 *
 * Form parameters:
 *   name - Volume name to remove
 *
 * Returns JSON: {"ok":true} or {"error":"..."}
 */
void ubi_remove_vol_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *name = NULL;
	char *json_out;
	int ret;

	failsafe_free_session(status, response);

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	struct ubi_device *ubi = ubi_devices[0];

	if (!ubi) {
		json_out = strdup("{\"error\":\"no ubi device attached\"}");
		failsafe_http_reply_json_alloc(response, 400,
			json_out ? json_out : "{\"error\":\"no ubi device\"}",
			json_out);
		return;
	}

	/* Get volume name */
	ret = failsafe_get_form_value(request, "name", &name,
		UBI_VOL_NAME_MAX_LEN, false, false);
	if (ret || !name || !name[0]) {
		json_out = strdup("{\"error\":\"missing volume name\"}");
		failsafe_http_reply_json_alloc(response, 400,
			json_out ? json_out : "{\"error\":\"missing name\"}",
			json_out);
		return;
	}

	/* Remove volume */
	ret = ubi_remove_vol(name);
	free(name);

	if (ret) {
		json_out = malloc(128);
		if (json_out)
			snprintf(json_out, 128,
				"{\"error\":\"remove failed: %d\"}", ret);
		failsafe_http_reply_json_alloc(response, 500,
			json_out ? json_out : "{\"error\":\"remove failed\"}",
			json_out);
		return;
	}

	json_out = strdup("{\"ok\":true}");
	failsafe_http_reply_json_alloc(response, 200,
		json_out ? json_out : "{\"ok\":true}", json_out);
}

/**
 * ubi_rename_vol_handler - POST /ubi/rename
 *
 * Form parameters:
 *   old_name - Current volume name
 *   new_name - New volume name
 *
 * Returns JSON: {"ok":true} or {"error":"..."}
 */
void ubi_rename_vol_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *old_name = NULL;
	char *new_name = NULL;
	char *json_out;
	int ret;

	failsafe_free_session(status, response);

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	struct ubi_device *ubi = ubi_devices[0];

	if (!ubi) {
		json_out = strdup("{\"error\":\"no ubi device attached\"}");
		failsafe_http_reply_json_alloc(response, 400,
			json_out ? json_out : "{\"error\":\"no ubi device\"}",
			json_out);
		return;
	}

	/* Get old name */
	ret = failsafe_get_form_value(request, "old_name", &old_name,
		UBI_VOL_NAME_MAX_LEN, false, false);
	if (ret || !old_name || !old_name[0]) {
		json_out = strdup("{\"error\":\"missing old_name\"}");
		failsafe_http_reply_json_alloc(response, 400,
			json_out ? json_out : "{\"error\":\"missing old_name\"}",
			json_out);
		return;
	}

	/* Get new name */
	ret = failsafe_get_form_value(request, "new_name", &new_name,
		UBI_VOL_NAME_MAX_LEN, false, false);
	if (ret || !new_name || !new_name[0]) {
		free(old_name);
		json_out = strdup("{\"error\":\"missing new_name\"}");
		failsafe_http_reply_json_alloc(response, 400,
			json_out ? json_out : "{\"error\":\"missing new_name\"}",
			json_out);
		return;
	}

	/* Find volume */
	struct ubi_volume *vol;
	vol = ubi_find_volume(old_name);
	if (!vol) {
		free(old_name);
		free(new_name);
		json_out = strdup("{\"error\":\"volume not found\"}");
		failsafe_http_reply_json_alloc(response, 404,
			json_out ? json_out : "{\"error\":\"not found\"}",
			json_out);
		return;
	}

	/* Rename volume */
	struct ubi_rename_entry rename;
	struct ubi_volume_desc desc;
	struct list_head list;

	rename.new_name_len = strlen(new_name);
	strcpy(rename.new_name, new_name);
	rename.remove = 0;
	desc.vol = vol;
	desc.mode = 0;
	rename.desc = &desc;
	INIT_LIST_HEAD(&rename.list);
	INIT_LIST_HEAD(&list);
	list_add(&rename.list, &list);

	ret = ubi_rename_volumes(ubi, &list);
	free(old_name);
	free(new_name);

	if (ret) {
		json_out = malloc(128);
		if (json_out)
			snprintf(json_out, 128,
				"{\"error\":\"rename failed: %d\"}", ret);
		failsafe_http_reply_json_alloc(response, 500,
			json_out ? json_out : "{\"error\":\"rename failed\"}",
			json_out);
		return;
	}

	json_out = strdup("{\"ok\":true}");
	failsafe_http_reply_json_alloc(response, 200,
		json_out ? json_out : "{\"ok\":true}", json_out);
}

/**
 * ubi_mtd_list_handler - GET /ubi/mtd_list
 *
 * Returns JSON array of available MTD partitions:
 * {"partitions":[{"name":"...","size":0,"type":"..."},...]}
 */
void ubi_mtd_list_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *buf;
	int len = 0;
	int left = UBI_JSON_BUF_SZ;
	bool first = true;
	struct mtd_info *mtd;

	failsafe_free_session(status, response);

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_GET) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	buf = malloc(left);
	if (!buf) {
		failsafe_http_reply_json(response, 500, "{\"error\":\"oom\"}");
		return;
	}

	len = buf_appendf(buf, left, len, "{\"partitions\":[");

	/* Probe all MTD devices */
	mtd_probe_devices();

	mtd_for_each_device(mtd) {
		if (len >= left - 256)
			break;

		len = buf_appendf(buf, left, len,
			"%s{\"name\":\"%s\",\"size\":%llu,\"erasesize\":%lu}",
			first ? "" : ",",
			mtd->name,
			(unsigned long long)mtd->size,
			(unsigned long)mtd->erasesize);

		first = false;
	}

	len = buf_appendf(buf, left, len, "]}");

	failsafe_http_reply_json_alloc(response, 200, buf, buf);
}

/**
 * ubi_backup_handler - POST /ubi/backup
 *
 * Form parameters:
 *   name - Volume name to backup/download
 *
 * Returns the volume content as a binary download.
 */
void ubi_backup_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	struct ubi_backup_session {
		struct ubi_volume *vol;
		struct ubi_device *ubi;
		u64 total;
		u64 cur;
		void *buf;
		size_t buf_size;
		char hdr[512];
		int hdr_len;
	} *st;
	struct httpd_form_value *name_val;
	char *vol_name = NULL;
	char filename[128];
	int ret;

	failsafe_free_session(status, response);

	if (status == HTTP_CB_RESPONDING) {
		u64 remain;
		size_t to_read;

		st = response->session_data;
		if (!st) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		remain = st->total - st->cur;
		if (!remain) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		to_read = (size_t)min_t(u64, remain, st->buf_size);

		ret = ubi_volume_read(st->vol->name, st->buf, st->cur, to_read);
		if (ret) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		st->cur += to_read;

		response->status = HTTP_RESP_CUSTOM;
		response->data = (const char *)st->buf;
		response->size = to_read;
		return;
	}

	if (status == HTTP_CB_CLOSED) {
		st = response->session_data;
		if (st) {
			free(st->buf);
			free(st);
		}
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	/* Get volume name */
	name_val = httpd_request_find_value(request, "name");
	if (!name_val || !name_val->data || !name_val->size) {
		failsafe_http_reply_text(response, 400, "missing name");
		return;
	}

	if (name_val->size > UBI_VOL_NAME_MAX_LEN) {
		failsafe_http_reply_text(response, 400, "name too long");
		return;
	}

	vol_name = malloc(name_val->size + 1);
	if (!vol_name) {
		failsafe_http_reply_text(response, 500, "oom");
		return;
	}

	memcpy(vol_name, name_val->data, name_val->size);
	vol_name[name_val->size] = '\0';

	/* Check if UBI is attached */
	struct ubi_device *ubi = ubi_devices[0];

	if (!ubi) {
		free(vol_name);
		failsafe_http_reply_text(response, 400, "no ubi device");
		return;
	}

	/* Find volume */
	struct ubi_volume *vol = ubi_find_volume(vol_name);
	if (!vol) {
		free(vol_name);
		failsafe_http_reply_text(response, 404, "volume not found");
		return;
	}

	/* Allocate session */
	st = calloc(1, sizeof(*st));
	if (!st) {
		free(vol_name);
		failsafe_http_reply_text(response, 500, "oom");
		return;
	}

	st->buf_size = 64 * 1024;
	st->buf = malloc(st->buf_size);
	if (!st->buf) {
		free(st);
		free(vol_name);
		failsafe_http_reply_text(response, 500, "oom");
		return;
	}

	st->vol = vol;
	st->ubi = ubi;
	st->total = (u64)vol->used_bytes;
	st->cur = 0;

	/* Generate filename */
	{
		char safe_name[64];
		const char *p;
		size_t i;

		/* Sanitize volume name for filename */
		p = vol_name;
		for (i = 0; i < sizeof(safe_name) - 1 && *p; i++, p++) {
			unsigned char c = *p;
			if (isalnum(c) || c == '-' || c == '_')
				safe_name[i] = c;
			else
				safe_name[i] = '_';
		}
		safe_name[i] = '\0';

		snprintf(filename, sizeof(filename), "ubi_%s.bin", safe_name);
	}

	free(vol_name);

	/* Build HTTP header */
	st->hdr_len = snprintf(st->hdr, sizeof(st->hdr),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Length: %llu\r\n"
		"Content-Disposition: attachment; filename=\"%s\"\r\n"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n"
		"\r\n",
		(unsigned long long)st->total,
		filename);

	response->session_data = st;
	response->status = HTTP_RESP_CUSTOM;
	response->data = st->hdr;
	response->size = st->hdr_len;
}

#ifdef CONFIG_WEBUI_FAILSAFE_UBI
void ubi_register_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/ubi.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/ubi_js.js", &js_handler, NULL);
	httpd_register_uri_handler(inst, "/ubi/info", &ubi_info_handler, NULL);
	httpd_register_uri_handler(inst, "/ubi/volumes", &ubi_volumes_handler, NULL);
	httpd_register_uri_handler(inst, "/ubi/attach", &ubi_attach_handler, NULL);
	httpd_register_uri_handler(inst, "/ubi/detach", &ubi_detach_handler, NULL);
	httpd_register_uri_handler(inst, "/ubi/create", &ubi_create_vol_handler, NULL);
	httpd_register_uri_handler(inst, "/ubi/remove", &ubi_remove_vol_handler, NULL);
	httpd_register_uri_handler(inst, "/ubi/rename", &ubi_rename_vol_handler, NULL);
	httpd_register_uri_handler(inst, "/ubi/mtd_list", &ubi_mtd_list_handler, NULL);
	httpd_register_uri_handler(inst, "/ubi/backup", &ubi_backup_handler, NULL);
}
#endif
