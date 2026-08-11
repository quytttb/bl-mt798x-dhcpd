// SPDX-License-Identifier: GPL-2.0+
/*
 * GPIO Hardware Verification Tool — DM GPIO backend (MT798x Filogic)
 *
 * Copyright (C) 2026 Yuzhii0718 <admin@yuzhii0718.eu.org>
 *
 * For MT7981/MT7986/MT7987/MT7988 platforms with Mediatek Pinctrl + DM_GPIO.
 * Single GPIO controller, gpio_count discovered dynamically from GPIO device.
 *
 * Usage:
 *   gpiotest list                        - Scan all GPIO pins, show states
 *   gpiotest blink <pin> [cnt] [ms]      - Blink GPIO (LED verification)
 *   gpiotest monitor <pin>               - Monitor GPIO input (button test)
 *   gpiotest all-leds [start] [end] [ms] - Cycle through GPIOs one by one
 *   gpiotest out <pin> <0|1>             - Set GPIO output value
 *   gpiotest in <pin>                    - Read GPIO input value
 *
 * Shortcut: gpioscan  (same as gpiotest list)
 */

#include <command.h>
#include <linux/delay.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <dm.h>
#include <dm/uclass.h>
#include <asm/gpio.h>

#define GPIOTEST_GPIO_MAX	128

/* =================================================================
 * DM GPIO Backend — single GPIO controller (Mediatek Pinctrl)
 * ================================================================= */

/**
 * gpiotest_request() - request a GPIO pin by global pin number
 *
 * MT798x has a single GPIO controller, so offset == pin number.
 * The pin range is validated against the controller's gpio_count at runtime.
 */
static int gpiotest_request(uint gpio_num, struct gpio_desc *desc)
{
	struct udevice *dev;
	struct gpio_dev_priv *priv;
	int ret;

	if (gpio_num >= GPIOTEST_GPIO_MAX)
		return -EINVAL;

	ret = uclass_first_device_err(UCLASS_GPIO, &dev);
	if (ret)
		return ret;

	priv = dev_get_uclass_priv(dev);
	if (!priv)
		return -ENODEV;

	if (gpio_num >= priv->gpio_count)
		return -EINVAL;

	desc->dev = dev;
	desc->offset = gpio_num;
	desc->flags = 0;
	ret = dm_gpio_request(desc, "gpiotest");

	return ret;
}

/**
 * gpiotest_get_gpio_count() - discover number of GPIO pins from hardware
 */
static uint gpiotest_get_gpio_count(void)
{
	struct udevice *dev;
	struct gpio_dev_priv *priv;

	if (uclass_first_device_err(UCLASS_GPIO, &dev) || !dev)
		return GPIOTEST_GPIO_MAX;

	priv = dev_get_uclass_priv(dev);
	if (!priv)
		return GPIOTEST_GPIO_MAX;

	if (priv->gpio_count > GPIOTEST_GPIO_MAX)
		return GPIOTEST_GPIO_MAX;

	return priv->gpio_count;
}

static int dm_gpio_dir_input(struct gpio_desc *desc)
{
	return dm_gpio_set_dir_flags(desc, GPIOD_IS_IN);
}

static int dm_gpio_dir_output(struct gpio_desc *desc, int value)
{
	int ret = dm_gpio_set_dir_flags(desc, GPIOD_IS_OUT);

	if (!ret)
		ret = dm_gpio_set_value(desc, value);
	return ret;
}

static int dm_gpio_read(struct gpio_desc *desc)
{
	return dm_gpio_get_value(desc);
}

static int dm_gpio_write(struct gpio_desc *desc, int value)
{
	return dm_gpio_set_value(desc, value);
}

/* =================================================================
 * Command Implementations
 * ================================================================= */

