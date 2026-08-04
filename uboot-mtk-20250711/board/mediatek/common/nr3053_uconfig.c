// SPDX-License-Identifier: GPL-2.0+
/*
 * One-time Keenetic U-Config provisioning for Viettel NR3053.
 *
 * Identity values are never printed.  Existing valid identities are never
 * replaced.  The recovery copy is committed and verified before the active
 * copy so an interrupted first provision still has a recoverable slot.
 */

#include <asm/unaligned.h>
#include <command.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <malloc.h>
#include <mtd.h>
#include <nr3053_uconfig.h>
#include <u-boot/crc.h>
#include <u-boot/md5.h>
#include <u-boot/sha256.h>

#define NR_FACTORY_NAME		"Factory"
#define NR_UBI_NAME			"ubi"
#define NR_FACTORY_SIZE		0x200000
#define NR_UCONFIG_OFFSET		0x080000
#define NR_UCONFIG_RES_OFFSET		0x7480000
#define NR_UCONFIG_SIZE		NR3053_UCONFIG_IMAGE_SIZE
#define NR_ENV_BLOCK_SIZE		0x020000
#define NR_ENV_DATA_SIZE		(NR_ENV_BLOCK_SIZE - sizeof(u32))
#define NR_HASH_CHUNK_SIZE		0x010000
#define NR_MAC0_OFFSET		0x04
#define NR_MAC1_OFFSET		0x0a

#define NR_COUNTRY			"VN"
#define NR_NDM_HW_ID			"KN-3811"
#define NR_SERVICE_HOST		"127.0.0.1"

static const char nr_derivation_context[] =
	"UNLOCK_ROUTER_VIETTEL/NR3053/U-Config/autoprovision/v2";

static const char *const nr_ctrlsum_fields[] = {
	"servicetag", "servicehost", "servicepass", "wlanssid",
	"wlankey", "wlanwps", "country", "ndmhwid",
};

struct nr_default_entry {
	const char *key;
	const char *value;
};

static const struct nr_default_entry nr_recovery_defaults[] = {
	{ "baudrate", "115200" },
	{ "bootdelay", "unable to determine a firmware ID (0x%1), %2" },
	{ "bootfile", "KN-3811_recovery.bin" },
	{ "bootfile.bl2", "KN-3811_bl2.img" },
	{ "bootfile.fip", "KN-3811_fip.bin" },
	{ "bootfile.e2p", "KN-3811_e2p.bin" },
	{ "bootfile.bl31", "KN-3811_bl31.img" },
	{ "bootfile.bl33", "KN-3811_uboot.bin" },
	{ "ethaddr", "00:AA:BB:CC:DD:10" },
	{ "ipaddr", "192.168.1.1" },
	{ "netmask", "255.255.255.0" },
	{ "netretry", "yes" },
	{ "serverip", "192.168.1.2" },
};

struct nr_identity {
	char sernumb[14];
	char servicetag[16];
	char servicepass[21];
	char wlanssid[16];
	char wlankey[21];
	char wlanwps[9];
	char ctrlsum[33];
};

struct nr_env_value {
	const u8 *data;
	size_t len;
};

