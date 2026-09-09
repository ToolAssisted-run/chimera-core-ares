/* gate-harness.h - the schedule, shared by the reference and the sandbox.
 *
 * The gate's claim is "these two builds are the same machine". That claim is
 * only worth anything if the two are driven identically, so the loop, the
 * option parsing and the digests live here and each driver supplies nothing but
 * a handful of function pointers. A difference in the numbers is then a
 * difference in the MACHINE, which is the only thing being asked about.
 */
#ifndef CHIMERA_ARES_GATE_HARNESS_H
#define CHIMERA_ARES_GATE_HARNESS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Buttons, in the order waterbox.config declares them. */
enum
{
	GATE_BTN_POWER = 0,
	GATE_BTN_RESET,
	GATE_BTN_PAD0,
	GATE_BTN_PER_PAD = 16,
	GATE_BTN_COUNT = GATE_BTN_PAD0 + 4 * GATE_BTN_PER_PAD,
};

struct gate_core
{
	int (*init)(void);
	const char *(*load_error)(void);
	void (*set_button)(int32_t index, int32_t state);
	void (*set_axis)(int32_t index, int32_t value);
	void (*frame)(void);
	const uint32_t *(*video)(int *w, int *h);
	const int16_t *(*audio)(int *samples);
	int (*input_was_read)(void);
	int (*domain_count)(void);
	const char *(*domain_name)(int i);
	const uint8_t *(*domain_ptr)(int i);
	int64_t (*domain_size)(int i);
	void (*set_rendering)(int on);
	/* Run before every frame. The sandbox driver round-trips the machine
	 * through save/load here when --rerecord is given. */
	void (*pre_frame)(void);
};

struct gate_press
{
	int from;
	int button;
};

struct gate_opts
{
	int frames;
	int digest_every;
	int dump_frame;
	int quiet;
	int render;
	int stick_x, stick_y;
	int hold[GATE_BTN_COUNT];
	struct gate_press press[32];
	int press_count;
	const char *dump_path;
};

static const char *const gate_pad_button_names[GATE_BTN_PER_PAD] = {
	"up", "down", "left", "right", "a", "b", "z", "start",
	"l", "r", "cup", "cdown", "cleft", "cright", 0, 0,
};

/* "start" is pad 1; "p3:start" names another. */
static int gate_button_index(const char *name)
{
	int pad = 0;
	const char *bare = name;
	if (name[0] == 'p' && name[1] >= '1' && name[1] <= '4' && name[2] == ':')
	{
		pad = name[1] - '1';
		bare = name + 3;
	}
	if (!strcmp(bare, "power")) return GATE_BTN_POWER;
	if (!strcmp(bare, "reset")) return GATE_BTN_RESET;
	for (int i = 0; i < GATE_BTN_PER_PAD; i++)
	{
		if (gate_pad_button_names[i] && !strcmp(gate_pad_button_names[i], bare))
			return GATE_BTN_PAD0 + pad * GATE_BTN_PER_PAD + i;
	}
	return -1;
}

static void gate_usage(void)
{
	fprintf(stderr,
		"  --frames N        how many frames to run (default 60)\n"
		"  --hold BUTTON     hold a button for the whole run\n"
		"  --press F BUTTON  hold a button from frame F onwards\n"
		"  --stick X Y       hold pad 1's analogue stick at X,Y (-127..127)\n"
		"  --digest-every N  print a digest every N frames as well as at the end\n"
		"  --dump-frame F    write frame F as a .ppm and stop\n"
		"  --dump-to PATH    where --dump-frame writes (default frame.ppm)\n"
		"  --no-render       run with drawing off (turbo)\n"
		"  --quiet           only the final digest line\n"
		"Buttons: power, reset, up, down, left, right, a, b, z, start, l, r,\n"
		"         cup, cdown, cleft, cright; prefix p2:/p3:/p4: for other pads.\n");
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
		else if (!strcmp(a, "--no-render")) o->render = 0;
		else if (!strcmp(a, "--quiet")) o->quiet = 1;
		else if (!strcmp(a, "--stick")) { o->stick_x = atoi(GATE_NEXT()); o->stick_y = atoi(GATE_NEXT()); }
		else if (!strcmp(a, "--hold"))
		{
			const char *name = GATE_NEXT();
			int b = gate_button_index(name);
			if (b < 0) { fprintf(stderr, "unknown button '%s'\n", name); return 0; }
			o->hold[b] = 1;
		}
		else if (!strcmp(a, "--press"))
		{
			if (o->press_count >= (int)(sizeof o->press / sizeof *o->press))
			{
				fprintf(stderr, "too many --press\n");
				return 0;
			}
			int f = atoi(GATE_NEXT());
			const char *name = GATE_NEXT();
			int b = gate_button_index(name);
			if (b < 0) { fprintf(stderr, "unknown button '%s'\n", name); return 0; }
			o->press[o->press_count].from = f;
			o->press[o->press_count].button = b;
			o->press_count++;
		}
		/* Options the driver itself consumed; skipping them here keeps one
		 * parser rather than two that must agree. */
		else if (!strcmp(a, "--rom") || !strcmp(a, "--time") || !strcmp(a, "--port")) { (void)GATE_NEXT(); if (!strcmp(a, "--port")) (void)GATE_NEXT(); }
		else if (!strcmp(a, "--pal") || !strcmp(a, "--fast-vi") || !strcmp(a, "--rerecord") || !strcmp(a, "--report-stick")) { }
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

static int gate_run(const struct gate_core *c, const struct gate_opts *o)
{
	if (c->init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", c->load_error());
		return 1;
	}
	c->set_rendering(o->render);

	if (!o->quiet)
	{
		for (int i = 0; i < c->domain_count(); i++)
		{
			fprintf(stderr, "  domain %-14s %10lld bytes\n",
				c->domain_name(i), (long long)c->domain_size(i));
		}
	}

	uint64_t video = gate_hash_init(), audio = gate_hash_init();
	int lag = 0, w = 0, h = 0, samples = 0;

	for (int f = 0; f < o->frames; f++)
	{
		for (int b = 0; b < GATE_BTN_COUNT; b++)
		{
			int on = o->hold[b];
			for (int p = 0; p < o->press_count; p++)
				if (o->press[p].button == b && f >= o->press[p].from) on = 1;
			c->set_button(b, on);
		}
		c->set_axis(0, o->stick_x);
		c->set_axis(1, o->stick_y);

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
			uint64_t mem = gate_hash_init();
			for (int d = 0; d < c->domain_count(); d++)
				mem = gate_hash_feed(mem, c->domain_ptr(d), (size_t)c->domain_size(d));
			printf("frame %6d  video %016llx  audio %016llx  memory %016llx  %dx%d  %d samples\n",
				f + 1, (unsigned long long)video, (unsigned long long)audio,
				(unsigned long long)mem, w, h, samples);
			fflush(stdout);
		}
	}

	uint64_t mem = gate_hash_init();
	for (int d = 0; d < c->domain_count(); d++)
		mem = gate_hash_feed(mem, c->domain_ptr(d), (size_t)c->domain_size(d));

	printf("video %016llx  audio %016llx  memory %016llx  lag %d/%d  %dx%d\n",
		(unsigned long long)video, (unsigned long long)audio, (unsigned long long)mem,
		lag, o->frames, w, h);
	return 0;
}

#endif
