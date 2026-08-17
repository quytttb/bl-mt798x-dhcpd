/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe flash editor module
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
#include <linux/mtd/nand.h>
#include <linux/mtd/spi-nor.h>
#include <linux/mtd/spinand.h>
#include "../../board/mediatek/common/mtd_helper.h"
#endif

#ifdef CONFIG_MTK_BOOTMENU_MMC
#include "../../board/mediatek/common/mmc_helper.h"
#endif

#ifdef CONFIG_PARTITIONS
#include <part.h>
#endif

#include <failsafe/internal.h>

#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
#include <failsafe/nand_raw.h>
#endif

/* Max bytes to read per /flash/read request in chunked mode.
 * Each chunk is hex-encoded (3× expansion) + JSON overhead,
 * so 256 KiB → ~770 KiB JSON, safe for U‑Boot's heap.
 */
#define FLASH_READ_CHUNK  (256 * 1024)

/* ------------------------------------------------------------------ */
/*  Static helpers                                                     */
/* ------------------------------------------------------------------ */

static int flash_parse_start_end(const char *start_s, const char *end_s,
	u64 *start, u64 *end)
{
	if (!start_s || !end_s || !start || !end)
		return -EINVAL;

	if (parse_u64_len(start_s, start))
		return -EINVAL;
	if (parse_u64_len(end_s, end))
		return -EINVAL;
	if (*end <= *start)
		return -ERANGE;

	return 0;
}

static int flash_parse_hex(const char *in, u8 **out, size_t *out_len)
{
	size_t digits = 0, i = 0, o = 0, bytes;
	u8 *buf;
	int high = -1;

	if (!in || !out || !out_len)
		return -EINVAL;

	*out = NULL;
	*out_len = 0;

	while (in[i]) {
		if (in[i] == '0' && (in[i + 1] == 'x' || in[i + 1] == 'X')) {
			i += 2;
			continue;
		}
		if (isxdigit((unsigned char)in[i]))
			digits++;
		i++;
	}

	if (!digits || (digits & 1))
		return -EINVAL;

	bytes = digits / 2;

	buf = malloc(bytes);
	if (!buf)
		return -ENOMEM;

	for (i = 0; in[i]; i++) {
		int v;

		if (in[i] == '0' && (in[i + 1] == 'x' || in[i + 1] == 'X')) {
			i++;
			high = -1;
			continue;
		}

		if (!isxdigit((unsigned char)in[i]))
			continue;

		if (in[i] >= '0' && in[i] <= '9')
			v = in[i] - '0';
		else if (in[i] >= 'a' && in[i] <= 'f')
			v = in[i] - 'a' + 10;
		else
			v = in[i] - 'A' + 10;

		if (high < 0) {
			high = v;
		} else {
			buf[o++] = (u8)((high << 4) | v);
			high = -1;
		}
	}

	if (o != bytes) {
		free(buf);
		return -EINVAL;
	}

	*out = buf;
	*out_len = bytes;
	return 0;
}

static char *flash_hex_dump(const u8 *data, size_t len, size_t *out_len)
{
	size_t i, cap;
	char *out;
	static const char hex[] = "0123456789abcdef";

	if (!data || !out_len)
		return NULL;

	cap = len * 3 + 8;
	out = malloc(cap);
	if (!out)
		return NULL;

	for (i = 0; i < len; i++) {
		out[i * 3] = hex[(data[i] >> 4) & 0xf];
		out[i * 3 + 1] = hex[data[i] & 0xf];
		out[i * 3 + 2] = (i + 1 == len) ? '\0' : ' ';
	}

	*out_len = strlen(out);
	return out;
}

