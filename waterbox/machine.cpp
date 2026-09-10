/* machine.cpp - ares, driven as a machine rather than as an application.
 *
 * ares is written to be hosted: a Platform supplies its files, takes its
 * pictures and sound, and is told what the machine's inputs are doing. That is
 * exactly the shape a waterbox core needs, so most of this file is that
 * Platform plus the loading sequence ares' own desktop-ui performs, generalised
 * over every console in waterbox/machines.h.
 *
 * Two things are deliberately NOT ares'. The Nintendo 64's display processor is
 * angrylion's software rasteriser (patches/ares/0004), because ares' own is
 * paraLLEl-RDP on Vulkan and a movie cannot be replayed on somebody else's
 * graphics driver. And the bindings between this core's declared inputs and
 * ares' nodes are generated (waterbox/machines.inc), because the order of a
 * controller's buttons is what a movie is written in.
 */
#include "machine.h"
#include "machines.h"
#include "machines.inc"

#include <mia/mia.hpp>

#include <angrylion.h>

#include <string.h>
#include <string>

using namespace ares;

namespace
{
	/* The Nintendo 64's FPU rounding mode is machine state, and ares sets the
	 * HOST's rounding mode to match it while the CPU runs. Anything that runs
	 * between frames expects round-to-nearest, so the mode is swapped in on the
	 * way in and out again on the way out. Without this a savestate taken
	 * mid-run reloads under whatever mode the last game left behind, and
	 * floating point stops being reproducible.
	 */
	struct FenvGuard
	{
		nall::float_env saved;
		nall::float_env *machine;

		FenvGuard(nall::float_env *machine_) : machine(machine_)
		{
			if (machine && machine->getRound() != saved.getRound()) machine->setRound(machine->getRound());
		}

		~FenvGuard()
		{
			if (machine && machine->getRound() != saved.getRound()) saved.setRound(saved.getRound());
		}
	};

	/* Wide enough for the largest picture any declared machine draws; the
	 * package's video.width/height say the same number, generated from the same
	 * table. */
	constexpr int VideoWidthMax = 1280;
	constexpr int VideoHeightMax = 576;
	constexpr int MaxSamplesPerFrame = 4096;

	char g_error[256] = "";
	bool fail(const char *why) { snprintf(g_error, sizeof g_error, "%s", why); return false; }

	int16_t g_audio[MaxSamplesPerFrame * 2];
	int g_audioSamples = 0;

	uint32_t g_video[VideoWidthMax * VideoHeightMax];
	int g_videoWidth = 0, g_videoHeight = 0;
	bool g_rendering = true;

	bool g_inputWasRead = false;
	bool g_pal = false;
	double g_refreshRate = 0.0;
	const machines::Spec *g_spec = nullptr;
	const machines::MachineInputs *g_inputs = nullptr;
	bool g_isN64 = false;

	std::shared_ptr<mia::Pak> g_systemPak;
	std::shared_ptr<mia::Pak> g_cartridgePak;
	Node::System g_root;

	/* What each declared input binds to, resolved once at init. A null entry is
	 * an input the machine does not have this time - a port left empty. */
	std::vector<Node::Input::Button> g_buttons;
	std::vector<Node::Input::Axis> g_axes;
	std::vector<Node::Audio::Stream> g_streams;

	struct ChimeraPlatform : ares::Platform
	{
		auto attach(Node::Object node) -> void override
		{
			/* ares resamples its audio to whatever the host asks for. 44100 is
			 * what the package declares. */
			if (auto stream = node->cast<Node::Audio::Stream>())
			{
				stream->setResamplerFrequency(44100);
				g_streams.push_back(stream);
			}
		}

		auto detach(Node::Object node) -> void override
		{
			if (auto stream = node->cast<Node::Audio::Stream>())
			{
				for (auto it = g_streams.begin(); it != g_streams.end(); ++it)
					if (*it == stream) { g_streams.erase(it); break; }
			}
		}

		auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override
		{
			/* The system node is the machine itself; anything else asking is the
			 * medium in its slot. */
			if (g_root && node == g_root) return g_systemPak ? g_systemPak->pak : nullptr;
			return g_cartridgePak ? g_cartridgePak->pak : nullptr;
		}

