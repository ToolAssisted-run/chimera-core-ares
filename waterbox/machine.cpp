/* machine.cpp - ares, driven as a machine rather than as an application.
 *
 * ares is written to be hosted: a Platform supplies its files, takes its
 * pictures and sound, and answers what time it is. That is exactly the shape a
 * waterbox core needs, so almost all of this file is that Platform, plus the
 * loading sequence ares' own desktop-ui performs.
 *
 * Two things are deliberately NOT ares': the picture and the display list. The
 * RDP here is angrylion's software rasteriser (see patches/ares/0004), because
 * ares' own renderer is paraLLEl-RDP on Vulkan and a movie cannot be replayed
 * on somebody else's graphics driver.
 */
#include "machine.h"

#include <n64/n64.hpp>
#include <mia/mia.hpp>

#include <angrylion.h>

#include <string.h>

using namespace ares;

namespace
{
	/* The N64's FPU rounding mode is machine state, and ares sets the HOST's
	 * rounding mode to match it while the CPU runs. Anything that runs between
	 * frames - the frontend, this file's own arithmetic - expects round-to-
	 * nearest, so the mode is swapped in on the way in and out again on the way
	 * out. Without this a savestate taken mid-run reloads under whatever mode
	 * the last game left behind, and floating point stops being reproducible.
	 */
	struct FenvGuard
	{
		nall::float_env saved;
		nall::float_env &machine;

		FenvGuard(nall::float_env &machine_) : machine(machine_)
		{
			if (machine.getRound() != saved.getRound()) machine.setRound(machine.getRound());
		}

		~FenvGuard()
		{
			if (machine.getRound() != saved.getRound()) saved.setRound(saved.getRound());
		}
	};

	constexpr int VideoWidth = 640;
	constexpr int VideoHeightMax = 576;
	constexpr int MaxSamplesPerFrame = 4096;

	char g_error[256] = "";

	void fail(const char *why)
	{
		snprintf(g_error, sizeof g_error, "%s", why);
	}

	/* ares hands audio out one stereo pair at a time, as doubles, whenever the
	 * machine's audio interface has produced some. A frame's worth is collected
	 * here and handed over whole. */
	int16_t g_audio[MaxSamplesPerFrame * 2];
	int g_audioSamples = 0;

	uint32_t g_video[VideoWidth * VideoHeightMax];
	bool g_rendering = true;

	bool g_inputWasRead = false;
	bool g_pal = false;

	std::shared_ptr<mia::Pak> g_systemPak;
	std::shared_ptr<mia::Pak> g_cartridgePak;
	Node::System g_root;

	struct ChimeraPlatform : ares::Platform
	{
		auto attach(Node::Object node) -> void override
		{
			/* ares resamples its audio to whatever the host asks for. 44100 is
			 * what the package declares. */
			if (auto stream = node->cast<Node::Audio::Stream>())
			{
				stream->setResamplerFrequency(44100);
			}
		}

		auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override
		{
			if (node->name() == "Nintendo 64") return g_systemPak ? g_systemPak->pak : nullptr;
			if (node->name() == "Nintendo 64 Cartridge") return g_cartridgePak ? g_cartridgePak->pak : nullptr;
			return {};
		}

		auto audio(Node::Audio::Stream stream) -> void override
		{
			while (stream->pending())
			{
				double frame[2];
				stream->read(frame);
				if (g_audioSamples >= MaxSamplesPerFrame) continue;
				auto clamp16 = [](double v) -> int16_t {
					double scaled = v * 32768.0;
					if (scaled > 32767.0) scaled = 32767.0;
					if (scaled < -32768.0) scaled = -32768.0;
					return (int16_t)scaled;
				};
				g_audio[g_audioSamples * 2 + 0] = clamp16(frame[0]);
				g_audio[g_audioSamples * 2 + 1] = clamp16(frame[1]);
				g_audioSamples++;
			}
		}

		auto input(Node::Input::Input node) -> void override
		{
			/* A frame in which the machine polled a controller is a frame the
			 * player's input reached. ares announces every read; one button is
			 * enough to notice, and Start is the one every device has. */
			if (auto button = node->cast<Node::Input::Button>())
			{
				if (button->name() == "Start" || button->name() == "Left Click") g_inputWasRead = true;
			}
		}

	};

	ChimeraPlatform *g_platform = nullptr;

	/* The four ports, in the order the frontend numbers them. */
	Nintendo64::ControllerPort *g_ports[4] = {
		&Nintendo64::controllerPort1, &Nintendo64::controllerPort2,
		&Nintendo64::controllerPort3, &Nintendo64::controllerPort4,
	};

	const char *portDeviceName(machine::Port port)
	{
		switch (port)
		{
			case machine::Port::Gamepad:
			case machine::Port::GamepadWithControllerPak:
			case machine::Port::GamepadWithRumblePak: return "Gamepad";
			case machine::Port::Mouse: return "Mouse";
			default: return nullptr;
		}
	}