/* ---------- gpiotest list ---------- */
static int do_gpiotest_list(void)
{
	struct gpio_desc desc;
	uint max_gpio = gpiotest_get_gpio_count();
	int i, ret;
	int tested = 0, ok_cnt = 0, fail = 0;

	printf("=== MT798x GPIO Scan (GPIO#0 ~ GPIO#%d) ===\n", max_gpio - 1);
	printf("%-4s %-12s %-8s %s\n", "PIN", "DIRECTION", "VALUE", "NOTES");
	printf("---- ------------ -------- --------------------\n");

	for (i = 0; i < (int)max_gpio; i++) {
		int val;
		const char *dir_str = "?";
		char note[40] = "";

		ret = gpiotest_request(i, &desc);
		if (ret)
			continue;

		tested++;

		ret = dm_gpio_dir_input(&desc);
		if (!ret) {
			val = dm_gpio_read(&desc);
			if (val >= 0) {
				ok_cnt++;
				dir_str = "IN";
			} else {
				fail++;
				dir_str = "ERR";
			}
		} else {
			fail++;
			dir_str = "n/a";
			val = -1;
		}

		dm_gpio_free(desc.dev, &desc);

		if (val >= 0)
			printf("%-4d %-12s %-8d %s\n", i, dir_str, val, note);
		else
			printf("%-4d %-12s %-8s %s\n", i, dir_str, "ERR", note);
	}

	printf("---- ------------ -------- --------------------\n");
	printf("Tested: %d, OK: %d", tested, ok_cnt);
	if (fail > 0)
		printf(", Failed: %d", fail);
	printf("\n");

	return CMD_RET_SUCCESS;
}

/* ---------- gpiotest blink ---------- */
static int do_gpiotest_blink(uint gpio_num, int count, int interval_ms)
{
	struct gpio_desc desc;
	int ret, i;

	if (count <= 0)
		count = 3;
	if (interval_ms <= 0)
		interval_ms = 200;

	ret = gpiotest_request(gpio_num, &desc);
	if (ret) {
		printf("ERROR: Cannot request GPIO#%d (err=%d)\n", gpio_num, ret);
		return CMD_RET_FAILURE;
	}

	ret = dm_gpio_dir_output(&desc, 0);
	if (ret) {
		printf("ERROR: Cannot set GPIO#%d as output (err=%d)\n",
		       gpio_num, ret);
		dm_gpio_free(desc.dev, &desc);
		return CMD_RET_FAILURE;
	}

	printf("Blinking GPIO#%d: %d times @ %dms interval\n",
	       gpio_num, count, interval_ms);

	for (i = 0; i < count; i++) {
		printf("\r  [%d/%d] ON  ", i + 1, count);
		dm_gpio_write(&desc, 1);
		mdelay(interval_ms);

		printf("\r  [%d/%d] OFF ", i + 1, count);
		dm_gpio_write(&desc, 0);
		mdelay(interval_ms);
	}

	dm_gpio_write(&desc, 0);
	printf("\r  Done. GPIO#%d set to LOW.          \n", gpio_num);

	dm_gpio_free(desc.dev, &desc);
	return CMD_RET_SUCCESS;
}

/* ---------- gpiotest monitor ---------- */
static int do_gpiotest_monitor(uint gpio_num)
{
	struct gpio_desc desc;
	ulong start_time;
	int ret;
	int last_val = -1;
	int val;

	ret = gpiotest_request(gpio_num, &desc);
	if (ret) {
		printf("ERROR: Cannot request GPIO#%d (err=%d)\n", gpio_num, ret);
		return CMD_RET_FAILURE;
	}

	ret = dm_gpio_dir_input(&desc);
	if (ret) {
		printf("ERROR: Cannot set GPIO#%d as input (err=%d)\n",
		       gpio_num, ret);
		dm_gpio_free(desc.dev, &desc);
		return CMD_RET_FAILURE;
	}

	printf("Monitoring GPIO#%d (press any key to stop)...\n", gpio_num);
	printf("Initial state: ");
	val = dm_gpio_read(&desc);
	if (val >= 0) {
		printf("%d (%s)\n", val, val ? "HIGH" : "LOW");
		last_val = val;
	} else {
		printf("ERROR\n");
		dm_gpio_free(desc.dev, &desc);
		return CMD_RET_FAILURE;
	}
	printf("--- Waiting for state changes ---\n");

	start_time = get_timer(0);

	while (1) {
		if (tstc()) {
			getchar();
			printf("\n--- Monitoring stopped by user ---\n");
			break;
		}

		val = dm_gpio_read(&desc);
		if (val < 0) {
			printf("ERROR reading GPIO#%d\n", gpio_num);
			break;
		}

		if (val != last_val) {
			ulong elapsed = get_timer(start_time) / 1000;

			printf("[%3lu.%03lus] GPIO#%d: %d -> %d (%s -> %s)\n",
			       elapsed / 1000, elapsed % 1000,
			       gpio_num, last_val, val,
			       last_val ? "HIGH" : "LOW",
			       val ? "HIGH" : "LOW");
			last_val = val;
		}

		mdelay(10);
	}

	printf("GPIO#%d final state: %d\n", gpio_num, last_val);
	dm_gpio_free(desc.dev, &desc);

	return CMD_RET_SUCCESS;
}