		/* A machine can have more than one of these - a Mega Drive has the
		 * YM2612 and the PSG, a Mega CD adds two more - and they are separate
		 * voices of one output, not separate outputs. So a sample is taken from
		 * EVERY stream and the samples are summed, and only when every stream
		 * has one waiting: reading them out of step lets the streams slide
		 * against each other. That is ares' own rule, in desktop-ui.
		 *
		 * ares calls this once per stream, so it runs as often as there are
		 * voices and does nothing on all but the call that completes the set.
		 *
		 * Taking each stream's samples in turn instead - which is what this did
		 * - concatenates the voices rather than mixing them: a Mega Drive frame
		 * came out twice as long, PSG followed by FM, and the frontend heard
		 * both at double speed. */
		auto audio(Node::Audio::Stream) -> void override
		{
			if (g_streams.empty()) return;

			auto clamp16 = [](double v) -> int16_t {
				double scaled = v * 32768.0;
				if (scaled > 32767.0) scaled = 32767.0;
				if (scaled < -32768.0) scaled = -32768.0;
				return (int16_t)scaled;
			};

			for (;;)
			{
				for (auto &stream : g_streams) if (!stream->pending()) return;

				double left = 0.0, right = 0.0;
				for (auto &stream : g_streams)
				{
					/* read() only writes as many channels as the stream has, so
					 * a monaural voice leaves the second slot untouched - it has
					 * to be zeroed here and doubled, or the right channel is
					 * whatever the stack held. */
					double buffer[2] = {0.0, 0.0};
					uint32_t channels = stream->read(buffer);
					left += buffer[0];
					right += channels == 1 ? buffer[0] : buffer[1];
				}

				if (g_audioSamples >= MaxSamplesPerFrame) continue;
				g_audio[g_audioSamples * 2 + 0] = clamp16(left);
				g_audio[g_audioSamples * 2 + 1] = clamp16(right);
				g_audioSamples++;
			}
		}

		/* Everything but the Nintendo 64 draws through ares' own screen node,
		 * which hands over finished ARGB8888 rows. */
		auto video(Node::Video::Screen, const u32 *data, u32 pitch, u32 width, u32 height) -> void override
		{
			if (!g_rendering || data == nullptr) return;
			if (width > VideoWidthMax) width = VideoWidthMax;
			if (height > VideoHeightMax) height = VideoHeightMax;
			u32 stride = pitch / sizeof(u32);
			for (u32 y = 0; y < height; y++)
			{
				memcpy(g_video + (size_t)y * width, data + (size_t)y * stride, width * sizeof(u32));
			}
			g_videoWidth = (int)width;
			g_videoHeight = (int)height;
		}

		/* Every machine tells ares what it actually refreshes at, out of its own
		 * clock: a Game Boy is 4194304/(456*154) = 59.7275Hz, not 60. The
		 * frontend needs that for the length of a movie and for keeping sound in
		 * step with picture, so it is taken from the machine rather than
		 * guessed. */
		auto refreshRateHint(double refreshRate) -> void override
		{
			/* A television's band. The Atari 2600 counts the lines its program
			 * actually drew, and the first frames of a cartridge are not a
			 * picture yet - one of them hinted 4.6Hz - so a figure outside what
			 * a screen could be is not an answer, it is a machine still waking
			 * up. 75.5Hz for the WonderSwan is the highest anything here says. */
			if (refreshRate > 20.0 && refreshRate < 130.0) g_refreshRate = refreshRate;
		}

		auto input(Node::Input::Input node) -> void override
		{
			/* A frame in which the machine polled a controller is a frame the
			 * player's input reached. ares announces every read. */
			(void)node;
			g_inputWasRead = true;
		}
	};

	ChimeraPlatform *g_platform = nullptr;

	nall::float_env *machineFenv(void)
	{
		return g_isN64 ? &Nintendo64::cpu.fenv : nullptr;
	}