	const char *portPakName(machine::Port port)
	{
		switch (port)
		{
			case machine::Port::GamepadWithControllerPak: return "Controller Pak";
			case machine::Port::GamepadWithRumblePak: return "Rumble Pak";
			default: return nullptr;
		}
	}
}

namespace { void publishDomains(void); }

namespace machine
{
	const char *error(void) { return g_error; }

	bool init(const Config &config)
	{
		g_error[0] = 0;
		Nintendo64::rtcEpoch = config.initTimeUnix;
		g_pal = config.pal;

		g_platform = new ChimeraPlatform;
		ares::platform = g_platform;

		/* mia is ares' own media layer: it reads the cartridge, works out its
		 * region, its CIC and what kind of save chip it has, and hands back the
		 * pak the machine expects. Using it rather than a table of our own is
		 * what makes the OTHER ares systems a build-list change later. */
		g_systemPak = mia::System::create("Nintendo 64");
		if (!g_systemPak) return fail("ares has no Nintendo 64 system"), false;
		if (g_systemPak->load({}) != successful) return fail("the Nintendo 64 system pak would not load"), false;

		if (!config.romFile || !*config.romFile) return fail("no cartridge given"), false;
		g_cartridgePak = mia::Medium::create("Nintendo 64");
		if (!g_cartridgePak) return fail("ares has no Nintendo 64 medium"), false;
		if (g_cartridgePak->load(config.romFile) != successful)
			return fail("the cartridge would not load"), false;

		/* The picture is the rasteriser's, and it is written straight into the
		 * buffer the frontend reads. */
		angrylion::OutFrameBuffer = g_video;
		angrylion::OutHeight = config.pal ? 576 : 480;
		Nintendo64::FastVI = config.fastVI;
		Nintendo64::BobDeinterlace = config.bobDeinterlace;

		/* Real hardware powers on with genuinely random RDRAM timings, and ares
		 * models that by seeding its RNG from the host clock. A movie cannot be
		 * replayed against a machine that starts differently every time, so the
		 * seed is pinned - which is what this option is for. Without it the same
		 * run produces a different machine on every boot, and only the picture
		 * happens to agree. */
		Nintendo64::option("Deterministic Entropy", "true");

		string name = config.pal ? "[Nintendo] Nintendo 64 (PAL)" : "[Nintendo] Nintendo 64 (NTSC)";
		if (!Nintendo64::load(g_root, name)) return fail("ares would not build the machine"), false;

		if (auto port = g_root->find<Node::Port>("Cartridge Slot"))
		{
			port->allocate();
			port->connect();
		}
		else return fail("the machine has no cartridge slot"), false;

		for (int i = 0; i < 4; i++)
		{
			auto port = g_root->find<Node::Port>({"Controller Port ", 1 + i});
			if (!port) return fail("the machine is missing a controller port"), false;
			const char *device = portDeviceName(config.port[i]);
			if (!device) continue;

			auto peripheral = port->allocate(device);
			port->connect();

			if (const char *pak = portPakName(config.port[i]))
			{
				if (auto slot = peripheral->find<Node::Port>("Pak"))
				{
					slot->allocate(pak);
					slot->connect();
				}
				else return fail("the pad has no pak slot"), false;
			}
		}

		FenvGuard guard(Nintendo64::cpu.fenv);
		g_root->power();
		publishDomains();
		return true;
	}

	void frame(const Input &input)
	{
		FenvGuard guard(Nintendo64::cpu.fenv);

		angrylion::OutFrameBuffer = g_rendering ? g_video : nullptr;

		if (input.power) g_root->power(false);
		else if (input.reset) g_root->power(true);

		for (int i = 0; i < 4; i++)
		{
			const Pad &p = input.pad[i];
			Nintendo64::ControllerPort *slot = g_ports[i];
			if (auto pad = dynamic_cast<Nintendo64::Gamepad *>(slot->device.get()))
			{
				/* The frontend speaks the console's units - the -127..127 byte a
				 * Nintendo 64 controller actually reports - because that is what a
				 * movie records and what every N64 TAS is written in. ares' pads
				 * take the ±32767 of a modern analogue stick and put it through a
				 * deadzone and an octagonal gate on the way in, so the value is
				 * scaled up to that range here. Passing the raw byte instead lands
				 * inside ares' deadzone and the stick does nothing at all, which is
				 * how this was found.
				 *
				 * What the game finally reads is therefore SHAPED, not the byte the
				 * author typed; see docs/PLAN.md, "the analogue stick". */
				pad->x->setValue(p.x * 32767 / 127);
				pad->y->setValue(p.y * 32767 / 127);
				pad->up->setValue(p.up);
				pad->down->setValue(p.down);
				pad->left->setValue(p.left);
				pad->right->setValue(p.right);
				pad->b->setValue(p.b);
				pad->a->setValue(p.a);
				pad->cameraUp->setValue(p.cUp);
				pad->cameraDown->setValue(p.cDown);
				pad->cameraLeft->setValue(p.cLeft);
				pad->cameraRight->setValue(p.cRight);
				pad->l->setValue(p.l);
				pad->r->setValue(p.r);
				pad->z->setValue(p.z);
				pad->start->setValue(p.start);
			}
			else if (auto mouse = dynamic_cast<Nintendo64::Mouse *>(slot->device.get()))
			{
				mouse->x->setValue(p.x);
				mouse->y->setValue(p.y);
				mouse->left->setValue(p.a);
				mouse->right->setValue(p.b);
			}
		}

		g_inputWasRead = false;
		g_audioSamples = 0;

		g_root->run();
	}

