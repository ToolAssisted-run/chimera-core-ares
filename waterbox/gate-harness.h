/* gate-harness.h - the schedule, shared by the reference and the sandbox.
 *
 * The gate's claim is "these two builds are the same machine". That is only
 * worth anything if the two are driven identically, so the loop, the option
 * parsing and the digests live here and each driver supplies nothing but a
 * handful of function pointers. A difference in the numbers is then a
 * difference in the MACHINE, which is the only thing being asked about.
 */
#ifndef CHIMERA_ARES_GATE_HARNESS_H
#define CHIMERA_ARES_GATE_HARNESS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GATE_MAX_INPUTS 512

struct gate_core
{
	int (*init)(void);
	const char *(*load_error)(void);
	int (*button_count)(void);
	const char *(*button_name)(int i);
	int (*axis_count)(void);
	const char *(*axis_name)(int i);
	void (*set_button)(int index, int state);
	void (*set_axis)(int index, int value);
	void (*frame)(void);
	const uint32_t *(*video)(int *w, int *h);
	const int16_t *(*audio)(int *samples);
	int (*input_was_read)(void);
	int (*domain_count)(void);
	const char *(*domain_name)(int i);
	const uint8_t *(*domain_ptr)(int i);
	int64_t (*domain_size)(int i);
	int (*bus_count)(void);
	const char *(*bus_name)(int i);
	int64_t (*bus_size)(int i);
	int (*bus_peek)(int bus, int address);
	/* The whole machine, as ares serialises it - the strong comparison. */
	int (*state)(const uint8_t **data, int64_t *size);
	void (*set_rendering)(int on);
	/* Run before every frame. The sandbox driver round-trips the machine
	 * through save/load here when --rerecord is given. */
	void (*pre_frame)(void);
};

struct gate_press
{
	int from;
	char name[48];
};

struct gate_opts
{
	int frames;
	int digest_every;
	int dump_frame;
	int quiet;
	int render;
	int stick_x, stick_y;
	char hold[16][48];
	int hold_count;
	struct gate_press press[16];
	int press_count;
	const char *dump_path;
	const char *dump_state;
	const char *dump_bus;
	int list_inputs;
};

static void gate_usage(void)
{
	fprintf(stderr,
		"  --frames N        how many frames to run (default 60)\n"
		"  --hold NAME       hold a button for the whole run, by its declared name\n"
		"  --press F NAME    hold a button from frame F onwards\n"
		"  --stick X Y       hold the first two axes at X,Y\n"
		"  --digest-every N  print a digest every N frames as well as at the end\n"
		"  --dump-frame F    write frame F as a .ppm and stop\n"
		"  --dump-to PATH    where --dump-frame writes (default frame.ppm)\n"
		"  --dump-state PATH write the whole serialised machine there at the end\n"
		"  --dump-bus PATH   write every bus there, one file per bus, at the end\n"
		"  --no-render       run with drawing off (turbo)\n"
		"  --list-inputs     print what this machine declares, and stop\n"
		"  --quiet           only the final digest line\n"
		"Button names are the machine's own: run --list-inputs to see them.\n");
}

static int gate_parse_opts(int argc, char **argv, int from, struct gate_opts *o)
{
	memset(o, 0, sizeof *o);
	o->frames = 60;
	o->dump_frame = -1;
	o->render = 1;
	o->dump_path = "frame.ppm";

	for (int i = from; i < argc; i++)
	{
		const char *a = argv[i];
		#define GATE_NEXT() (i + 1 < argc ? argv[++i] : (gate_usage(), exit(2), ""))
		if (!strcmp(a, "--frames")) o->frames = atoi(GATE_NEXT());
		else if (!strcmp(a, "--digest-every")) o->digest_every = atoi(GATE_NEXT());
		else if (!strcmp(a, "--dump-frame")) o->dump_frame = atoi(GATE_NEXT());
		else if (!strcmp(a, "--dump-to")) o->dump_path = GATE_NEXT();
		else if (!strcmp(a, "--dump-state")) o->dump_state = GATE_NEXT();
		else if (!strcmp(a, "--dump-bus")) o->dump_bus = GATE_NEXT();
		else if (!strcmp(a, "--no-render")) o->render = 0;
		else if (!strcmp(a, "--quiet")) o->quiet = 1;
		else if (!strcmp(a, "--list-inputs")) o->list_inputs = 1;
		else if (!strcmp(a, "--stick")) { o->stick_x = atoi(GATE_NEXT()); o->stick_y = atoi(GATE_NEXT()); }
		else if (!strcmp(a, "--hold"))
		{
			if (o->hold_count >= 16) { fprintf(stderr, "too many --hold\n"); return 0; }
			snprintf(o->hold[o->hold_count++], sizeof o->hold[0], "%s", GATE_NEXT());
		}
		else if (!strcmp(a, "--press"))
		{
			if (o->press_count >= 16) { fprintf(stderr, "too many --press\n"); return 0; }
			o->press[o->press_count].from = atoi(GATE_NEXT());
			snprintf(o->press[o->press_count].name, sizeof o->press[0].name, "%s", GATE_NEXT());
			o->press_count++;
		}
		/* Options the driver itself consumed; skipping them here keeps one
		 * parser rather than two that must agree. */
		else if (!strcmp(a, "--rom") || !strcmp(a, "--time") || !strcmp(a, "--machine")
			|| !strcmp(a, "--firmware")) { (void)GATE_NEXT(); }
		else if (!strcmp(a, "--port")) { (void)GATE_NEXT(); (void)GATE_NEXT(); }
		else if (!strcmp(a, "--pal") || !strcmp(a, "--fast-vi") || !strcmp(a, "--rerecord")
			|| !strcmp(a, "--report-stick")
			|| !strcmp(a, "--report-refresh")) { }
		else if (!strcmp(a, "--set")) { i++; }
		else { fprintf(stderr, "unknown option '%s'\n", a); gate_usage(); return 0; }
		#undef GATE_NEXT
	}
	return 1;
}