	/* One setting has to name a device across twenty-one machines whose devices
	 * are not called the same thing - a Nintendo 64 has a "Gamepad", a Mega
	 * Drive a "Control Pad", a Neo Geo an "Arcade Stick" - and the setting
	 * carries an id, not ares' own display name. So the two are matched on
	 * letters alone: case ignored, and spaces dropped, which turns "controlPad"
	 * and "Control Pad" into the same word.
	 *
	 * Without this the settings were the Nintendo 64's, spelt its way, and a
	 * project that carried them - which every project the frontend makes does,
	 * because a declared setting always has a value - refused to load on every
	 * machine with a controller port in it. The gate never saw it: the gate
	 * writes only {"machine": ...} and leaves the ports unmentioned. */
	static bool sameDeviceName(const char *a, const nall::string &b)
	{
		const char *p = a;
		const char *q = b.data();
		for (;;)
		{
			while (*p == ' ' || *p == '-' || *p == '_') p++;
			while (*q == ' ' || *q == '-' || *q == '_') q++;
			if (*p == 0 || *q == 0) return *p == 0 && *q == 0;
			char lp = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
			char lq = (*q >= 'A' && *q <= 'Z') ? (char)(*q + 32) : *q;
			if (lp != lq) return false;
			p++; q++;
		}
	}

	/* Writes back whatever the project said about ares' own settings.
	 *
	 * These are not decoration. A Game Boy's DMG revision, a Master System's VDP
	 * revision, whether the boot ROM is skipped, whether frames are blended into
	 * each other - each changes what the machine does or what it draws, so each
	 * has to be part of what a movie pins. The package declares them (they are
	 * enumerated out of ares by gen-machines), the frontend offers the ones that
	 * belong to the machine being loaded, and this puts them back.
	 *
	 * Before power(), because that is when ares latches a setting into the
	 * hardware it configures. */
	void applySettings(const machine::Config &config)
	{
		if (config.lookupSetting == nullptr || g_inputs == nullptr) return;
		for (int i = 0; i < g_inputs->settingCount; i++)
		{
			const auto &decl = g_inputs->settings[i];
			char value[128];
			if (!config.lookupSetting(decl.key, value, (int)sizeof value)) continue;

			/* find() matches a node's EXACT identity, and Setting is the base -
			 * a Boolean answers to "setting.boolean" and never to "setting", so
			 * asking for the base finds nothing at all. Each concrete kind is
			 * asked in turn; writeValue is the shared interface once one
			 * answers, so nothing here needs to know which it was. */
			if (auto n = g_root->find<Node::Setting::Boolean>(decl.path)) { n->writeValue(value); continue; }
			if (auto n = g_root->find<Node::Setting::Natural>(decl.path)) { n->writeValue(value); continue; }
			if (auto n = g_root->find<Node::Setting::Integer>(decl.path)) { n->writeValue(value); continue; }
			if (auto n = g_root->find<Node::Setting::Real>(decl.path))    { n->writeValue(value); continue; }
			if (auto n = g_root->find<Node::Setting::String>(decl.path))  { n->writeValue(value); continue; }
		}
	}

	/* Plugs in whatever the config asked for, port by port, and binds the
	 * declared inputs to the nodes that now exist. */
	bool connectPorts(const machine::Config &config)
	{
		/* Nothing said at all: the machine's ordinary controller goes in its
		 * first port, which is what almost every run wants. What that controller
		 * is called differs between machines, so it is asked of the port rather
		 * than assumed. "default" says the same thing out loud, and is what the
		 * package ships as port 1's value. */
		bool nobodySaid = true;
		for (auto &p : config.port) if (p != nullptr) nobodySaid = false;

		int index = 0;
		for (auto &port : g_root->find<Node::Port>())
		{
			if (port->supported().empty()) continue;  /* a media slot, not a controller port */
			if (index >= 8) break;
			const char *device = config.port[index];
			const char *accessory = config.portAccessory[index];
			nall::string first;
			bool wantsDefault = device != nullptr && sameDeviceName(device, nall::string{"default"});
			if (wantsDefault || (device == nullptr && index == 0 && nobodySaid))
			{
				first = port->supported().front();
				device = first;
			}
			else if (device == nullptr) device = "";
			index++;
			if (!*device) continue;

			/* The id has to become the name ares knows this port's device by. */
			nall::string resolved;
			for (auto &supported : port->supported())
				if (sameDeviceName(device, supported)) { resolved = supported; break; }
			if (!resolved) return fail("this machine has no such device for that port");

			auto peripheral = port->allocate(resolved);
			if (!peripheral) return fail("this machine has no such device for that port");
			port->connect();

			if (accessory != nullptr && *accessory)
			{
				bool plugged = false;
				for (auto &slot : peripheral->find<Node::Port>())
				{
					slot->allocate(accessory);
					slot->connect();
					plugged = true;
					break;
				}
				if (!plugged) return fail("this device has no slot for that accessory");
			}
		}
		return true;
	}