	const uint32_t *video(void) { return g_video; }
	int videoWidth(void) { return VideoWidth; }
	int videoHeight(void) { return (int)angrylion::OutHeight; }
	void setRenderingEnabled(bool on) { g_rendering = on; }

	const int16_t *audio(void) { return g_audio; }
	int audioSamples(void) { return g_audioSamples; }
	bool inputWasRead(void) { return g_inputWasRead; }

	int vsyncNumerator(void) { return g_pal ? 50 : 60; }
	int vsyncDenominator(void) { return 1; }
}

/* ---- what the machine is made of ----
 *
 * Published once, as a pointer and a size, so a RAM search costs nothing. The
 * N64's memory IS blocks, so there is no bus to resolve per access.
 *
 * The cartridge's three save chips are mutually exclusive - a cart has one, or
 * none - so which of them exists depends on the game, and the list is built
 * after the cartridge is in.
 */
namespace
{
	struct Domain
	{
		const char *name;
		uint8_t *data;
		int64_t size;
		int writable;
	};

	Domain g_domains[8];
	int g_domainCount = 0;

	void publish(const char *name, void *data, int64_t size, int writable)
	{
		if (!data || size <= 0) return;
		if (g_domainCount >= (int)(sizeof g_domains / sizeof *g_domains)) return;
		g_domains[g_domainCount++] = {name, (uint8_t *)data, size, writable};
	}

	void publishDomains(void)
	{
		using namespace ares::Nintendo64;
		g_domainCount = 0;
		publish("RDRAM", rdram.ram.data, (int64_t)rdram.ram.size, 1);
		publish("RSP DMEM", rsp.dmem.data, (int64_t)rsp.dmem.size, 1);
		publish("RSP IMEM", rsp.imem.data, (int64_t)rsp.imem.size, 1);
		publish("PIF RAM", pif.ram.data, (int64_t)pif.ram.size, 1);
		publish("Cartridge ROM", cartridge.rom.data, (int64_t)cartridge.rom.size, 0);
		publish("SRAM", cartridge.ram.data, (int64_t)cartridge.ram.size, 1);
		publish("EEPROM", cartridge.eeprom.data, (int64_t)cartridge.eeprom.size, 1);
		publish("Flash", cartridge.flash.data, (int64_t)cartridge.flash.size, 1);
	}
}

namespace machine
{
	int memoryDomainCount(void) { return g_domainCount; }

	const char *memoryDomainName(int i)
	{
		return (i >= 0 && i < g_domainCount) ? g_domains[i].name : nullptr;
	}

	uint8_t *memoryDomainData(int i)
	{
		return (i >= 0 && i < g_domainCount) ? g_domains[i].data : nullptr;
	}

	int64_t memoryDomainSize(int i)
	{
		return (i >= 0 && i < g_domainCount) ? g_domains[i].size : 0;
	}

	int memoryDomainWritable(int i)
	{
		return (i >= 0 && i < g_domainCount) ? g_domains[i].writable : 0;
	}

	/* Export Save Data hands back whichever save chip this cartridge has. A cart
	 * with no battery keeps nothing, and says so by having none. */
	const uint8_t *saveData(void)
	{
		using namespace ares::Nintendo64;
		if (cartridge.ram.size) return (const uint8_t *)cartridge.ram.data;
		if (cartridge.eeprom.size) return (const uint8_t *)cartridge.eeprom.data;
		if (cartridge.flash.size) return (const uint8_t *)cartridge.flash.data;
		return nullptr;
	}

	int64_t saveDataSize(void)
	{
		using namespace ares::Nintendo64;
		if (cartridge.ram.size) return (int64_t)cartridge.ram.size;
		if (cartridge.eeprom.size) return (int64_t)cartridge.eeprom.size;
		if (cartridge.flash.size) return (int64_t)cartridge.flash.size;
		return 0;
	}

	const char *saveDataName(void)
	{
		using namespace ares::Nintendo64;
		if (cartridge.ram.size) return "save.ram";
		if (cartridge.eeprom.size) return "save.eeprom";
		if (cartridge.flash.size) return "save.flash";
		return nullptr;
	}
}
