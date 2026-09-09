/* run-native - the reference. ares outside the sandbox, driven by the same
 * harness the sandbox driver uses, so that every gate leg can ask "did the
 * sandbox change the answer?" and get a number rather than an opinion.
 */
#include "machine.h"
#include "gate-harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
	bool g_buttons[GATE_BTN_COUNT];
	int g_axes[8];

	int core_init(void) { return 1; }  /* the machine is built before gate_run */
	const char *core_load_error(void) { return machine::error(); }
	void core_set_button(int32_t i, int32_t s) { if (i >= 0 && i < GATE_BTN_COUNT) g_buttons[i] = s != 0; }
	void core_set_axis(int32_t i, int32_t v) { if (i >= 0 && i < 8) g_axes[i] = v; }

	void core_frame(void)
	{
		machine::Input input = {};
		input.power = g_buttons[GATE_BTN_POWER];
		input.reset = g_buttons[GATE_BTN_RESET];
		for (int pad = 0; pad < 4; pad++)
		{
			const bool *b = &g_buttons[GATE_BTN_PAD0 + pad * GATE_BTN_PER_PAD];
			machine::Pad &p = input.pad[pad];
			p.up = b[0];  p.down = b[1];  p.left = b[2];  p.right = b[3];
			p.a = b[4];   p.b = b[5];     p.z = b[6];     p.start = b[7];
			p.l = b[8];   p.r = b[9];
			p.cUp = b[10]; p.cDown = b[11]; p.cLeft = b[12]; p.cRight = b[13];
			int x = g_axes[pad * 2 + 0], y = g_axes[pad * 2 + 1];
			if (x < -127) x = -127; if (x > 127) x = 127;
			if (y < -127) y = -127; if (y > 127) y = 127;
			p.x = (int8_t)x;
			p.y = (int8_t)y;
		}
		machine::frame(input);
	}

	const uint32_t *core_video(int *w, int *h)
	{
		*w = machine::videoWidth();
		*h = machine::videoHeight();
		return machine::video();
	}

	const int16_t *core_audio(int *n) { *n = machine::audioSamples(); return machine::audio(); }
	int core_input_was_read(void) { return machine::inputWasRead() ? 1 : 0; }
	int core_domain_count(void) { return machine::memoryDomainCount(); }
	const char *core_domain_name(int i) { return machine::memoryDomainName(i); }
	const uint8_t *core_domain_ptr(int i) { return machine::memoryDomainData(i); }
	int64_t core_domain_size(int i) { return machine::memoryDomainSize(i); }
	void core_set_rendering(int on) { machine::setRenderingEnabled(on != 0); }
	void core_pre_frame(void) { }

	machine::Port portKind(const char *name)
	{
		if (!strcmp(name, "none")) return machine::Port::Unplugged;
		if (!strcmp(name, "gamepad")) return machine::Port::Gamepad;
		if (!strcmp(name, "cpak")) return machine::Port::GamepadWithControllerPak;
		if (!strcmp(name, "rpak")) return machine::Port::GamepadWithRumblePak;
		if (!strcmp(name, "mouse")) return machine::Port::Mouse;
		fprintf(stderr, "run-native: unknown port kind '%s'\n", name);
		exit(2);
	}
}

int main(int argc, char **argv)
{
	machine::Config config = {};
	config.port[0] = machine::Port::Gamepad;

	/* The options that describe the MACHINE are this driver's; the ones that
	 * describe the RUN are the harness'. */
	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		if (!strcmp(a, "--rom") && i + 1 < argc) config.romFile = argv[++i];
		else if (!strcmp(a, "--pal")) config.pal = true;
		else if (!strcmp(a, "--fast-vi")) config.fastVI = true;
		else if (!strcmp(a, "--time") && i + 1 < argc) config.initTimeUnix = strtoull(argv[++i], nullptr, 10);
		else if (!strcmp(a, "--port") && i + 2 < argc)
		{
			int which = atoi(argv[++i]);
			const char *kind = argv[++i];
			if (which < 1 || which > 4) { fprintf(stderr, "run-native: --port takes 1..4\n"); return 2; }
			config.port[which - 1] = portKind(kind);
		}
	}

	if (!config.romFile)
	{
		fprintf(stderr, "usage: run-native --rom FILE [options]\n"
			"  --pal             a PAL machine (default NTSC)\n"
			"  --time N          the RTC's starting Unix time (default 0)\n"
			"  --port N KIND     none, gamepad, cpak, rpak or mouse\n"
			"  --fast-vi         skip the VI's filtering\n");
		gate_usage();
		return 2;
	}

	struct gate_opts opts;
	if (!gate_parse_opts(argc, argv, 1, &opts)) return 2;

	if (!machine::init(config))
	{
		fprintf(stderr, "run-native: %s\n", machine::error());
		return 1;
	}
	if (!opts.quiet)
	{
		fprintf(stderr, "run-native: %s, %s, %d frames\n",
			config.romFile, config.pal ? "PAL" : "NTSC", opts.frames);
	}

	struct gate_core core = {
		core_init, core_load_error, core_set_button, core_set_axis, core_frame,
		core_video, core_audio, core_input_was_read, core_domain_count,
		core_domain_name, core_domain_ptr, core_domain_size, core_set_rendering,
		core_pre_frame,
	};
	return gate_run(&core, &opts);
}