	void bindInputs(void)
	{
		g_buttons.clear();
		g_axes.clear();
		if (g_inputs == nullptr) return;
		for (int i = 0; i < g_inputs->buttonCount; i++)
		{
			g_buttons.push_back(g_root->find<Node::Input::Button>(g_inputs->buttons[i].path));
		}
		for (int i = 0; i < g_inputs->axisCount; i++)
		{
			g_axes.push_back(g_root->find<Node::Input::Axis>(g_inputs->axes[i].path));
		}
	}

	/* ---- what the machine is made of ---- */

	struct Domain { const char *name; uint8_t *data; int64_t size; int writable; };
	Domain g_domains[8];
	int g_domainCount = 0;

	void publish(const char *name, void *data, int64_t size, int writable)
	{
		if (!data || size <= 0) return;
		if (g_domainCount >= (int)(sizeof g_domains / sizeof *g_domains)) return;
		g_domains[g_domainCount++] = {name, (uint8_t *)data, size, writable};
	}

	/* Only the Nintendo 64 publishes blocks: its memory IS blocks, and a
	 * digest over eight megabytes wants a pointer rather than eight million
	 * calls. Every other machine is covered by the buses below, which ares
	 * gives us for free. */
	void publishDomains(void)
	{
		g_domainCount = 0;
		if (!g_isN64) return;
		using namespace ares::Nintendo64;
		publish("RDRAM", rdram.ram.data, (int64_t)rdram.ram.size, 1);
		publish("RSP DMEM", rsp.dmem.data, (int64_t)rsp.dmem.size, 1);
		publish("RSP IMEM", rsp.imem.data, (int64_t)rsp.imem.size, 1);
		publish("PIF RAM", pif.ram.data, (int64_t)pif.ram.size, 1);
		publish("Cartridge ROM", cartridge.rom.data, (int64_t)cartridge.rom.size, 0);
		publish("SRAM", cartridge.ram.data, (int64_t)cartridge.ram.size, 1);
		publish("EEPROM", cartridge.eeprom.data, (int64_t)cartridge.eeprom.size, 1);
		publish("Flash", cartridge.flash.data, (int64_t)cartridge.flash.size, 1);
	}

	std::vector<Node::Debugger::Memory> g_buses;
	/* ares' node->name() hands back a string BY VALUE, so a (const char*) taken
	 * from it dangles the moment the expression ends. The names are copied here
	 * once instead, where they live as long as the machine does. */
	std::vector<std::string> g_busNames;

	void publishBuses(void)
	{
		g_buses = g_root->find<Node::Debugger::Memory>();
		g_busNames.clear();
		for (auto &bus : g_buses) g_busNames.push_back((const char *)bus->name());
	}
}

namespace machine
{
	const char *error(void) { return g_error; }

