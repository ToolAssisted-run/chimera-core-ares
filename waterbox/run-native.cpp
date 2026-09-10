/* run-native - the reference. ares outside the sandbox, driven by the same
 * harness the sandbox driver uses, so every gate leg can ask "did the sandbox
 * change the answer?" and get a number rather than an opinion.
 */
#include "machine.h"
#include "machines-names.h"
#include "gate-harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
	int core_init(void) { return 1; }  /* the machine is built before gate_run */
	const char *core_load_error(void) { return machine::error(); }
	int core_button_count(void) { return machine::buttonCount(); }
	const char *core_button_name(int i) { return machines::buttonName(i); }
	int core_axis_count(void) { return machine::axisCount(); }
	const char *core_axis_name(int i) { return machines::axisName(i); }
	void core_set_button(int i, int s) { machine::setButton(i, s != 0); }
	void core_set_axis(int i, int v) { machine::setAxis(i, v); }
	void core_frame(void) { machine::frame(); }

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
	int core_bus_count(void) { return machine::busCount(); }
	const char *core_bus_name(int i) { return machine::busName(i); }
	int64_t core_bus_size(int i) { return machine::busSize(i); }
	int core_bus_peek(int b, int a) { return machine::busPeek(b, a); }
	int core_state(const uint8_t **d, int64_t *n) { return machine::captureState(d, n) ? 1 : 0; }
	void core_set_rendering(int on) { machine::setRenderingEnabled(on != 0); }
	void core_pre_frame(void) { }
}

/* --set key=value, for ares' own machine settings. Small and fixed: a machine
 * declares a handful, and a test that needs more than sixteen is a test that
 * wants a settings file. */
static struct { char key[64]; char value[128]; } g_sets[16];
static int g_setCount = 0;

static bool lookupSet(const char *key, char *out, int outSize)
{
	for (int i = 0; i < g_setCount; i++)
	{
		if (strcmp(g_sets[i].key, key) != 0) continue;
		snprintf(out, (size_t)outSize, "%s", g_sets[i].value);
		return true;
	}
	return false;
}

int main(int argc, char **argv)
{
	machine::Config config = {};
	config.machine = "N64";

	int portsGiven = 0;

	/* The options that describe the MACHINE are this driver's; the ones that
	 * describe the RUN are the harness'. */
	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		if (!strcmp(a, "--rom") && i + 1 < argc) config.romFile = argv[++i];
		else if (!strcmp(a, "--firmware") && i + 1 < argc) config.firmwareFile = argv[++i];
		else if (!strcmp(a, "--machine") && i + 1 < argc) config.machine = argv[++i];
		else if (!strcmp(a, "--pal")) config.pal = true;
		else if (!strcmp(a, "--fast-vi")) config.fastVI = true;
		else if (!strcmp(a, "--time") && i + 1 < argc) config.initTimeUnix = strtoull(argv[++i], nullptr, 10);
		else if (!strcmp(a, "--set") && i + 1 < argc)
		{
			/* --set gb.fastBoot=true: one of ares' own settings, by the key the
			 * package declares it under. The sandbox reads these out of the
			 * mounted settings file; the reference has no file, so it takes
			 * them here and the two flavours can be compared. */
			if (g_setCount < (int)(sizeof g_sets / sizeof g_sets[0]))
			{
				const char *pair = argv[++i];
				const char *eq = strchr(pair, '=');
				if (eq == nullptr) { fprintf(stderr, "run-native: --set wants key=value\n"); return 2; }
				size_t klen = (size_t)(eq - pair);
				if (klen >= sizeof g_sets[0].key) klen = sizeof g_sets[0].key - 1;
				memcpy(g_sets[g_setCount].key, pair, klen);
				g_sets[g_setCount].key[klen] = 0;
				snprintf(g_sets[g_setCount].value, sizeof g_sets[0].value, "%s", eq + 1);
				g_setCount++;
			}
			else { fprintf(stderr, "run-native: too many --set\n"); return 2; }
		}
		else if (!strcmp(a, "--port") && i + 2 < argc)
		{
			int which = atoi(argv[++i]);
			const char *device = argv[++i];
			if (which < 1 || which > 8) { fprintf(stderr, "run-native: --port takes 1..8\n"); return 2; }
			config.port[which - 1] = strcmp(device, "none") ? device : "";
			portsGiven = 1;
		}
	}

	/* Nothing said about the ports leaves them all null, and the machine puts
	 * its own ordinary controller in the first one. */
	(void)portsGiven;

	if (!config.romFile && !machines::bootsWithoutMedium(config.machine))
	{
		fprintf(stderr, "usage: run-native --rom FILE [options]\n"
			"  --machine ID      which console (default N64); see waterbox/machines.h\n"
			"  --firmware FILE   the console BIOS, for a machine that needs one\n"
			"  --pal             a PAL machine, where the console has one\n"
			"  --time N          a cartridge clock's starting Unix time\n"
			"  --port N DEVICE   what to plug into port N, by ares' name, or none\n"
			"  --set KEY=VALUE   one of ares' own settings, e.g. gb.fastBoot=true\n"
			"  --fast-vi         skip the Nintendo 64's video filtering\n");
		gate_usage();
		return 2;
	}

	struct gate_opts opts;
	if (!gate_parse_opts(argc, argv, 1, &opts)) return 2;

	config.lookupSetting = lookupSet;

	if (!machine::init(config))
	{
		fprintf(stderr, "run-native: %s\n", machine::error());
		return 1;
	}
	machines::useMachine(config.machine);

	if (!opts.quiet)
	{
		fprintf(stderr, "run-native: %s on %s, %s, %d frames, %.4fHz (%d/%d)\n",
			config.romFile, config.machine, config.pal ? "PAL" : "NTSC", opts.frames,
			(double)machine::vsyncNumerator() / machine::vsyncDenominator(),
			machine::vsyncNumerator(), machine::vsyncDenominator());
	}

	/* --report-stick: run, then say what the pad reported. Nothing else in the
	 * run changes, so the digests stay comparable. */
	bool reportStick = false;
	bool reportRefresh = false;
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--report-stick")) reportStick = true;
		if (!strcmp(argv[i], "--report-refresh")) reportRefresh = true;
	}

	struct gate_core core = {
		core_init, core_load_error, core_button_count, core_button_name,
		core_axis_count, core_axis_name, core_set_button, core_set_axis, core_frame,
		core_video, core_audio, core_input_was_read,
		core_domain_count, core_domain_name, core_domain_ptr, core_domain_size,
		core_bus_count, core_bus_name, core_bus_size, core_bus_peek, core_state,
		core_set_rendering, core_pre_frame,
	};
	int rc = gate_run(&core, &opts);
	if (reportRefresh)
	{
		/* Read AFTER the run: a machine that works its rate out from the frame
		 * it has just drawn has said something by now, and that is the number
		 * the declaration in machines.h has to agree with. */
		int dn = 0, dd = 0;
		machine::declaredRefresh(&dn, &dd);
		printf("refresh declared %d/%d observed %d/%d\n",
			dn, dd, machine::vsyncNumerator(), machine::vsyncDenominator());
	}
	if (reportStick)
	{
		int x = 0, y = 0;
		if (machine::padReport(0, &x, &y)) printf("stick reported x=%d y=%d\n", x, y);
		else printf("stick reported none\n");
	}
	return rc;
}