#ifdef CONFIG_MTD
static int flash_mtd_update_range(struct mtd_info *mtd, u64 start,
	const u8 *data, size_t len)
{
	u64 block_start, block_end, blk;
	size_t erase_sz;
	u8 *blkbuf = NULL;
	int ret = 0;

	if (!mtd || !data || !len)
		return -EINVAL;

	erase_sz = mtd->erasesize;
	if (!erase_sz)
		return -EINVAL;

	block_start = start & ~((u64)erase_sz - 1);
	block_end = (start + len + erase_sz - 1) & ~((u64)erase_sz - 1);

	blkbuf = malloc(erase_sz);
	if (!blkbuf)
		return -ENOMEM;

	for (blk = block_start; blk < block_end; blk += erase_sz) {
		size_t readsz = 0;
		u64 data_start, data_end;
		size_t copy_len;

		ret = mtd_read_skip_bad(mtd, blk, erase_sz, erase_sz, &readsz, blkbuf);
		if (ret || readsz != erase_sz) {
			ret = ret ? ret : -EIO;
			goto out;
		}

		data_start = max(start, blk);
		data_end = min(start + (u64)len, blk + (u64)erase_sz);
		copy_len = (size_t)(data_end - data_start);
		if (copy_len) {
			memcpy(blkbuf + (data_start - blk),
				data + (size_t)(data_start - start),
				copy_len);
		}

		ret = mtd_erase_skip_bad(mtd, blk, erase_sz, erase_sz,
			NULL, NULL, mtd->name, true);
		if (ret)
			goto out;

		ret = mtd_write_skip_bad(mtd, blk, erase_sz, erase_sz,
			NULL, blkbuf, true);
		if (ret)
			goto out;
	}

out:
	free(blkbuf);
	return ret;
}

static int flash_mtd_restore_range(struct mtd_info *mtd, u64 start,
	const u8 *data, size_t len)
{
	u64 erased = 0;
	size_t written = 0;
	int ret;

	if (!mtd || !data || !len)
		return -EINVAL;

	ret = mtd_erase_skip_bad(mtd, start, len, mtd->size - start,
		&erased, NULL, mtd->name, true);
	if (ret)
		return ret;

	ret = mtd_write_skip_bad(mtd, start, len, mtd->size - start,
		&written, data, true);
	if (ret)
		return ret;

	if (written != len)
		return -EIO;

	return 0;
}

static int flash_mtd_erase_range(struct mtd_info *mtd, u64 start, u64 len)
{
	u64 block_start, block_end, blk;
	size_t erase_sz;
	u8 *blkbuf = NULL;
	int ret = 0;

	if (!mtd || !len)
		return -EINVAL;

	erase_sz = mtd->erasesize;
	if (!erase_sz)
		return -EINVAL;

	block_start = start & ~((u64)erase_sz - 1);
	block_end = (start + len + erase_sz - 1) & ~((u64)erase_sz - 1);

	for (blk = block_start; blk < block_end; blk += erase_sz) {
		u64 data_start = max(start, blk);
		u64 data_end = min(start + (u64)len, blk + (u64)erase_sz);
		bool full_block = (data_start == blk) && (data_end == blk + (u64)erase_sz);

		if (full_block) {
			ret = mtd_erase_skip_bad(mtd, blk, erase_sz, erase_sz,
				NULL, NULL, mtd->name, true);
			if (ret)
				goto out;
			continue;
		}

		if (!blkbuf) {
			blkbuf = malloc(erase_sz);
			if (!blkbuf) {
				ret = -ENOMEM;
				goto out;
			}
		}

		{
			size_t readsz = 0;
			size_t fill_len = (size_t)(data_end - data_start);

			ret = mtd_read_skip_bad(mtd, blk, erase_sz, erase_sz, &readsz, blkbuf);
			if (ret || readsz != erase_sz) {
				ret = ret ? ret : -EIO;
				goto out;
			}

			memset(blkbuf + (size_t)(data_start - blk), 0xff, fill_len);

			ret = mtd_erase_skip_bad(mtd, blk, erase_sz, erase_sz,
				NULL, NULL, mtd->name, true);
			if (ret)
				goto out;

			ret = mtd_write_skip_bad(mtd, blk, erase_sz, erase_sz,
				NULL, blkbuf, true);
			if (ret)
				goto out;
		}
	}

out:
	free(blkbuf);
	return ret;
}
#endif

static const char *flash_find_last_before(const char *s, const char *needle,
	const char *limit)
{
	const char *p = s;
	const char *last = NULL;

	if (!s || !needle || !limit || limit <= s)
		return NULL;

	while ((p = strstr(p, needle)) != NULL) {
		if (p >= limit)
			break;
		last = p;
		p++;
	}

	return last;
}