/* FNV-1a. The gate compares these between two builds of the same sources and
 * against nothing else, so what matters is that both compute it identically. */
static uint64_t gate_hash_init(void) { return 1469598103934665603ull; }

static uint64_t gate_hash_feed(uint64_t h, const void *data, size_t size)
{
	const uint8_t *p = (const uint8_t *)data;
	for (size_t i = 0; i < size; i++) { h ^= p[i]; h *= 1099511628211ull; }
	return h;
}

static void gate_write_ppm(const char *path, const uint32_t *bgra, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++)
	{
		uint32_t p = bgra[i];
		fputc((p >> 16) & 0xff, f);
		fputc((p >> 8) & 0xff, f);
		fputc((p >> 0) & 0xff, f);
	}
	fclose(f);
	fprintf(stderr, "wrote %s (%dx%d)\n", path, w, h);
}

static int gate_find_button(const struct gate_core *c, const char *name)
{
	for (int i = 0; i < c->button_count(); i++)
	{
		const char *n = c->button_name(i);
		if (n && !strcmp(n, name)) return i;
	}
	return -1;
}

/* The machine's memory, whichever way it publishes it. Blocks are a pointer, so
 * they are cheap; a bus is resolved per byte, so it is not, but it is the one
 * every ares machine has. */
static uint64_t gate_memory_digest(const struct gate_core *c)
{
	uint64_t h = gate_hash_init();
	if (c->domain_count() > 0)
	{
		for (int d = 0; d < c->domain_count(); d++)
			h = gate_hash_feed(h, c->domain_ptr(d), (size_t)c->domain_size(d));
		return h;
	}
	for (int b = 0; b < c->bus_count(); b++)
	{
		int64_t size = c->bus_size(b);
		for (int64_t a = 0; a < size; a++)
		{
			uint8_t byte = (uint8_t)c->bus_peek(b, (int)a);
			h = gate_hash_feed(h, &byte, 1);
		}
	}
	return h;
}

static uint64_t gate_state_digest(const struct gate_core *c)
{
	const uint8_t *data = NULL;
	int64_t size = 0;
	if (!c->state || !c->state(&data, &size) || !data) return 0;
	return gate_hash_feed(gate_hash_init(), data, (size_t)size);
}

