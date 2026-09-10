/* machines.h - which ares machines this core offers, and how to build each one.
 *
 * ares is one emulator for about thirty machines. This table is the whole of
 * what makes a machine available: a name for ares, a name for mia, and the
 * function that builds it. Everything else about a machine - its ports, the
 * devices they take, the buttons on those devices - is asked of ares itself by
 * gen-machines, so it cannot drift from what the emulator actually does.
 *
 * A machine is listed here when it needs nothing of the user but a cartridge.
 * The ones ares can only build with a console BIOS (the Atari 5200,
 * ColecoVision, MSX, Neo Geo, Game Boy Advance, PlayStation) are absent until
 * this core declares that firmware; see docs/PLAN.md.
 */
#ifndef CHIMERA_ARES_MACHINES_H
#define CHIMERA_ARES_MACHINES_H

#include "machine-inputs.h"

#include <ares/ares.hpp>

/* termios.h defines NCCS, and the PlayStation's GTE has an instruction of that
 * name. Only a build that pulls in both nall's serial support and the PS1 ever
 * sees the collision, which is why upstream does not. */
#undef NCCS

#include <a26/a26.hpp>
#include <a52/a52.hpp>
#include <cv/cv.hpp>
#include <fc/fc.hpp>
#include <gba/gba.hpp>
#include <gb/gb.hpp>
#include <md/md.hpp>
#include <ms/ms.hpp>
#include <msx/msx.hpp>
#include <myvision/myvision.hpp>
#include <n64/n64.hpp>
#include <ng/ng.hpp>
#include <ngp/ngp.hpp>
#include <ps1/ps1.hpp>
#include <sg/sg.hpp>
#include <spec/spec.hpp>
#include <ws/ws.hpp>

#include <vector>

namespace machines
{
	using LoadFn = bool (*)(ares::Node::System &, nall::string);

	struct Spec
	{
		const char *id;          /* what a project records, and what waterbox.config calls it */
		const char *label;       /* what a person reads */
		const char *miaSystem;   /* mia's name for the console, which supplies its boot ROMs */
		const char *miaMedium;   /* mia's name for the medium, which reads the cartridge */
		LoadFn load;
		const char *configNtsc;  /* ares' own name for the machine, per region */
		const char *configPal;   /* null when the machine has only one form */
		/* The picture at its largest. The buffer the package declares has to
		 * hold every machine's, so these are what that number is made of. */
		int maxWidth, maxHeight;
		/* The aspect the picture is meant to be seen at. */
		int virtualWidth, virtualHeight;
		const char *extensions;  /* space separated, no dots */
		/* Which of the port's devices this core offers, by ares' own name, space
		 * separated. Null means "the ordinary controller and nothing else",
		 * which is what almost every machine wants: ares supports thirty-three
		 * devices on an Atari 2600 port, and declaring the union of them would
		 * make a controller nobody could read. */
		const char *devices;
		/* The console BIOS ares cannot build this machine without, by the id the
		 * package declares it under - which is also the name the frontend mounts
		 * it as. Null for a machine that carries everything it needs. */
		const char *firmware;
		/* True for a machine that starts with nothing in its drive - a
		 * PlayStation reaches its BIOS shell with no disc, which is a real
		 * machine to compare and one that needs no content at all. A cartridge
		 * console does not, and asking it to is a mistake worth refusing. */
		bool bootsWithoutMedium;
		/* What this machine refreshes at when ares will not say, as a rational.
		 * Almost every machine tells ares its rate out of its own clock while it
		 * is being built, and that is what the core reports. A few work the
		 * number out from the frame they have just drawn - because the number of
		 * lines in a frame is something the PROGRAM chooses - and say nothing at
		 * all until a frame has gone by. The frontend asks once, at load, and
		 * writes the answer into the movie, so "after the first frame" is too
		 * late.
		 *
		 * Running the machine to hear the answer and putting it back was tried
		 * and abandoned: restoring even a perfect savestate over a machine that
		 * has only just powered on does not give the machine back, because the
		 * serialiser synchronises the threads and power-on has not. A declared
		 * number changes no machine at all, and the gate checks it against what
		 * the machine actually reports once it is running.
		 *
		 * 0/0 means "ares will say" - which is all but three of them. The
		 * numbers are ares' own constants divided out: an Atari 2600 is
		 * 3579575/(228*262), a WonderSwan 3072000/(256*159). */
		int refreshNtscNum, refreshNtscDen;
		int refreshPalNum, refreshPalDen;
	};