/* ---------- gpiotest all-leds ---------- */
static int do_gpiotest_all_leds(uint start, uint end, int interval_ms)
{
	uint i;
	uint max_gpio = gpiotest_get_gpio_count();

	if (end >= max_gpio)
		end = max_gpio - 1;
	if (start > end) {
		uint tmp = start;
		start = end;
		end = tmp;
	}
	if (interval_ms <= 0)
		interval_ms = 500;

	printf("Cycling GPIO#%u ~ GPIO#%u one by one (%dms each)\n",
	       start, end, interval_ms);
	printf("Watch the LEDs to identify which GPIO controls which LED.\n");
	printf("(Press Ctrl+C to skip current pin)\n\n");

	for (i = start; i <= end; i++) {
		struct gpio_desc desc;
		int ret;

		ret = gpiotest_request(i, &desc);
		if (ret)
			continue;

		ret = dm_gpio_dir_output(&desc, 0);
		if (ret) {
			dm_gpio_free(desc.dev, &desc);
			continue;
		}

		printf("\rGPIO#%u [ON ]", i);
		dm_gpio_write(&desc, 1);
		mdelay(interval_ms);

		if (tstc()) {
			getchar();
			printf(" (skipped)");
			mdelay(500);
		}

		printf("\rGPIO#%u [OFF]", i);
		dm_gpio_write(&desc, 0);
		mdelay(200);

		dm_gpio_free(desc.dev, &desc);
	}

	printf("\rDone. All GPIOs restored to LOW.                              \n");

	return CMD_RET_SUCCESS;
}

/* ---------- gpiotest out ---------- */
static int do_gpiotest_out(uint gpio_num, int value)
{
	struct gpio_desc desc;
	int ret;

	ret = gpiotest_request(gpio_num, &desc);
	if (ret) {
		printf("ERROR: Cannot request GPIO#%d (err=%d)\n", gpio_num, ret);
		return CMD_RET_FAILURE;
	}

	ret = dm_gpio_dir_output(&desc, value);
	if (ret) {
		printf("ERROR: Cannot set GPIO#%d as output (err=%d)\n",
		       gpio_num, ret);
		dm_gpio_free(desc.dev, &desc);
		return CMD_RET_FAILURE;
	}

	printf("GPIO#%d -> %d (%s)\n", gpio_num, value, value ? "HIGH" : "LOW");
	dm_gpio_free(desc.dev, &desc);
	return CMD_RET_SUCCESS;
}

/* ---------- gpiotest in ---------- */
static int do_gpiotest_in(uint gpio_num)
{
	struct gpio_desc desc;
	int ret, val;

	ret = gpiotest_request(gpio_num, &desc);
	if (ret) {
		printf("ERROR: Cannot request GPIO#%d (err=%d)\n", gpio_num, ret);
		return CMD_RET_FAILURE;
	}

	ret = dm_gpio_dir_input(&desc);
	if (ret) {
		printf("ERROR: Cannot set GPIO#%d as input (err=%d)\n",
		       gpio_num, ret);
		dm_gpio_free(desc.dev, &desc);
		return CMD_RET_FAILURE;
	}

	val = dm_gpio_read(&desc);
	if (val >= 0)
		printf("GPIO#%d = %d (%s)\n", gpio_num, val, val ? "HIGH" : "LOW");
	else
		printf("ERROR: Cannot read GPIO#%d\n", gpio_num);

	dm_gpio_free(desc.dev, &desc);
	return CMD_RET_SUCCESS;
}