static int gate_run(const struct gate_core *c, const struct gate_opts *o)
{
	if (c->init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", c->load_error());
		return 1;
	}
	c->set_rendering(o->render);

	if (o->list_inputs)
	{
		printf("buttons (%d):\n", c->button_count());
		for (int i = 0; i < c->button_count(); i++) printf("  %3d  %s\n", i, c->button_name(i));
		printf("axes (%d):\n", c->axis_count());
		for (int i = 0; i < c->axis_count(); i++) printf("  %3d  %s\n", i, c->axis_name(i));
		return 0;
	}

	/* Resolve the names once. A name the machine does not have is a mistake in
	 * the gate, not something to skip quietly. */
	int held[16], pressed[16];
	for (int i = 0; i < o->hold_count; i++)
	{
		held[i] = gate_find_button(c, o->hold[i]);
		if (held[i] < 0) { fprintf(stderr, "this machine has no button '%s'\n", o->hold[i]); return 2; }
	}
	for (int i = 0; i < o->press_count; i++)
	{
		pressed[i] = gate_find_button(c, o->press[i].name);
		if (pressed[i] < 0) { fprintf(stderr, "this machine has no button '%s'\n", o->press[i].name); return 2; }
	}

	if (!o->quiet)
	{
		for (int i = 0; i < c->domain_count(); i++)
			fprintf(stderr, "  domain %-14s %10lld bytes\n", c->domain_name(i), (long long)c->domain_size(i));
		for (int i = 0; i < c->bus_count(); i++)
			fprintf(stderr, "  bus    %-14s %10lld bytes\n", c->bus_name(i), (long long)c->bus_size(i));
	}

	uint64_t video = gate_hash_init(), audio = gate_hash_init();
	int lag = 0, w = 0, h = 0, samples = 0;

	for (int f = 0; f < o->frames; f++)
	{
		for (int b = 0; b < c->button_count(); b++)
		{
			int on = 0;
			for (int i = 0; i < o->hold_count; i++) if (held[i] == b) on = 1;
			for (int i = 0; i < o->press_count; i++)
				if (pressed[i] == b && f >= o->press[i].from) on = 1;
			c->set_button(b, on);
		}
		if (c->axis_count() > 0) c->set_axis(0, o->stick_x);
		if (c->axis_count() > 1) c->set_axis(1, o->stick_y);

		c->pre_frame();
		c->frame();

		if (!c->input_was_read()) lag++;

		const uint32_t *px = c->video(&w, &h);
		video = gate_hash_feed(video, px, (size_t)w * h * sizeof(uint32_t));
		const int16_t *pcm = c->audio(&samples);
		audio = gate_hash_feed(audio, pcm, (size_t)samples * 2 * sizeof(int16_t));

		if (o->dump_frame >= 0 && f == o->dump_frame)
		{
			gate_write_ppm(o->dump_path, px, w, h);
			break;
		}

		if (o->digest_every > 0 && (f + 1) % o->digest_every == 0)
		{
			/* In a defined order: asking for the state SYNCHRONISES the machine,
			 * so a memory digest taken after it is a digest of a different
			 * moment. As printf's arguments are evaluated in whatever order the
			 * compiler likes, they cannot both be arguments. */
			uint64_t mem = gate_memory_digest(c);
			uint64_t state = gate_state_digest(c);
			printf("frame %6d  video %016llx  audio %016llx  memory %016llx  state %016llx  %dx%d  %d samples\n",
				f + 1, (unsigned long long)video, (unsigned long long)audio,
				(unsigned long long)mem, (unsigned long long)state, w, h, samples);
			fflush(stdout);
		}
	}

	if (o->dump_bus)
	{
		for (int b = 0; b < c->bus_count(); b++)
		{
			char path[512];
			snprintf(path, sizeof path, "%s.%d", o->dump_bus, b);
			FILE *f = fopen(path, "wb");
			if (!f) continue;
			int64_t size = c->bus_size(b);
			for (int64_t a = 0; a < size; a++) fputc(c->bus_peek(b, (int)a), f);
			fclose(f);
			fprintf(stderr, "wrote %s (%s, %lld bytes)\n", path, c->bus_name(b), (long long)size);
		}
	}

	/* Before any of the digests, and in particular before the state capture,
	 * which synchronises. */
	if (!o->quiet)
	{
		for (int b = 0; b < c->bus_count(); b++)
		{
			uint64_t h = gate_hash_init();
			int64_t size = c->bus_size(b);
			for (int64_t a = 0; a < size; a++)
			{
				uint8_t byte = (uint8_t)c->bus_peek(b, (int)a);
				h = gate_hash_feed(h, &byte, 1);
			}
			fprintf(stderr, "  bus    %-14s %016llx\n", c->bus_name(b), (unsigned long long)h);
		}
		for (int d = 0; d < c->domain_count(); d++)
		{
			fprintf(stderr, "  domain %-14s %016llx\n", c->domain_name(d),
				(unsigned long long)gate_hash_feed(gate_hash_init(), c->domain_ptr(d), (size_t)c->domain_size(d)));
		}
	}

	if (o->dump_state && c->state)
	{
		const uint8_t *data = NULL;
		int64_t size = 0;
		if (c->state(&data, &size) && data)
		{
			FILE *f = fopen(o->dump_state, "wb");
			if (f) { fwrite(data, 1, (size_t)size, f); fclose(f); }
			fprintf(stderr, "wrote %s (%lld bytes)\n", o->dump_state, (long long)size);
		}
	}

	/* Memory first, then the state - see the note above. */
	uint64_t mem = gate_memory_digest(c);
	uint64_t state = gate_state_digest(c);
	printf("video %016llx  audio %016llx  memory %016llx  state %016llx  lag %d/%d  %dx%d\n",
		(unsigned long long)video, (unsigned long long)audio,
		(unsigned long long)mem, (unsigned long long)state, lag, o->frames, w, h);
	return 0;
}

#endif