	/* Ordered as a person would look for them, not as ares stores them. */
	inline auto all() -> const std::vector<Spec> &
	{
		static const std::vector<Spec> specs = {
			{"N64", "Nintendo 64", "Nintendo 64", "Nintendo 64", ares::Nintendo64::load,
			 "[Nintendo] Nintendo 64 (NTSC)", "[Nintendo] Nintendo 64 (PAL)",
			 640, 576, 640, 480, "n64 v64 z64", "Gamepad Mouse", nullptr},

			{"NES", "Famicom / NES", "Famicom", "Famicom", ares::Famicom::load,
			 "[Nintendo] Famicom (NTSC-J)", "[Nintendo] Famicom (PAL)",
			 512, 480, 293, 240, "fc nes unf unif", nullptr, nullptr},

			{"GB", "Game Boy", "Game Boy", "Game Boy", ares::GameBoy::load,
			 "[Nintendo] Game Boy", nullptr,
			 160, 144, 160, 144, "gb", nullptr, nullptr},

			{"GBC", "Game Boy Color", "Game Boy Color", "Game Boy Color", ares::GameBoy::load,
			 "[Nintendo] Game Boy Color", nullptr,
			 160, 144, 160, 144, "gbc", nullptr, nullptr},

			{"GEN", "Mega Drive / Genesis", "Mega Drive", "Mega Drive", ares::MegaDrive::load,
			 "[Sega] Mega Drive (NTSC-U)", "[Sega] Mega Drive (PAL)",
			 1280, 480, 292, 224, "md gen smd bin", nullptr, nullptr},

			{"SMS", "Master System", "Master System", "Master System", ares::MasterSystem::load,
			 "[Sega] Master System (NTSC-U)", "[Sega] Master System (PAL)",
			 284, 243, 284, 192, "sms", nullptr, nullptr},

			{"GG", "Game Gear", "Game Gear", "Game Gear", ares::MasterSystem::load,
			 "[Sega] Game Gear (NTSC-U)", nullptr,
			 160, 144, 160, 144, "gg", nullptr, nullptr},

			{"SG", "SG-1000", "SG-1000", "SG-1000", ares::SG1000::load,
			 "[Sega] SG-1000 (NTSC)", "[Sega] SG-1000 (PAL)",
			 284, 243, 284, 192, "sg sg1000", nullptr, nullptr},

			/* The Atari 2600 and the WonderSwan count the lines their program
			 * actually drew, so they say nothing until a frame has gone by and
			 * the frontend has already asked. Declared, and checked by the gate
			 * against what they report once running. */
			{"A26", "Atari 2600", "Atari 2600", "Atari 2600", ares::Atari2600::load,
			 "[Atari] Atari 2600 (NTSC)", "[Atari] Atari 2600 (PAL)",
			 160, 312, 292, 222, "a26 bin", nullptr, nullptr, false,
			 27325, 456, 3546894, 71136},

			{"WS", "WonderSwan", "WonderSwan", "WonderSwan", ares::WonderSwan::load,
			 "[Bandai] WonderSwan", nullptr,
			 224, 224, 224, 144, "ws", nullptr, nullptr, false,
			 4000, 53, 0, 0},

			{"WSC", "WonderSwan Color", "WonderSwan Color", "WonderSwan Color", ares::WonderSwan::load,
			 "[Bandai] WonderSwan Color", nullptr,
			 224, 224, 224, 144, "wsc", nullptr, nullptr, false,
			 4000, 53, 0, 0},

			{"ZXS", "ZX Spectrum", "ZX Spectrum", "ZX Spectrum", ares::ZXSpectrum::load,
			 "[Sinclair] ZX Spectrum", nullptr,
			 352, 296, 352, 296, "z80 tap tzx", nullptr, nullptr},

			{"MYV", "MyVision", "MyVision", "MyVision", ares::MyVision::load,
			 "[Nichibutsu] MyVision", nullptr,
			 284, 243, 284, 192, "myvision", nullptr, nullptr},

			/* ---- and the ones ares cannot build without a console BIOS ----
			 *
			 * The user supplies these; Chimera resolves them, remembers where
			 * they were, and mounts each under the id named here. Nothing of the
			 * kind ships in this package, and the machines below simply do not
			 * appear in a project until their firmware has been found. */

			{"GBA", "Game Boy Advance", "Game Boy Advance", "Game Boy Advance", ares::GameBoyAdvance::load,
			 "[Nintendo] Game Boy Advance", nullptr,
			 240, 160, 240, 160, "gba", nullptr, "gbaBios"},

			{"CV", "ColecoVision", "ColecoVision", "ColecoVision", ares::ColecoVision::load,
			 "[Coleco] ColecoVision (NTSC)", "[Coleco] ColecoVision (PAL)",
			 284, 243, 284, 192, "cv col", nullptr, "cvBios"},

			/* An MSX with nothing in its slot boots its own BASIC, which is a
			 * whole machine to compare and needs no cartridge - the same reason
			 * the PlayStation is marked this way. It is the only content this
			 * machine has: nobody has a freely redistributable MSX cartridge
			 * here. */
			{"MSX", "MSX", "MSX", "MSX", ares::MSX::load,
			 "[Microsoft] MSX (NTSC)", "[Microsoft] MSX (PAL)",
			 284, 243, 284, 192, "msx rom", nullptr, "msxBios", true},

			/* The one machine here that loads a disc rather than a cartridge.
			 * mia takes a .cue (with its .bin beside it, mounted under the name
			 * the cue gives) or a bare PlayStation executable, which is how most
			 * homebrew arrives and which needs no disc at all. */
			{"PS1", "PlayStation", "PlayStation", "PlayStation", ares::PlayStation::load,
			 "[Sony] PlayStation (NTSC-U)", "[Sony] PlayStation (PAL)",
			 640, 512, 640, 480, "cue exe ps-exe", nullptr, "ps1Bios", true},

			{"A52", "Atari 5200", "Atari 5200", "Atari 5200", ares::Atari5200::load,
			 "[Atari] Atari 5200 (NTSC)", nullptr,
			 384, 240, 384, 240, "a52 bin", nullptr, "a52Bios", false},

			{"NGP", "Neo Geo Pocket", "Neo Geo Pocket", "Neo Geo Pocket", ares::NeoGeoPocket::load,
			 "[SNK] Neo Geo Pocket", nullptr,
			 160, 152, 160, 152, "ngp", nullptr, "ngpBios", false},

			{"NGPC", "Neo Geo Pocket Color", "Neo Geo Pocket Color", "Neo Geo Pocket Color",
			 ares::NeoGeoPocket::load, "[SNK] Neo Geo Pocket Color", nullptr,
			 160, 152, 160, 152, "ngc ngpc", nullptr, "ngpcBios", false},

			{"NG", "Neo Geo AES", "Neo Geo AES", "Neo Geo", ares::NeoGeo::load,
			 "[SNK] Neo Geo AES", nullptr,
			 320, 256, 320, 224, "zip", nullptr, "ngBios", false},
		};
		return specs;
	}

	inline auto find(const char *id) -> const Spec *
	{
		if (id == nullptr) return nullptr;
		for (auto &spec : all()) if (nall::string{spec.id} == id) return &spec;
		return nullptr;
	}
}

#endif