static bool nr_mac_valid(const u8 mac[6])
{
	static const u8 zero[6];
	static const u8 ff[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	return !(mac[0] & 1) && memcmp(mac, zero, sizeof(zero)) &&
	       memcmp(mac, ff, sizeof(ff));
}

static int nr_mtd_read_exact(struct mtd_info *mtd, loff_t offset,
			     size_t length, void *buffer)
{
	size_t retlen = 0;
	int ret;

	if (offset < 0 || offset + length > mtd->size)
		return -ERANGE;

	ret = mtd_read(mtd, offset, length, &retlen, buffer);
	if (ret < 0 || retlen != length)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int nr_read_factory_macs(struct mtd_info *factory, u8 macs[2][6])
{
	u8 raw[12];
	int ret;

	ret = nr_mtd_read_exact(factory, NR_MAC0_OFFSET, sizeof(raw), raw);
	if (ret)
		return ret;

	memcpy(macs[0], raw, 6);
	memcpy(macs[1], raw + (NR_MAC1_OFFSET - NR_MAC0_OFFSET), 6);
	memset(raw, 0, sizeof(raw));

	if (!nr_mac_valid(macs[0]) || !nr_mac_valid(macs[1]) ||
	    !memcmp(macs[0], macs[1], 6))
		return -EINVAL;

	return 0;
}

static int nr_hash_factory_range(struct mtd_info *factory,
				 loff_t start, size_t length,
				 u8 *scratch, sha256_context *ctx)
{
	while (length) {
		size_t chunk = min_t(size_t, length, NR_HASH_CHUNK_SIZE);
		int ret = nr_mtd_read_exact(factory, start, chunk, scratch);

		if (ret)
			return ret;
		sha256_update(ctx, scratch, chunk);
		start += chunk;
		length -= chunk;
	}

	return 0;
}

static int nr_factory_seed(struct mtd_info *factory, u8 seed[SHA256_SUM_LEN])
{
	sha256_context ctx;
	u8 *scratch;
	int ret;

	if (factory->size < NR_FACTORY_SIZE)
		return -ERANGE;

	scratch = malloc(NR_HASH_CHUNK_SIZE);
	if (!scratch)
		return -ENOMEM;

	sha256_starts(&ctx);
	sha256_update(&ctx, (const u8 *)nr_derivation_context,
		      strlen(nr_derivation_context));
	ret = nr_hash_factory_range(factory, 0, NR_UCONFIG_OFFSET,
				    scratch, &ctx);
	if (!ret)
		ret = nr_hash_factory_range(factory,
					    NR_UCONFIG_OFFSET + NR_UCONFIG_SIZE,
					    NR_FACTORY_SIZE - NR_UCONFIG_OFFSET -
					    NR_UCONFIG_SIZE, scratch, &ctx);
	if (!ret)
		sha256_finish(&ctx, seed);

	memset(scratch, 0, NR_HASH_CHUNK_SIZE);
	free(scratch);
	memset(&ctx, 0, sizeof(ctx));
	return ret;
}

static void nr_derive(const u8 seed[SHA256_SUM_LEN], const char *label,
		      u8 digest[SHA256_SUM_LEN])
{
	sha256_context ctx;
	const u8 separator = 0;

	sha256_starts(&ctx);
	sha256_update(&ctx, seed, SHA256_SUM_LEN);
	sha256_update(&ctx, &separator, 1);
	sha256_update(&ctx, (const u8 *)label, strlen(label));
	sha256_finish(&ctx, digest);
	memset(&ctx, 0, sizeof(ctx));
}

static void nr_decimal_string(const u8 seed[SHA256_SUM_LEN], const char *label,
			      char *output, size_t length)
{
	u8 digest[SHA256_SUM_LEN];
	size_t i;

	nr_derive(seed, label, digest);
	for (i = 0; i < length; i++)
		output[i] = '0' + digest[i] % 10;
	if (output[0] == '0')
		output[0] = '1';
	output[length] = '\0';
	memset(digest, 0, sizeof(digest));
}

static void nr_alnum_string(const u8 seed[SHA256_SUM_LEN], const char *label,
			    char *output, size_t length)
{
	static const char alphabet[] =
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	u8 digest[SHA256_SUM_LEN];
	size_t i;

	nr_derive(seed, label, digest);
	for (i = 0; i < length; i++)
		output[i] = alphabet[digest[i] % (sizeof(alphabet) - 1)];
	output[length] = '\0';
	memset(digest, 0, sizeof(digest));
}

static unsigned int nr_wps_checksum(unsigned int pin)
{
	unsigned int accumulator = 0;

	while (pin) {
		accumulator += 3 * (pin % 10);
		pin /= 10;
		accumulator += pin % 10;
		pin /= 10;
	}

	return (10 - accumulator % 10) % 10;
}

static int nr_append(char **cursor, size_t *remaining,
		     const void *data, size_t length)
{
	if (length >= *remaining)
		return -ENOSPC;
	memcpy(*cursor, data, length);
	*cursor += length;
	*remaining -= length;
	return 0;
}

static int nr_append_cstr(char **cursor, size_t *remaining, const char *value)
{
	return nr_append(cursor, remaining, value, strlen(value));
}

static int nr_format_mac(char output[18], const u8 mac[6])
{
	return snprintf(output, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int nr_calculate_ctrlsum(struct nr_identity *identity,
				const u8 macs[2][6])
{
	const char *const values[] = {
		identity->servicetag, NR_SERVICE_HOST, identity->servicepass,
		identity->wlanssid, identity->wlankey, identity->wlanwps,
		NR_COUNTRY, NR_NDM_HW_ID,
	};
	char serialized[384], mac_text[2][18];
	char *cursor = serialized;
	size_t remaining = sizeof(serialized);
	u8 digest[MD5_SUM_LEN];
	size_t i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(values); i++) {
		if (i && nr_append_cstr(&cursor, &remaining, ","))
			return -ENOSPC;
		if (nr_append_cstr(&cursor, &remaining, values[i]))
			return -ENOSPC;
	}
	for (i = 0; i < ARRAY_SIZE(mac_text); i++) {
		nr_format_mac(mac_text[i], macs[i]);
		ret = nr_append_cstr(&cursor, &remaining, ",");
		if (!ret)
			ret = nr_append_cstr(&cursor, &remaining, mac_text[i]);
		if (ret)
			return ret;
	}

	md5_wd((const u8 *)serialized, cursor - serialized, digest,
	       MD5_DEF_CHUNK_SZ);
	for (i = 0; i < MD5_SUM_LEN; i++)
		sprintf(identity->ctrlsum + i * 2, "%02x", digest[i]);
	identity->ctrlsum[32] = '\0';

	memset(serialized, 0, sizeof(serialized));
	memset(mac_text, 0, sizeof(mac_text));
	memset(digest, 0, sizeof(digest));
	return 0;
}

static int nr_build_identity(struct mtd_info *factory, const u8 macs[2][6],
			     struct nr_identity *identity)
{
	u8 seed[SHA256_SUM_LEN];
	char factory_code[3], serial_sequence[7], wps_base_text[8];
	unsigned int wps_base;
	int ret;

	memset(identity, 0, sizeof(*identity));
	ret = nr_factory_seed(factory, seed);
	if (ret)
		return ret;

	nr_decimal_string(seed, "serial-factory", factory_code, 2);
	nr_decimal_string(seed, "serial", serial_sequence, 6);
	snprintf(identity->sernumb, sizeof(identity->sernumb), "S26%sVN%s",
		 factory_code, serial_sequence);
	nr_decimal_string(seed, "servicetag", identity->servicetag, 15);
	nr_alnum_string(seed, "servicepass", identity->servicepass, 20);
	snprintf(identity->wlanssid, sizeof(identity->wlanssid), "NR3053-%02X%02X",
		 macs[0][4], macs[0][5]);
	nr_alnum_string(seed, "wlankey", identity->wlankey, 20);
	nr_decimal_string(seed, "wlanwps", wps_base_text, 7);
	wps_base = simple_strtoul(wps_base_text, NULL, 10);
	snprintf(identity->wlanwps, sizeof(identity->wlanwps), "%07u%u",
		 wps_base, nr_wps_checksum(wps_base));
	ret = nr_calculate_ctrlsum(identity, macs);

	memset(seed, 0, sizeof(seed));
	memset(factory_code, 0, sizeof(factory_code));
	memset(serial_sequence, 0, sizeof(serial_sequence));
	memset(wps_base_text, 0, sizeof(wps_base_text));
	return ret;
}

static int nr_env_add(char **cursor, size_t *remaining,
		      const char *key, const char *value)
{
	int ret;

	ret = nr_append_cstr(cursor, remaining, key);
	if (!ret)
		ret = nr_append_cstr(cursor, remaining, "=");
	if (!ret)
		ret = nr_append(cursor, remaining, value, strlen(value) + 1);
	return ret;
}

static int nr_create_uconfig(struct mtd_info *factory, const u8 macs[2][6],
			     u8 image[NR_UCONFIG_SIZE])
{
	struct nr_identity identity;
	struct nr_default_entry identity_entries[] = {
		{ "sernumb", identity.sernumb },
		{ "servicetag", identity.servicetag },
		{ "servicehost", NR_SERVICE_HOST },
		{ "servicepass", identity.servicepass },
		{ "wlanssid", identity.wlanssid },
		{ "wlankey", identity.wlankey },
		{ "wlanwps", identity.wlanwps },
		{ "country", NR_COUNTRY },
		{ "ndmhwid", NR_NDM_HW_ID },
	};
	char *cursor;
	size_t remaining;
	u8 *payload;
	u32 crc;
	size_t i;
	int ret;

	ret = nr_build_identity(factory, macs, &identity);
	if (ret)
		return ret;

	memset(image, 0xff, NR_UCONFIG_SIZE);
	payload = image + sizeof(u32);
	memset(payload, 0, NR_ENV_DATA_SIZE);
	cursor = (char *)payload;
	remaining = NR_ENV_DATA_SIZE;

	for (i = 0; i < ARRAY_SIZE(identity_entries); i++) {
		ret = nr_env_add(&cursor, &remaining, identity_entries[i].key,
				 identity_entries[i].value);
		if (ret)
			goto out;
	}
	for (i = 0; i < ARRAY_SIZE(nr_recovery_defaults); i++) {
		ret = nr_env_add(&cursor, &remaining, nr_recovery_defaults[i].key,
				 nr_recovery_defaults[i].value);
		if (ret)
			goto out;
	}
	ret = nr_env_add(&cursor, &remaining, "ctrlsum", identity.ctrlsum);
	if (ret)
		goto out;

	crc = crc32(0, payload, NR_ENV_DATA_SIZE);
	put_unaligned_le32(crc, image);
	memcpy(image + NR_ENV_BLOCK_SIZE, image, NR_ENV_BLOCK_SIZE);

out:
	memset(&identity, 0, sizeof(identity));
	return ret;
}

static bool nr_env_block_crc_ok(const u8 block[NR_ENV_BLOCK_SIZE])
{
	return get_unaligned_le32(block) ==
		crc32(0, block + sizeof(u32), NR_ENV_DATA_SIZE);
}

static bool nr_env_get(const u8 block[NR_ENV_BLOCK_SIZE], const char *key,
		       struct nr_env_value *value)
{
	const u8 *cursor = block + sizeof(u32);
	const u8 *end = block + NR_ENV_BLOCK_SIZE;
	size_t key_len = strlen(key);

	while (cursor < end && *cursor) {
		const u8 *nul = memchr(cursor, '\0', end - cursor);
		const u8 *equals;

		if (!nul)
			return false;
		equals = memchr(cursor, '=', nul - cursor);
		if (!equals)
			return false;
		if ((size_t)(equals - cursor) == key_len &&
		    !memcmp(cursor, key, key_len)) {
			value->data = equals + 1;
			value->len = nul - equals - 1;
			return value->len != 0;
		}
		cursor = nul + 1;
	}

	return false;
}

static bool nr_serial_valid(const struct nr_env_value *serial)
{
	size_t i;

	if (serial->len != 13 || serial->data[0] != 'S')
		return false;
	for (i = 1; i < 5; i++)
		if (!isdigit(serial->data[i]))
			return false;
	for (i = 5; i < 7; i++)
		if (!isdigit(serial->data[i]) && !isupper(serial->data[i]))
			return false;
	for (i = 7; i < 13; i++)
		if (!isdigit(serial->data[i]))
			return false;
	return true;
}

static bool nr_ctrlsum_valid(const u8 block[NR_ENV_BLOCK_SIZE],
			     const u8 macs[2][6])
{
	struct nr_env_value fields[ARRAY_SIZE(nr_ctrlsum_fields)], stored;
	char serialized[384], mac_text[18];
	char expected[33];
	char *cursor = serialized;
	size_t remaining = sizeof(serialized);
	u8 digest[MD5_SUM_LEN];
	size_t i;
	bool valid = false;

	for (i = 0; i < ARRAY_SIZE(fields); i++) {
		if (!nr_env_get(block, nr_ctrlsum_fields[i], &fields[i]))
			goto out;
		if (i && nr_append_cstr(&cursor, &remaining, ","))
			goto out;
		if (nr_append(&cursor, &remaining, fields[i].data, fields[i].len))
			goto out;
	}
	for (i = 0; i < 2; i++) {
		nr_format_mac(mac_text, macs[i]);
		if (nr_append_cstr(&cursor, &remaining, ",") ||
		    nr_append_cstr(&cursor, &remaining, mac_text))
			goto out;
	}
	if (!nr_env_get(block, "ctrlsum", &stored) || stored.len != 32)
		goto out;

	md5_wd((const u8 *)serialized, cursor - serialized, digest,
	       MD5_DEF_CHUNK_SZ);
	for (i = 0; i < MD5_SUM_LEN; i++)
		sprintf(expected + i * 2, "%02x", digest[i]);
	expected[32] = '\0';
	valid = !memcmp(stored.data, expected, 32);

out:
	memset(serialized, 0, sizeof(serialized));
	memset(mac_text, 0, sizeof(mac_text));
	memset(expected, 0, sizeof(expected));
	memset(digest, 0, sizeof(digest));
	return valid;
}

static const u8 *nr_valid_identity_block(const u8 image[NR_UCONFIG_SIZE],
					 const u8 macs[2][6])
{
	size_t block_index;

	for (block_index = 0; block_index < 2; block_index++) {
		const u8 *block = image + block_index * NR_ENV_BLOCK_SIZE;
		struct nr_env_value serial;

		if (!nr_env_block_crc_ok(block))
			continue;
		if (!nr_env_get(block, "sernumb", &serial) ||
		    !nr_serial_valid(&serial))
			continue;
		if (!nr_ctrlsum_valid(block, macs))
			continue;
		return block;
	}

	return NULL;
}

static bool nr_env_equals(const u8 block[NR_ENV_BLOCK_SIZE], const char *key,
			  const char *expected)
{
	struct nr_env_value value;
	size_t expected_len = strlen(expected);

	return nr_env_get(block, key, &value) && value.len == expected_len &&
	       !memcmp(value.data, expected, expected_len);
}

static bool nr_recovery_defaults_valid(const u8 block[NR_ENV_BLOCK_SIZE])
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(nr_recovery_defaults); i++)
		if (!nr_env_equals(block, nr_recovery_defaults[i].key,
				   nr_recovery_defaults[i].value))
			return false;

	return true;
}

static void nr_fill_slot_status(const u8 image[NR_UCONFIG_SIZE],
				const u8 macs[2][6],
				struct nr3053_uconfig_slot_status *status)
{
	const u8 *valid_block;

	memset(status, 0, sizeof(*status));
	status->crc[0] = nr_env_block_crc_ok(image);
	status->crc[1] = nr_env_block_crc_ok(image + NR_ENV_BLOCK_SIZE);
	status->redundant = status->crc[0] && status->crc[1] &&
		!memcmp(image, image + NR_ENV_BLOCK_SIZE, NR_ENV_BLOCK_SIZE);
	valid_block = nr_valid_identity_block(image, macs);
	status->identity_valid = valid_block;
	if (!valid_block)
		return;

	status->country_vn = nr_env_equals(valid_block, "country", NR_COUNTRY);
	status->recovery_defaults = nr_recovery_defaults_valid(valid_block);
}

static int nr_open_layout(struct mtd_info **factory_out,
			  struct mtd_info **ubi_out, u8 (*macs)[6])
{
	struct mtd_info *factory, *ubi;
	int ret;

	*factory_out = NULL;
	*ubi_out = NULL;
	mtd_probe_devices();
	factory = get_mtd_device_nm(NR_FACTORY_NAME);
	if (IS_ERR_OR_NULL(factory))
		return -ENODEV;
	ubi = get_mtd_device_nm(NR_UBI_NAME);
	if (IS_ERR_OR_NULL(ubi)) {
		put_mtd_device(factory);
		return -ENODEV;
	}
	if (factory->size < NR_FACTORY_SIZE ||
	    ubi->size < NR_UCONFIG_RES_OFFSET + NR_UCONFIG_SIZE) {
		put_mtd_device(factory);
		put_mtd_device(ubi);
		return -ERANGE;
	}
	if (macs) {
		ret = nr_read_factory_macs(factory, macs);
		if (ret) {
			put_mtd_device(factory);
			put_mtd_device(ubi);
			return ret;
		}
	}

	*factory_out = factory;
	*ubi_out = ubi;
	return 0;
}

static void nr_close_layout(struct mtd_info *factory, struct mtd_info *ubi)
{
	if (factory)
		put_mtd_device(factory);
	if (ubi)
		put_mtd_device(ubi);
}

int nr3053_uconfig_get_status(struct nr3053_uconfig_status *status)
{
	struct mtd_info *factory = NULL, *ubi = NULL;
	u8 macs[2][6] = {};
	u8 *active = NULL, *recovery = NULL;
	int ret;

	if (!status)
		return -EINVAL;
	memset(status, 0, sizeof(*status));
	ret = nr_open_layout(&factory, &ubi, macs);
	if (ret)
		goto out;
	active = malloc(NR_UCONFIG_SIZE);
	recovery = malloc(NR_UCONFIG_SIZE);
	if (!active || !recovery) {
		ret = -ENOMEM;
		goto out;
	}
	ret = nr_mtd_read_exact(factory, NR_UCONFIG_OFFSET,
				NR_UCONFIG_SIZE, active);
	if (!ret)
		ret = nr_mtd_read_exact(ubi, NR_UCONFIG_RES_OFFSET,
					NR_UCONFIG_SIZE, recovery);
	if (ret)
		goto out;

	nr_fill_slot_status(active, macs, &status->active);
	nr_fill_slot_status(recovery, macs, &status->recovery);
	status->copies_identical = !memcmp(active, recovery, NR_UCONFIG_SIZE);
	status->healthy = status->active.identity_valid &&
		status->recovery.identity_valid && status->active.redundant &&
		status->recovery.redundant && status->active.country_vn &&
		status->recovery.country_vn &&
		status->active.recovery_defaults &&
		status->recovery.recovery_defaults && status->copies_identical;

out:
	memset(macs, 0, sizeof(macs));
	if (active) {
		memset(active, 0, NR_UCONFIG_SIZE);
		free(active);
	}
	if (recovery) {
		memset(recovery, 0, NR_UCONFIG_SIZE);
		free(recovery);
	}
	nr_close_layout(factory, ubi);
	return ret;
}

int nr3053_uconfig_read_slot(enum nr3053_uconfig_slot slot, void *buffer,
			     size_t length)
{
	struct mtd_info *factory = NULL, *ubi = NULL;
	int ret;

	if (!buffer || length != NR_UCONFIG_SIZE)
		return -EINVAL;
	if (slot != NR3053_UCONFIG_ACTIVE &&
	    slot != NR3053_UCONFIG_RECOVERY)
		return -EINVAL;
	ret = nr_open_layout(&factory, &ubi, NULL);
	if (ret)
		return ret;
	if (slot == NR3053_UCONFIG_ACTIVE)
		ret = nr_mtd_read_exact(factory, NR_UCONFIG_OFFSET, length, buffer);
	else
		ret = nr_mtd_read_exact(ubi, NR_UCONFIG_RES_OFFSET, length, buffer);
	nr_close_layout(factory, ubi);
	return ret;
}

static void nr_normalize_slot(u8 image[NR_UCONFIG_SIZE], const u8 *valid_block)
{
	memset(image, 0xff, NR_UCONFIG_SIZE);
	memcpy(image, valid_block, NR_ENV_BLOCK_SIZE);
	memcpy(image + NR_ENV_BLOCK_SIZE, valid_block, NR_ENV_BLOCK_SIZE);
}

static int nr_write_verified(struct mtd_info *mtd, loff_t offset,
			     const u8 image[NR_UCONFIG_SIZE], u8 *verify)
{
	struct erase_info erase = {};
	size_t retlen = 0;
	int ret;

	if (offset + NR_UCONFIG_SIZE > mtd->size ||
	    offset % mtd->erasesize || NR_UCONFIG_SIZE % mtd->erasesize ||
	    offset % mtd->writesize || NR_UCONFIG_SIZE % mtd->writesize)
		return -ERANGE;

	erase.mtd = mtd;
	erase.addr = offset;
	erase.len = NR_UCONFIG_SIZE;
	ret = mtd_erase(mtd, &erase);
	if (ret)
		return ret;

	ret = mtd_write(mtd, offset, NR_UCONFIG_SIZE, &retlen, image);
	if (ret < 0 || retlen != NR_UCONFIG_SIZE)
		return ret < 0 ? ret : -EIO;
	ret = nr_mtd_read_exact(mtd, offset, NR_UCONFIG_SIZE, verify);
	if (ret)
		return ret;
	if (memcmp(image, verify, NR_UCONFIG_SIZE))
		return -EIO;

	return 0;
}

int nr3053_uconfig_restore(const void *image, size_t length,
			   enum nr3053_uconfig_action *action)
{
	struct mtd_info *factory = NULL, *ubi = NULL;
	const u8 *valid_block;
	u8 macs[2][6] = {};
	u8 *normalized = NULL, *verify = NULL;
	int ret;

	if (action)
		*action = NR3053_UCONFIG_ACTION_NONE;
	if (!image || length != NR_UCONFIG_SIZE)
		return -EINVAL;
	ret = nr_open_layout(&factory, &ubi, macs);
	if (ret)
		goto out;
	valid_block = nr_valid_identity_block(image, macs);
	if (!valid_block ||
	    !nr_env_equals(valid_block, "country", NR_COUNTRY) ||
	    !nr_env_equals(valid_block, "ndmhwid", NR_NDM_HW_ID) ||
	    !nr_env_equals(valid_block, "servicehost", NR_SERVICE_HOST) ||
	    !nr_recovery_defaults_valid(valid_block)) {
		ret = -EINVAL;
		goto out;
	}
	normalized = malloc(NR_UCONFIG_SIZE);
	verify = malloc(NR_UCONFIG_SIZE);
	if (!normalized || !verify) {
		ret = -ENOMEM;
		goto out;
	}
	nr_normalize_slot(normalized, valid_block);
	ret = nr_write_verified(ubi, NR_UCONFIG_RES_OFFSET,
				normalized, verify);
	if (!ret)
		ret = nr_write_verified(factory, NR_UCONFIG_OFFSET,
					normalized, verify);
	if (!ret && action)
		*action = NR3053_UCONFIG_ACTION_RESTORED;

out:
	memset(macs, 0, sizeof(macs));
	if (normalized) {
		memset(normalized, 0, NR_UCONFIG_SIZE);
		free(normalized);
	}
	if (verify) {
		memset(verify, 0, NR_UCONFIG_SIZE);
		free(verify);
	}
	nr_close_layout(factory, ubi);
	return ret;
}

static int nr_uconfig_run(enum nr3053_uconfig_action *action)
{
	struct mtd_info *factory = NULL, *ubi = NULL;
	const u8 *active_block, *reserve_block;
	enum nr3053_uconfig_action performed = NR3053_UCONFIG_ACTION_NONE;
	u8 macs[2][6] = {};
	u8 *active = NULL, *reserve = NULL;
	int ret;

	if (action)
		*action = NR3053_UCONFIG_ACTION_NONE;
	ret = nr_open_layout(&factory, &ubi, macs);
	if (ret) {
		printf("U-Config: layout or Factory identity source invalid; preserving flash\n");
		goto out;
	}

	active = malloc(NR_UCONFIG_SIZE);
	reserve = malloc(NR_UCONFIG_SIZE);
	if (!active || !reserve) {
		ret = -ENOMEM;
		goto out;
	}
	ret = nr_mtd_read_exact(factory, NR_UCONFIG_OFFSET,
				NR_UCONFIG_SIZE, active);
	if (!ret)
		ret = nr_mtd_read_exact(ubi, NR_UCONFIG_RES_OFFSET,
					NR_UCONFIG_SIZE, reserve);
	if (ret) {
		printf("U-Config: read failed; preserving flash\n");
		goto out;
	}

	active_block = nr_valid_identity_block(active, macs);
	reserve_block = nr_valid_identity_block(reserve, macs);
	if (active_block && reserve_block) {
		printf("U-Config: valid device identity present; preserved\n");
		performed = NR3053_UCONFIG_ACTION_PRESERVED;
		ret = 0;
		goto out;
	}

	if (active_block) {
		nr_normalize_slot(reserve, active_block);
		printf("U-Config: restoring recovery mirror from valid identity\n");
		performed = NR3053_UCONFIG_ACTION_REPAIRED_RECOVERY;
		ret = nr_write_verified(ubi, NR_UCONFIG_RES_OFFSET,
					reserve, active);
	} else if (reserve_block) {
		nr_normalize_slot(active, reserve_block);
		printf("U-Config: restoring active slot from valid recovery identity\n");
		performed = NR3053_UCONFIG_ACTION_REPAIRED_ACTIVE;
		ret = nr_write_verified(factory, NR_UCONFIG_OFFSET,
					active, reserve);
	} else {
		printf("U-Config: provisioning device-unique VN identity\n");
		performed = NR3053_UCONFIG_ACTION_PROVISIONED;
		ret = nr_create_uconfig(factory, macs, active);
		if (!ret)
			ret = nr_write_verified(ubi, NR_UCONFIG_RES_OFFSET,
						active, reserve);
		if (!ret)
			ret = nr_write_verified(factory, NR_UCONFIG_OFFSET,
						active, reserve);
	}
	if (ret) {
		printf("U-Config: provisioning failed; boot will continue\n");
	} else {
		printf("U-Config: write and read-back verification passed\n");
		if (action)
			*action = performed;
	}

out:
	if (!ret && action && performed == NR3053_UCONFIG_ACTION_PRESERVED)
		*action = performed;
	memset(macs, 0, sizeof(macs));
	if (active) {
		memset(active, 0, NR_UCONFIG_SIZE);
		free(active);
	}
	if (reserve) {
		memset(reserve, 0, NR_UCONFIG_SIZE);
		free(reserve);
	}
	nr_close_layout(factory, ubi);
	return ret;
}

int nr3053_uconfig_provision(enum nr3053_uconfig_action *action)
{
	return nr_uconfig_run(action);
}

int nr3053_uconfig_autoboot_prepare(void)
{
	static bool completed;
	int ret;

	if (completed)
		return 0;

	printf("U-Config: autoboot preflight independent of saved bootcmd\n");
	ret = nr3053_uconfig_provision(NULL);
	if (!ret)
		completed = true;
	else
		printf("U-Config: autoboot preflight failed; continuing boot\n");

	return ret;
}

const char *nr3053_uconfig_action_name(enum nr3053_uconfig_action action)
{
	switch (action) {
	case NR3053_UCONFIG_ACTION_PRESERVED:
		return "preserved";
	case NR3053_UCONFIG_ACTION_REPAIRED_ACTIVE:
		return "repaired-active";
	case NR3053_UCONFIG_ACTION_REPAIRED_RECOVERY:
		return "repaired-recovery";
	case NR3053_UCONFIG_ACTION_PROVISIONED:
		return "provisioned";
	case NR3053_UCONFIG_ACTION_RESTORED:
		return "restored";
	default:
		return "none";
	}
}

static int do_nr3053_uconfig(struct cmd_tbl *cmdtp, int flag, int argc,
			     char *const argv[])
{
	struct nr3053_uconfig_status status;
	enum nr3053_uconfig_action action;
	int ret;

	if (argc != 2)
		return CMD_RET_USAGE;
	if (!strcmp(argv[1], "status")) {
		ret = nr3053_uconfig_get_status(&status);
		if (ret) {
			printf("U-Config: read-only status failed\n");
			return CMD_RET_FAILURE;
		}
		printf("U-Config: active=%s recovery=%s copies=%s policy=%s\n",
		       status.active.identity_valid ? "valid" : "invalid",
		       status.recovery.identity_valid ? "valid" : "invalid",
		       status.copies_identical ? "identical" : "different",
		       status.healthy ? "pass" : "attention");
		return status.healthy ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
	}
	if (!strcmp(argv[1], "provision")) {
		ret = nr3053_uconfig_provision(&action);
		return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
	}
	return CMD_RET_USAGE;
}

static int do_nr3053_autoboot(struct cmd_tbl *cmdtp, int flag, int argc,
			      char *const argv[])
{
	static bool running;
	int ret;

	if (argc != 1)
		return CMD_RET_USAGE;
	if (running) {
		printf("U-Config: recursive nr3053_autoboot blocked\n");
		return CMD_RET_FAILURE;
	}

	/* Fail-open: boot the saved command even if identity repair failed. */
	nr3053_uconfig_autoboot_prepare();
	running = true;
	ret = run_command("run bootcmd", 0);
	running = false;

	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(nr3053_uconfig, 2, 0, do_nr3053_uconfig,
	   "validate or one-time provision NR3053 Keenetic U-Config",
	   "status\n"
	   "nr3053_uconfig provision\n"
	   "Values remain redacted; valid existing identities are preserved."
);

U_BOOT_CMD(nr3053_autoboot, 1, 0, do_nr3053_autoboot,
	   "run NR3053 U-Config preflight, then the saved bootcmd",
	   "The preflight runs once per U-Boot session and is fail-open."
);