/* =================================================================
 * Command Dispatcher
 * ================================================================= */

static int do_gpiotest(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	const char *subcmd;

	if (argc < 2)
		return CMD_RET_USAGE;

	subcmd = argv[1];

	/* gpiotest list */
	if (!strcmp(subcmd, "list"))
		return do_gpiotest_list();

	/* gpiotest blink <pin> [count] [interval_ms] */
	if (!strcmp(subcmd, "blink")) {
		uint pin;
		int count = 3, interval = 200;

		if (argc < 3)
			return CMD_RET_USAGE;
		pin = dectoul(argv[2], NULL);
		if (argc >= 4)
			count = dectoul(argv[3], NULL);
		if (argc >= 5)
			interval = dectoul(argv[4], NULL);
		return do_gpiotest_blink(pin, count, interval);
	}

	/* gpiotest monitor <pin> */
	if (!strcmp(subcmd, "monitor")) {
		uint pin;

		if (argc < 3)
			return CMD_RET_USAGE;
		pin = dectoul(argv[2], NULL);
		return do_gpiotest_monitor(pin);
	}

	/* gpiotest all-leds [start] [end] [interval_ms] */
	if (!strcmp(subcmd, "all-leds")) {
		uint max_gpio = gpiotest_get_gpio_count();
		uint start = 0, end = max_gpio - 1;
		int interval = 500;

		if (argc >= 3)
			start = dectoul(argv[2], NULL);
		if (argc >= 4)
			end = dectoul(argv[3], NULL);
		if (argc >= 5)
			interval = dectoul(argv[4], NULL);
		return do_gpiotest_all_leds(start, end, interval);
	}

	/* gpiotest out <pin> <0|1> */
	if (!strcmp(subcmd, "out")) {
		uint pin;
		int value;

		if (argc < 4)
			return CMD_RET_USAGE;
		pin = dectoul(argv[2], NULL);
		value = dectoul(argv[3], NULL) ? 1 : 0;
		return do_gpiotest_out(pin, value);
	}

	/* gpiotest in <pin> */
	if (!strcmp(subcmd, "in")) {
		uint pin;

		if (argc < 3)
			return CMD_RET_USAGE;
		pin = dectoul(argv[2], NULL);
		return do_gpiotest_in(pin);
	}

	printf("Unknown subcommand: %s\n", subcmd);
	return CMD_RET_USAGE;
}

U_BOOT_CMD(
	gpiotest, 6, 0, do_gpiotest,
	"GPIO hardware verification tool (MT798x DM GPIO)",
	"list                        - Scan all GPIO pins, show states\n"
	"blink <pin> [cnt] [ms]       - Blink GPIO for LED identification\n"
	"monitor <pin>                - Monitor GPIO input for button test\n"
	"all-leds [start] [end] [ms]  - Cycle through GPIO pins one by one\n"
	"out <pin> <0|1>              - Set GPIO output value\n"
	"in <pin>                     - Read GPIO input value\n"
	"\n"
	"MT798x GPIO: single controller, pin count auto-detected from hardware\n"
	"  MT7981: 59 pins (GPIO 0-58), MT7986: 103 pins (GPIO 0-102)\n"
	"  MT7987: 52 pins (GPIO 0-51), MT7988: 87 pins (GPIO 0-86)"
);

/* ==================== gpioscan shortcut ==================== */

static int do_gpioscan(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	char *my_argv[2] = {"gpiotest", "list"};

	return do_gpiotest(cmdtp, flag, 2, my_argv);
}

U_BOOT_CMD(
	gpioscan, 1, 0, do_gpioscan,
	"Shortcut: scan all GPIO pins on MT798x",
	"  - gpiotest list"
);
