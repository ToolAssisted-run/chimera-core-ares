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
			"  --fast-vi         skip the Nintendo 64's video filtering\n");
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
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--report-stick")) reportStick = true;

	struct gate_core core = {
		core_init, core_load_error, core_button_count, core_button_name,
		core_axis_count, core_axis_name, core_set_button, core_set_axis, core_frame,
		core_video, core_audio, core_input_was_read,
		core_domain_count, core_domain_name, core_domain_ptr, core_domain_size,
		core_bus_count, core_bus_name, core_bus_size, core_bus_peek, core_state,
		core_set_rendering, core_pre_frame,
	};
	int rc = gate_run(&core, &opts);
	if (reportStick)
	{
		int x = 0, y = 0;
		if (machine::padReport(0, &x, &y)) printf("stick reported x=%d y=%d\n", x, y);
		else printf("stick reported none\n");
	}
	return rc;
}