	bool init(const Config &config)
	{
		g_error[0] = 0;

		g_spec = machines::find(config.machine ? config.machine : "N64");
		if (g_spec == nullptr) return fail("this core has no such machine");
		g_isN64 = nall::string{g_spec->id} == "N64";
		g_pal = config.pal && g_spec->configPal != nullptr;
		g_refreshRate = 0.0;
		g_streams.clear();

		g_inputs = nullptr;
		for (auto &entry : kMachineInputs)
		{
			if (nall::string{entry.id} == g_spec->id) g_inputs = &entry;
		}

		g_platform = new ChimeraPlatform;
		ares::platform = g_platform;

		/* mia is ares' own media layer: it reads the cartridge, works out its
		 * region and what kind of save chip it has, and hands back the pak the
		 * machine expects. Using it rather than a loader of our own is what
		 * makes each additional machine a table entry. */
		g_systemPak = mia::System::create(g_spec->miaSystem);
		if (!g_systemPak) return fail("ares has no such system");
		/* A machine that needs a console BIOS is handed the file it was mounted
		 * as; one that carries its own boot code is handed nothing. */
		string firmware;
		if (g_spec->firmware != nullptr)
		{
			/* The frontend mounts a firmware under the id the package declares
			 * it by, so the id IS the file name and nothing has to be passed.
			 * run-native overrides it with a host path, which is the only place
			 * a path is ever spoken. */
			firmware = (config.firmwareFile && *config.firmwareFile) ? config.firmwareFile
			                                                        : g_spec->firmware;
		}
		if (g_systemPak->load(firmware) != successful)
		{
			return fail(g_spec->firmware ? "the console BIOS would not load"
			                             : "this machine's system pak would not load");
		}

		/* The reference runner's PIF ROM, if it was given one: the machine then
		 * boots the way hardware does, which is what the boot leg compares the
		 * HLE boot against. Nothing in a shipped core ever sets this. */
		if (config.pifRomFile && *config.pifRomFile && g_isN64)
		{
			auto rom = file::read(config.pifRomFile);
			if (rom.empty()) return fail("the PIF boot ROM would not open");
			g_systemPak->pak->append(config.pal ? "pif.pal.rom" : "pif.ntsc.rom", rom);
		}

		bool haveMedium = config.romFile != nullptr && *config.romFile;
		if (!haveMedium && !g_spec->bootsWithoutMedium) return fail("no cartridge given");
		if (haveMedium)
		{
			g_cartridgePak = mia::Medium::create(g_spec->miaMedium);
			if (!g_cartridgePak) return fail("ares has no such medium");
			/* mia says WHY, and the reason is the difference between "this file is
			 * not for this machine" and "this machine wants a database this core
			 * does not carry". Passing it on costs nothing and saves an hour. */
			auto result = g_cartridgePak->load(config.romFile);
			if (result != successful)
			{
				static const char *why[] = {
					"loaded", "no file was chosen", "its game database is missing",
					"it is not in the game database", "the file was not found",
					"the file is not a game this machine takes",
					"this core does not support that medium",
					"that file is for a different machine",
					"its manifest could not be read", "it needs firmware", "it would not load",
				};
				int at = (int)result.result;
				const char *reason = (at >= 0 && at < (int)(sizeof why / sizeof *why))
					? why[at] : "it would not load";
				if (result.info)
				{
					snprintf(g_error, sizeof g_error, "the game would not load: %s (%s)",
						reason, (const char *)result.info);
					return false;
				}
				snprintf(g_error, sizeof g_error, "the game would not load: %s", reason);
				return false;
			}
		}

		if (g_isN64)
		{
			/* The picture is the rasteriser's, written straight into the buffer
			 * the frontend reads. */
			angrylion::OutFrameBuffer = g_video;
			angrylion::OutHeight = g_pal ? 576 : 480;
			Nintendo64::FastVI = config.fastVI;
			Nintendo64::cpu.bootProbe = config.bootProbe;
			Nintendo64::BobDeinterlace = config.bobDeinterlace;
			Nintendo64::rtcEpoch = config.initTimeUnix;
			g_videoWidth = 640;
			g_videoHeight = g_pal ? 576 : 480;

			/* Real hardware powers on with genuinely random RDRAM timings, and
			 * ares models that by seeding its RNG from the host clock. A movie
			 * cannot be replayed against a machine that starts differently every
			 * time, so the seed is pinned. Without it the same run produces a
			 * different machine on every boot, and only the picture happens to
			 * agree. */
			Nintendo64::option("Deterministic Entropy", "true");
		}

		const char *name = g_pal ? g_spec->configPal : g_spec->configNtsc;
		if (name == nullptr) name = g_spec->configNtsc ? g_spec->configNtsc : g_spec->configPal;
		if (!g_spec->load(g_root, name)) return fail("ares would not build the machine");

		/* Every machine has a slot for its medium, and it is the one port that
		 * takes no device name. */
		for (auto &port : g_root->find<Node::Port>())
		{
			if (!port->supported().empty()) continue;
			port->allocate();
			port->connect();
		}

		if (!connectPorts(config)) return false;
		applySettings(config);

		FenvGuard guard(machineFenv());
		g_root->power();
		bindInputs();
		publishDomains();
		publishBuses();
		return true;
	}

