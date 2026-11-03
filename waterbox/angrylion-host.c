/* What angrylion's rasteriser asks of the program it is built into, for the
 * Nintendo 64's Software renderer (software-rdp.cpp). Its own worker pool
 * (parallel.cpp) is not built: the core draws on the machine's thread with
 * one worker, so a picture never depends on how threads were scheduled.
 *
 * This file is this repository's own work, under its MIT licence. */
#include <stdarg.h>
#include <stdio.h>

#include "msg.h"
#include "parallel.h"

void parallel_init(uint32_t num, bool busy)
{
	(void)num;
	(void)busy;
}

void parallel_run(void task(uint32_t))
{
	task(0);
}

uint32_t parallel_num_workers(void)
{
	return 1;
}

void parallel_close(void)
{
}

/* Its complaints go to the core's log. A game that sets something odd every
 * frame would say so every frame, so only the first few are kept. */
static int g_said;

static void say(const char *kind, const char *fmt, va_list ap)
{
	if (g_said >= 32) return;
	g_said++;
	fprintf(stderr, "angrylion %s: ", kind);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
}

void msg_error(const char *err, ...)
{
	va_list ap;
	va_start(ap, err);
	say("error", err, ap);
	va_end(ap);
}

void msg_warning(const char *err, ...)
{
	va_list ap;
	va_start(ap, err);
	say("warning", err, ap);
	va_end(ap);
}

void msg_debug(const char *err, ...)
{
	(void)err;
}
