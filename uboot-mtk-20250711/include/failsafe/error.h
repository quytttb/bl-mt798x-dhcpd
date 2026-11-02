/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718 <admin@yuzhii0718.eu.org>
 *
 * Last error of a failsafe operation (upgrade / validation / storage).
 *
 * A failing upgrade used to be reported only on the U-Boot console, which
 * means the Web UI could not tell the user anything beyond "it failed" and
 * finding out why required attaching a serial console.  The helpers here
 * record the errno *and* the message of the failure so that the HTTP
 * handlers can hand both to the browser (see the /last-error handler in
 * failsafe/modules/upgrade.c).
 *
 * Usage at a failure site - the helper returns @code, so a recording
 * failure exit is a single statement and cannot be forgotten:
 *
 *     return failsafe_error(-ENODEV, "MTD partition '%s' not found", name);
 *
 * The record is global rather than per request: the web UI serves one
 * operation at a time and every recorded operation runs to completion
 * inside a single HTTP callback.  Callers reset it when they begin an
 * operation and clear it when the operation succeeds, so what is left is
 * the failure of the last operation that failed.
 */

#ifndef _FAILSAFE_ERROR_H_
#define _FAILSAFE_ERROR_H_

#include <linux/types.h>

/* Longest recorded message, including the terminating NUL. */
#define FAILSAFE_ERROR_MSG_SIZE		192

/**
 * failsafe_error() - record a failure and return its code
 * @code: error code to report (a negative errno)
 * @fmt: printf-style message describing what failed
 *
 * Formats the message, keeps it (with @code) as the last error and prints
 * it on the console the way the subsystem always did - now with the code
 * appended, so a console line and a Web UI report can be matched up:
 *
 *     Failsafe: MTD partition 'bl2' not found (code -19)
 *
 * Returns @code, so callers can write "return failsafe_error(...)".
 */
int failsafe_error(int code, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

/** failsafe_error_reset() - forget the last error (start of an operation) */
void failsafe_error_reset(void);

/** failsafe_error_code() - code of the last error, 0 when there is none */
int failsafe_error_code(void);

/** failsafe_error_msg() - message of the last error, "" when there is none */
const char *failsafe_error_msg(void);

#endif /* _FAILSAFE_ERROR_H_ */