	void setButton(int index, bool held)
	{
		if (index < 0 || index >= (int)g_buttons.size()) return;
		if (auto &node = g_buttons[index]) node->setValue(held);
	}

	void setAxis(int index, int value)
	{
		if (index < 0 || index >= (int)g_axes.size()) return;
		if (auto &node = g_axes[index]) node->setValue(value);
	}

	int buttonCount(void) { return g_inputs ? g_inputs->buttonCount : 0; }
	int axisCount(void) { return g_inputs ? g_inputs->axisCount : 0; }
	bool buttonActive(int i) { return i >= 0 && i < (int)g_buttons.size() && (bool)g_buttons[i]; }
	bool axisActive(int i) { return i >= 0 && i < (int)g_axes.size() && (bool)g_axes[i]; }

	void frame(void)
	{
		FenvGuard guard(machineFenv());
		if (g_isN64) angrylion::OutFrameBuffer = g_rendering ? g_video : nullptr;

		g_inputWasRead = false;
		g_audioSamples = 0;

		g_root->run();

		if (g_isN64)
		{
			g_videoWidth = 640;
			g_videoHeight = (int)angrylion::OutHeight;
		}
	}

	const uint32_t *video(void) { return g_video; }
	int videoWidth(void) { return g_videoWidth; }
	int videoHeight(void) { return g_videoHeight; }
	void setRenderingEnabled(bool on) { g_rendering = on; }

	const int16_t *audio(void) { return g_audio; }
	int audioSamples(void) { return g_audioSamples; }
	bool inputWasRead(void) { return g_inputWasRead; }

	/* The machine's own refresh, as an exact-as-possible rational. ares hands it
	 * over as a double computed from integers - a pixel clock over dots times
	 * lines - so a continued fraction recovers the original ratio rather than
	 * approximating it: 59.7275Hz comes back as 4194304/70224, not as a
	 * six-decimal-place near miss.
	 *
	 * A machine that never said gets the nominal rate for its region, which is
	 * what the Nintendo 64 does (ares hints a flat 50 or 60 there itself). */
	static void refreshAsRational(int *numerator, int *denominator)
	{
		double rate = g_refreshRate;
		if (!(rate > 20.0 && rate < 130.0))
		{
			/* The machine has not said. A few cannot say in time - see
			 * machines.h - and those declare theirs; the rest fall back to the
			 * nominal rate for their region. */
			int num = g_spec ? (g_pal ? g_spec->refreshPalNum : g_spec->refreshNtscNum) : 0;
			int den = g_spec ? (g_pal ? g_spec->refreshPalDen : g_spec->refreshNtscDen) : 0;
			if (num > 0 && den > 0) { *numerator = num; *denominator = den; return; }
			*numerator = g_pal ? 50 : 60;
			*denominator = 1;
			return;
		}

		/* Continued fractions, stopping at the first convergent that is right to
		 * a part in a billion or when the denominator would stop being sane. */
		/* h(-1)=1, h(-2)=0; k(-1)=0, k(-2)=1 - the standard seeds, and getting
		 * k wrong turns 59.7Hz into 0.98Hz without complaining. */
		int64_t bestNum = 1, bestDen = 0;
		int64_t prevNum = 0, prevDen = 1;
		double value = rate;
		for (int i = 0; i < 32; i++)
		{
			int64_t whole = (int64_t)value;
			int64_t num = whole * bestNum + prevNum;
			int64_t den = whole * bestDen + prevDen;
			if (den > 100000000ll || num > 2000000000ll) break;
			prevNum = bestNum; prevDen = bestDen;
			bestNum = num; bestDen = den;
			double got = (double)bestNum / (double)bestDen;
			double err = got > rate ? got - rate : rate - got;
			if (err < rate * 1e-9) break;
			double frac = value - (double)whole;
			if (frac < 1e-12) break;
			value = 1.0 / frac;
		}
		*numerator = (int)bestNum;
		*denominator = (int)bestDen;
	}

	void declaredRefresh(int *numerator, int *denominator)
	{
		*numerator = g_spec ? (g_pal ? g_spec->refreshPalNum : g_spec->refreshNtscNum) : 0;
		*denominator = g_spec ? (g_pal ? g_spec->refreshPalDen : g_spec->refreshNtscDen) : 0;
	}

