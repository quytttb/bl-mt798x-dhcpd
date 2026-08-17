/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * Raw NAND page I/O — uses mtd_read_oob / mtd_write_oob with
 * MTD_OPS_RAW to read/write full pages including OOB spare area.
 */

#include <errno.h>
#include <malloc.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <mtd/mtd-abi.h>
#include <failsafe/nand_raw.h>

#ifdef CONFIG_MTD

int nand_raw_read_pages(struct mtd_info *mtd, u64 page_start, u64 page_count,
			void *buf, size_t buf_size, size_t *read_size)
{
	size_t rps, esize;
	u64 i;
	size_t offset = 0;
	int ret = 0;

	if (read_size)
		*read_size = 0;

	if (!mtd || !buf)
		return -EINVAL;
	if (!nand_raw_is_nand(mtd))
		return -ENODEV;

	rps = nand_raw_page_size(mtd);
	if (!rps)
		return -EINVAL;

	esize = mtd->erasesize;
	if (!esize)
		return -EINVAL;

	if (buf_size < page_count * rps)
		return -ENOSPC;

	for (i = 0; i < page_count; i++) {
		u64 page = page_start + i;
		loff_t block_addr = page * mtd->writesize;

		block_addr &= ~(loff_t)(esize - 1);

		/* Check bad block */
		if (mtd_block_isbad(mtd, block_addr)) {
			memset((u8 *)buf + offset, 0xff, rps);
			offset += rps;
			continue;
		}

		{
			struct mtd_oob_ops ops = {
				.mode   = MTD_OPS_RAW,
				.len    = mtd->writesize,
				.ooblen = mtd->oobsize,
				.datbuf = (u8 *)buf + offset,
				.oobbuf = (u8 *)buf + offset + mtd->writesize,
			};

			ret = mtd_read_oob(mtd, page * mtd->writesize, &ops);
			if (ret) {
				printf("nand_raw_read: page %llu read failed (%d)\n",
				       (unsigned long long)page, ret);
				break;
			}

			if (ops.retlen != mtd->writesize ||
			    ops.oobretlen != mtd->oobsize) {
				printf("nand_raw_read: page %llu short read (data=%zu/%u, oob=%zu/%u)\n",
				       (unsigned long long)page,
				       ops.retlen, mtd->writesize,
				       ops.oobretlen, mtd->oobsize);
				ret = -EIO;
				break;
			}
		}

		offset += rps;
	}

	if (read_size)
		*read_size = offset;

	return ret;
}

int nand_raw_write_pages(struct mtd_info *mtd, u64 page_start, u64 page_count,
			 const void *buf, size_t buf_size,
			 size_t *written_size)
{
	size_t rps, esize;
	u64 i;
	size_t offset = 0;
	int ret = 0;

	if (written_size)
		*written_size = 0;

	if (!mtd || !buf)
		return -EINVAL;
	if (!nand_raw_is_nand(mtd))
		return -ENODEV;

	rps = nand_raw_page_size(mtd);
	if (!rps)
		return -EINVAL;

	esize = mtd->erasesize;
	if (!esize)
		return -EINVAL;

	if (buf_size < page_count * rps)
		return -ENOSPC;

	for (i = 0; i < page_count; i++) {
		u64 page = page_start + i;
		loff_t block_addr = page * mtd->writesize;

		block_addr &= ~(loff_t)(esize - 1);

		/* Skip bad blocks */
		if (mtd_block_isbad(mtd, block_addr)) {
			offset += rps;
			continue;
		}

		{
			struct mtd_oob_ops ops = {
				.mode   = MTD_OPS_RAW,
				.len    = mtd->writesize,
				.ooblen = mtd->oobsize,
				.datbuf = (u8 *)buf + offset,
				.oobbuf = (u8 *)buf + offset + mtd->writesize,
			};

			ret = mtd_write_oob(mtd, page * mtd->writesize, &ops);
			if (ret) {
				printf("nand_raw_write: page %llu write failed (%d)\n",
				       (unsigned long long)page, ret);
				break;
			}

			if (ops.retlen != mtd->writesize ||
			    ops.oobretlen != mtd->oobsize) {
				printf("nand_raw_write: page %llu short write (data=%zu/%u, oob=%zu/%u)\n",
				       (unsigned long long)page,
				       ops.retlen, mtd->writesize,
				       ops.oobretlen, mtd->oobsize);
				ret = -EIO;
				break;
			}
		}

		offset += rps;
	}

	if (written_size)
		*written_size = offset;

	return ret;
}

int nand_raw_erase_blocks(struct mtd_info *mtd, u64 page_start, u64 page_count)
{
	u64 start_byte, end_byte, blk;
	size_t esize;
	int ret;

	if (!mtd)
		return -EINVAL;
	if (!nand_raw_is_nand(mtd))
		return -ENODEV;

	esize = mtd->erasesize;
	if (!esize)
		return -EINVAL;

	start_byte = page_start * mtd->writesize;
	end_byte   = (page_start + page_count) * mtd->writesize;

	/* Align to block boundaries */
	start_byte &= ~(u64)(esize - 1);
	end_byte = (end_byte + esize - 1) & ~(u64)(esize - 1);

	if (end_byte > mtd->size)
		end_byte = (mtd->size + esize - 1) & ~(u64)(esize - 1);

	for (blk = start_byte; blk < end_byte; blk += esize) {
		struct erase_info ei;

		if (mtd_block_isbad(mtd, blk))
			continue;

		memset(&ei, 0, sizeof(ei));
		ei.mtd  = mtd;
		ei.addr = blk;
		ei.len  = esize;

		ret = mtd_erase(mtd, &ei);
		if (ret) {
			printf("nand_raw_erase: block 0x%llx erase failed (%d)\n",
			       (unsigned long long)blk, ret);
			return ret;
		}
	}

	return 0;
}

#endif /* CONFIG_MTD */
