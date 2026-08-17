/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * Raw NAND page access helpers for failsafe backup/flash.
 * Reads/writes pages with OOB data interleaved (MTD_OPS_RAW mode).
 */

#ifndef _FAILSAFE_MODULES_NAND_RAW_H_
#define _FAILSAFE_MODULES_NAND_RAW_H_

#include <linux/types.h>
#include <linux/mtd/mtd.h>

#ifdef CONFIG_MTD

/**
 * nand_raw_page_size() - page size including OOB
 * @mtd: MTD device (must be NAND type)
 * Returns writesize + oobsize, or 0 if not NAND.
 */
static inline size_t nand_raw_page_size(struct mtd_info *mtd)
{
	if (!mtd || (mtd->type != MTD_NANDFLASH && mtd->type != MTD_MLCNANDFLASH))
		return 0;
	return mtd->writesize + mtd->oobsize;
}

/**
 * nand_raw_total_pages() - total number of pages on the device
 * @mtd: MTD device
 * Returns mtd->size / mtd->writesize, or 0 if not NAND.
 */
static inline u64 nand_raw_total_pages(struct mtd_info *mtd)
{
	if (!mtd || !mtd->writesize)
		return 0;
	return mtd->size / mtd->writesize;
}

/**
 * nand_raw_total_size() - total raw output size (data+OOB for all pages)
 * @mtd: MTD device
 * Returns total_pages * (writesize + oobsize), or 0 if not NAND.
 */
static inline u64 nand_raw_total_size(struct mtd_info *mtd)
{
	u64 pages = nand_raw_total_pages(mtd);
	size_t rps = nand_raw_page_size(mtd);

	if (!pages || !rps)
		return 0;
	return pages * rps;
}

/**
 * nand_raw_read_pages() - read pages in raw mode (data+OOB interleaved)
 * @mtd:       MTD device (NAND)
 * @page_start: first page number (0-based)
 * @page_count: number of pages to read
 * @buf:       output buffer (must hold page_count * raw_page_size)
 * @buf_size:  buffer size in bytes
 * @read_size: receives actual bytes written to buf
 *
 * Each page is laid out as [writesize bytes data][oobsize bytes OOB].
 * Bad blocks are filled with 0xff.
 *
 * Returns 0 on success, -errno on failure.
 */
int nand_raw_read_pages(struct mtd_info *mtd, u64 page_start, u64 page_count,
			void *buf, size_t buf_size, size_t *read_size);

/**
 * nand_raw_write_pages() - write pages in raw mode (data+OOB interleaved)
 * @mtd:          MTD device (NAND)
 * @page_start:   first page number (0-based)
 * @page_count:   number of pages to write
 * @buf:          input buffer (data+OOB interleaved, same format as read)
 * @buf_size:     buffer size in bytes
 * @written_size: receives actual bytes consumed from buf
 *
 * Bad blocks are skipped (data for those pages is consumed but not written).
 * Blocks must be erased before calling this function.
 *
 * Returns 0 on success, -errno on failure.
 */
int nand_raw_write_pages(struct mtd_info *mtd, u64 page_start, u64 page_count,
			 const void *buf, size_t buf_size,
			 size_t *written_size);

/**
 * nand_raw_erase_blocks() - erase all blocks covering a page range
 * @mtd:        MTD device (NAND)
 * @page_start: first page number
 * @page_count: number of pages
 *
 * Erases all blocks in the range.  Bad blocks are skipped.
 *
 * Returns 0 on success, -errno on failure.
 */
int nand_raw_erase_blocks(struct mtd_info *mtd, u64 page_start, u64 page_count);

/**
 * nand_raw_is_nand() - check if an MTD device is NAND type
 * @mtd: MTD device
 */
static inline bool nand_raw_is_nand(struct mtd_info *mtd)
{
	return mtd && (mtd->type == MTD_NANDFLASH || mtd->type == MTD_MLCNANDFLASH);
}

#endif /* CONFIG_MTD */
#endif /* _FAILSAFE_MODULES_NAND_RAW_H_ */