static int flash_parse_backup_filename(const char *filename,
	char *storage, size_t storage_sz,
	char *target, size_t target_sz,
	u64 *start, u64 *end)
{
	const char *range, *dash, *stype_mtd, *stype_mmc, *stype;
	char *range_end = NULL;
	char tmp[128];
	size_t seg_len;

	if (!filename || !storage || !target || !start || !end)
		return -EINVAL;

	range = strstr(filename, "_0x");
	if (!range)
		return -EINVAL;
	
	dash = strstr(range, "-0x");
	if (!dash)
		return -EINVAL;

	*start = simple_strtoull(range + 1, &range_end, 0);
	if (!range_end || range_end <= range + 1)
		return -EINVAL;

	*end = simple_strtoull(dash + 1, &range_end, 0);
	if (!range_end || range_end <= dash + 1)
		return -EINVAL;

	if (*end <= *start)
		return -ERANGE;

	stype_mtd = flash_find_last_before(filename, "_mtd_", range);
	stype_mmc = flash_find_last_before(filename, "_mmc_", range);
	if (stype_mtd && stype_mmc)
		stype = (stype_mtd > stype_mmc) ? stype_mtd : stype_mmc;
	else
		stype = stype_mtd ? stype_mtd : stype_mmc;

	if (!stype)
		return -EINVAL;

	if (stype == stype_mtd)
		strlcpy(storage, "mtd", storage_sz);
	else
		strlcpy(storage, "mmc", storage_sz);

	stype += (stype == stype_mtd) ? 5 : 5;
	seg_len = (size_t)(range - stype);
	if (!seg_len || seg_len >= sizeof(tmp))
		return -EINVAL;

	memcpy(tmp, stype, seg_len);
	tmp[seg_len] = '\0';

	{
		char *last = strrchr(tmp, '_');
		const char *name = last ? last + 1 : tmp;
		if (!name || !name[0])
			return -EINVAL;
		if (strlen(name) >= target_sz)
			return -E2BIG;
		strlcpy(target, name, target_sz);
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/*  Flash handler                                                      */
/* ------------------------------------------------------------------ */

void flash_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	const char *op = NULL;
	char *json = NULL;
	char storage_sel[16] = "auto";
	char target_name[64] = "";
	u64 start = 0, end = 0;
	int ret;

	if (status == HTTP_CB_CLOSED) {
		free(response->session_data);
		response->session_data = NULL;
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_json(response, 405,
			"{\"ok\":false,\"error\":\"method\"}\n");
		return;
	}

	{
		struct httpd_form_value *opv = httpd_request_find_value(request, "op");
		if (opv && opv->data)
			op = opv->data;
	}

	if (!op) {
		const char *uri;

		if (request->urih && request->urih->uri) {
			uri = request->urih->uri;
			if (!strcmp(uri, "/flash/read"))
				op = "read";
			else if (!strcmp(uri, "/flash/write"))
				op = "write";
			else if (!strcmp(uri, "/flash/restore"))
				op = "restore";
			else if (!strcmp(uri, "/flash/erase"))
				op = "erase";
		}
	}

	if (!op) {
		failsafe_http_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"no_op\"}\n");
		return;
	}

	if (!strcmp(op, "read")) {
		struct httpd_form_value *startv, *endv, *chunkv;
#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
		struct httpd_form_value *rawv;
#endif
		struct flash_target tgt;
		u8 *buf = NULL;
		char *hex = NULL;
		size_t len, read_len, hex_len = 0;
		u64 read_start;
		int chunk = -1;
		bool has_chunk;
#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
		bool raw_mode = false;
#endif

		ret = flash_parse_storage_target(request, storage_sel,
						  sizeof(storage_sel),
						  target_name, sizeof(target_name));
		if (ret)
			goto bad_req;

		startv = httpd_request_find_value(request, "start");
		endv = httpd_request_find_value(request, "end");

		if (!startv || !endv || !startv->data || !endv->data)
			goto bad_req;

		ret = flash_parse_start_end(startv->data, endv->data, &start, &end);
		if (ret)
			goto bad_range;

		len = (size_t)(end - start);
		if (!len)
			goto bad_req;

		chunkv = httpd_request_find_value(request, "chunk");
		has_chunk = chunkv && chunkv->data && chunkv->data[0];

		if (has_chunk)
			chunk = simple_strtoul(chunkv->data, NULL, 0);

		if (has_chunk) {
			read_start = start + (u64)chunk * FLASH_READ_CHUNK;
			read_len = (size_t)min((u64)FLASH_READ_CHUNK,
					       end - read_start);
		} else {
			read_start = start;
			read_len = len;
		}

		if (!read_len)
			goto bad_req;

#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
		rawv = httpd_request_find_value(request, "raw");
		if (rawv && rawv->data && !strcmp(rawv->data, "1"))
			raw_mode = true;
#endif

		ret = flash_open_target(storage_sel, target_name, &tgt);
		if (ret)
			goto bad_target;

		if (read_start + read_len > tgt.size) {
			flash_close_target(&tgt);
			goto bad_range;
		}

#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
		if (raw_mode && tgt.src == FAILSAFE_SRC_MTD) {
			size_t rps = nand_raw_page_size(tgt.mtd);
			u64 first_page, pages_to_read;
			size_t actual;

			if (!rps || !nand_raw_is_nand(tgt.mtd)) {
				flash_close_target(&tgt);
				goto bad_req;
			}

			first_page = read_start / rps;
			pages_to_read = (read_len + rps - 1) / rps;
			read_len = (size_t)(pages_to_read * rps);

			buf = malloc(read_len);
			if (!buf) {
				flash_close_target(&tgt);
				goto oom;
			}

			ret = nand_raw_read_pages(tgt.mtd, first_page,
				pages_to_read, buf, read_len, &actual);
			if (ret) {
				free(buf);
				flash_close_target(&tgt);
				goto io_err;
			}
			read_len = actual;
		} else
#endif
		{
			buf = malloc(read_len);
			if (!buf) {
				flash_close_target(&tgt);
				goto oom;
			}

			if (tgt.src == FAILSAFE_SRC_MTD) {
#ifdef CONFIG_MTD
				size_t readsz = 0;
				ret = mtd_read_skip_bad(tgt.mtd, read_start, read_len,
					tgt.mtd->size - read_start, &readsz, buf);
				if (ret || readsz != read_len) {
					free(buf);
					flash_close_target(&tgt);
					goto io_err;
				}
#else
				free(buf);
				flash_close_target(&tgt);
				goto bad_target;
#endif
			} else {
#ifdef CONFIG_MTK_BOOTMENU_MMC
				ret = mmc_read_generic(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0,
					tgt.base + read_start, buf, read_len);
				if (ret) {
					free(buf);
					flash_close_target(&tgt);
					goto io_err;
				}
#else
				free(buf);
				flash_close_target(&tgt);
				goto bad_target;
#endif
			}
		}

		hex = flash_hex_dump(buf, read_len, &hex_len);
		free(buf);
		flash_close_target(&tgt);
		if (!hex)
			goto oom;

		json = malloc(hex_len + 320);
		if (!json) {
			free(hex);
			goto oom;
		}

		if (has_chunk) {
			size_t total_chunks = (len + FLASH_READ_CHUNK - 1)
					      / FLASH_READ_CHUNK;
			snprintf(json, hex_len + 320,
				"{\"ok\":true,\"start\":\"0x%llx\",\"end\":\"0x%llx\","
				"\"size\":%zu,\"chunk\":%d,\"chunk_total\":%zu,"
				"\"chunk_offset\":\"0x%llx\",\"chunk_size\":%zu,"
				"\"data\":\"%s\"}\n",
				(unsigned long long)start,
				(unsigned long long)end,
				len, chunk, total_chunks,
				(unsigned long long)read_start, read_len,
				hex);
		} else {
			snprintf(json, hex_len + 320,
				"{\"ok\":true,\"start\":\"0x%llx\",\"end\":\"0x%llx\","
				"\"size\":%zu,\"data\":\"%s\"}\n",
				(unsigned long long)start,
				(unsigned long long)end,
				len, hex);
		}
		free(hex);

		failsafe_http_reply_json_alloc(response, 200, json, json);
		return;
	}

	if (!strcmp(op, "write")) {
		struct httpd_form_value *startv, *datav;
#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
		struct httpd_form_value *rawv;
#endif
		struct flash_target tgt;
		u8 *buf = NULL;
		size_t len = 0;
#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
		bool raw_mode = false;
#endif

		ret = flash_parse_storage_target(request, storage_sel,
						  sizeof(storage_sel),
						  target_name, sizeof(target_name));
		if (ret)
			goto bad_req;

		startv = httpd_request_find_value(request, "start");
		datav = httpd_request_find_value(request, "data");

		if (!startv || !startv->data || !datav || !datav->data)
			goto bad_req;

#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
		rawv = httpd_request_find_value(request, "raw");
		if (rawv && rawv->data && !strcmp(rawv->data, "1"))
			raw_mode = true;
#endif

		if (parse_u64_len(startv->data, &start))
			goto bad_range;

		ret = flash_parse_hex(datav->data, &buf, &len);
		if (ret)
			goto bad_req;

		ret = flash_open_target(storage_sel, target_name, &tgt);
		if (ret) {
			free(buf);
			goto bad_target;
		}

		if (start + len > tgt.size) {
			flash_close_target(&tgt);
			free(buf);
			goto bad_range;
		}

		if (tgt.src == FAILSAFE_SRC_MTD) {
#ifdef CONFIG_MTD
#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
			if (raw_mode && nand_raw_is_nand(tgt.mtd)) {
				size_t rps = nand_raw_page_size(tgt.mtd);
				u64 first_page, pages;

				if (!rps) {
					ret = -EINVAL;
				} else {
					first_page = start / rps;
					pages = (len + rps - 1) / rps;

					ret = nand_raw_erase_blocks(tgt.mtd,
						first_page, pages);
					if (!ret) {
						size_t wr = 0;
						ret = nand_raw_write_pages(tgt.mtd,
							first_page, pages,
							buf, len, &wr);
						len = wr;
					}
				}
			} else
#endif
			{
				ret = flash_mtd_update_range(tgt.mtd, start, buf, len);
			}
#else
			ret = -ENODEV;
#endif
		} else {
#ifdef CONFIG_MTK_BOOTMENU_MMC
			ret = mmc_write_generic(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0,
				tgt.base + start, tgt.size - start, buf, len, true);
#else
			ret = -ENODEV;
#endif
		}

		flash_close_target(&tgt);
		free(buf);

		if (ret)
			goto io_err;

		json = malloc(96);
		if (!json)
			goto oom;
		snprintf(json, 96, "{\"ok\":true,\"written\":%zu}\n", len);
		failsafe_http_reply_json_alloc(response, 200, json, json);
		return;
	}

	if (!strcmp(op, "restore")) {
		struct httpd_form_value *fw, *startv, *endv;
		struct flash_target tgt;
		char storage_from_name[16] = "";
		char target_from_name[64] = "";
		u64 name_start = 0, name_end = 0;
		size_t len = 0;
#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
		bool raw_restore = false;
#endif

		fw = httpd_request_find_value(request, "backup");
		if (!fw)
			fw = httpd_request_find_value(request, "file");

		if (!fw || !fw->data || !fw->size)
			goto bad_req;

		ret = fw->filename ? flash_parse_backup_filename(fw->filename,
			storage_from_name, sizeof(storage_from_name),
			target_from_name, sizeof(target_from_name),
			&name_start, &name_end) : -EINVAL;

		if (!ret) {
			strlcpy(storage_sel, storage_from_name, sizeof(storage_sel));
			strlcpy(target_name, target_from_name, sizeof(target_name));
			start = name_start;
			end = name_end;

#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
			/* Auto-detect OOB backup by filename suffix */
			if (fw->filename && strstr(fw->filename, "_oob"))
				raw_restore = true;
#endif
		} else {
			startv = httpd_request_find_value(request, "start");
			endv = httpd_request_find_value(request, "end");

			ret = flash_parse_storage_target(request, storage_sel,
							  sizeof(storage_sel),
							  target_name, sizeof(target_name));
			if (ret)
				goto bad_req;

			if (!startv || !endv || !startv->data || !endv->data)
				goto bad_req;

			if (flash_parse_start_end(startv->data, endv->data, &start, &end))
				goto bad_range;
		}

		len = fw->size;
		if (end <= start || (u64)len != (end - start))
			goto bad_range;

		ret = flash_open_target(storage_sel, target_name, &tgt);
		if (ret)
			goto bad_target;

		if (end > tgt.size) {
			flash_close_target(&tgt);
			goto bad_range;
		}

		if (tgt.src == FAILSAFE_SRC_MTD) {
#ifdef CONFIG_MTD
#ifdef CONFIG_WEBUI_FAILSAFE_NAND_RAW
			if (raw_restore && nand_raw_is_nand(tgt.mtd)) {
				size_t rps = nand_raw_page_size(tgt.mtd);
				u64 first_page, pages;

				if (!rps) {
					ret = -EINVAL;
				} else {
					first_page = start / rps;
					pages = (len + rps - 1) / rps;

					ret = nand_raw_erase_blocks(tgt.mtd,
						first_page, pages);
					if (!ret) {
						size_t wr = 0;
						ret = nand_raw_write_pages(tgt.mtd,
							first_page, pages,
							fw->data, len, &wr);
						len = wr;
					}
				}
			} else
#endif
			{
				ret = flash_mtd_restore_range(tgt.mtd, start, fw->data, len);
			}
#else
			ret = -ENODEV;
#endif
		} else {
#ifdef CONFIG_MTK_BOOTMENU_MMC
			ret = mmc_write_generic(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0,
				tgt.base + start, tgt.size - start, fw->data, len, true);
#else
			ret = -ENODEV;
#endif
		}

		flash_close_target(&tgt);

		if (ret)
			goto io_err;

		json = malloc(160);
		if (!json)
			goto oom;
		snprintf(json, 160,
			 "{\"ok\":true,\"restored\":%zu,\"alert\":\"Backup restore completed.\"}\n",
			 len);
		failsafe_http_reply_json_alloc(response, 200, json, json);
		return;
	}

	if (!strcmp(op, "erase")) {
		struct httpd_form_value *startv, *endv;
		struct flash_target tgt;
		u64 len;

		ret = flash_parse_storage_target(request, storage_sel,
						  sizeof(storage_sel),
						  target_name, sizeof(target_name));
		if (ret)
			goto bad_req;

		ret = flash_open_target(storage_sel, target_name, &tgt);
		if (ret)
			goto bad_target;

		startv = httpd_request_find_value(request, "start");
		endv = httpd_request_find_value(request, "end");

		if (startv && endv && startv->data && startv->data[0] &&
		    endv && endv->data && endv->data[0]) {
			if (flash_parse_start_end(startv->data, endv->data, &start, &end)) {
				flash_close_target(&tgt);
				goto bad_range;
			}
		} else if ((!startv || !startv->data || !startv->data[0]) &&
			   (!endv || !endv->data || !endv->data[0])) {
			start = 0;
			end = tgt.size;
		} else {
			flash_close_target(&tgt);
			goto bad_range;
		}

		if (start >= end || end > tgt.size) {
			flash_close_target(&tgt);
			goto bad_range;
		}

		len = end - start;

		if (tgt.src == FAILSAFE_SRC_MTD) {
#ifdef CONFIG_MTD
			ret = flash_mtd_erase_range(tgt.mtd, start, len);
#else
			ret = -ENODEV;
#endif
		} else {
#ifdef CONFIG_MTK_BOOTMENU_MMC
			ret = mmc_erase_generic(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0,
				tgt.base + start, len);
#else
			ret = -ENODEV;
#endif
		}

		flash_close_target(&tgt);

		if (ret)
			goto io_err;

		json = malloc(160);
		if (!json)
			goto oom;
		snprintf(json, 160,
			"{\"ok\":true,\"erased\":%llu,\"start\":\"0x%llx\",\"end\":\"0x%llx\"}\n",
			(unsigned long long)len,
			(unsigned long long)start,
			(unsigned long long)end);
		failsafe_http_reply_json_alloc(response, 200, json, json);
		return;
	}

bad_req:
	failsafe_http_reply_json(response, 400,
		"{\"ok\":false,\"error\":\"bad_request\"}\n");
	return;
bad_target:
	failsafe_http_reply_json(response, 404,
		"{\"ok\":false,\"error\":\"target_not_found\"}\n");
	return;
bad_range:
	failsafe_http_reply_json(response, 400,
		"{\"ok\":false,\"error\":\"bad_range\"}\n");
	return;
oom:
	failsafe_http_reply_json(response, 500,
		"{\"ok\":false,\"error\":\"oom\"}\n");
	return;
io_err:
	failsafe_http_reply_json(response, 500,
		"{\"ok\":false,\"error\":\"io\"}\n");
	return;
}

#ifdef CONFIG_WEBUI_FAILSAFE_FLASH
void flash_register_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/flash.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/flash_js.js", &js_handler, NULL);
	httpd_register_uri_handler(inst, "/flash/read", &flash_handler, NULL);
	httpd_register_uri_handler(inst, "/flash/write", &flash_handler, NULL);
	httpd_register_uri_handler(inst, "/flash/erase", &flash_handler, NULL);
	httpd_register_uri_handler(inst, "/flash/restore", &flash_handler, NULL);
}
#endif