	int vsyncNumerator(void) { int n, d; refreshAsRational(&n, &d); return n; }
	int vsyncDenominator(void) { int n, d; refreshAsRational(&n, &d); return d; }

	int memoryDomainCount(void) { return g_domainCount; }
	const char *memoryDomainName(int i) { return (i >= 0 && i < g_domainCount) ? g_domains[i].name : nullptr; }
	uint8_t *memoryDomainData(int i) { return (i >= 0 && i < g_domainCount) ? g_domains[i].data : nullptr; }
	int64_t memoryDomainSize(int i) { return (i >= 0 && i < g_domainCount) ? g_domains[i].size : 0; }
	int memoryDomainWritable(int i) { return (i >= 0 && i < g_domainCount) ? g_domains[i].writable : 0; }

	int busCount(void) { return (int)g_buses.size(); }

	const char *busName(int i)
	{
		if (i < 0 || i >= (int)g_busNames.size()) return nullptr;
		return g_busNames[i].c_str();
	}

	int64_t busSize(int i)
	{
		if (i < 0 || i >= (int)g_buses.size()) return 0;
		return (int64_t)g_buses[i]->size();
	}

	int busWritable(int i) { return (i >= 0 && i < (int)g_buses.size()) ? 1 : 0; }

	int busPeek(int bus, int address)
	{
		if (bus < 0 || bus >= (int)g_buses.size()) return 0;
		return g_buses[bus]->read((u32)address);
	}

	void busPoke(int bus, int address, int value)
	{
		if (bus < 0 || bus >= (int)g_buses.size()) return;
		g_buses[bus]->write((u32)address, (u8)value);
	}

	bool captureState(const uint8_t **data, int64_t *size)
	{
		static std::vector<uint8_t> blob;
		if (!g_root) return false;
		FenvGuard guard(machineFenv());
		auto s = g_root->serialize(true);
		if (!s) return false;
		blob.assign(s.data(), s.data() + s.size());
		if (data) *data = blob.data();
		if (size) *size = (int64_t)blob.size();
		return true;
	}

	/* Export Save Data hands back whichever save chip a Nintendo 64 cartridge
	 * has. The other machines save through mia, which this core does not let
	 * write to a host; their save data is inside the savestate instead. */
	const uint8_t *saveData(void)
	{
		if (!g_isN64) return nullptr;
		using namespace ares::Nintendo64;
		if (cartridge.ram.size) return (const uint8_t *)cartridge.ram.data;
		if (cartridge.eeprom.size) return (const uint8_t *)cartridge.eeprom.data;
		if (cartridge.flash.size) return (const uint8_t *)cartridge.flash.data;
		return nullptr;
	}

	int64_t saveDataSize(void)
	{
		if (!g_isN64) return 0;
		using namespace ares::Nintendo64;
		if (cartridge.ram.size) return (int64_t)cartridge.ram.size;
		if (cartridge.eeprom.size) return (int64_t)cartridge.eeprom.size;
		if (cartridge.flash.size) return (int64_t)cartridge.flash.size;
		return 0;
	}

	const char *saveDataName(void)
	{
		if (!g_isN64) return nullptr;
		using namespace ares::Nintendo64;
		if (cartridge.ram.size) return "save.ram";
		if (cartridge.eeprom.size) return "save.eeprom";
		if (cartridge.flash.size) return "save.flash";
		return nullptr;
	}

	bool padReport(int pad, int *x, int *y)
	{
		if (!g_isN64 || pad < 0 || pad > 3) return false;
		ares::Nintendo64::ControllerPort *ports[4] = {
			&ares::Nintendo64::controllerPort1, &ares::Nintendo64::controllerPort2,
			&ares::Nintendo64::controllerPort3, &ares::Nintendo64::controllerPort4,
		};
		auto *device = ports[pad]->device.get();
		if (device == nullptr) return false;
		auto data = device->read();
		if (x) *x = (int8_t)(uint8_t)(data >> 8 & 0xff);
		if (y) *y = (int8_t)(uint8_t)(data >> 0 & 0xff);
		return true;
	}
}
