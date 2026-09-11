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
#include <pce/pce.hpp>
#include <sfc/sfc.hpp>
#include <ps1/ps1.hpp>
#include <sg/sg.hpp>
#include <spec/spec.hpp>
#include <ws/ws.hpp>

#include <vector>

namespace machines
{
	using LoadFn = bool (*)(ares::Node::System &, nall::string);
	/* ares::<Machine>::option, which several machines must be given before they
	 * are built rather than after. */
	using OptionFn = bool (*)(nall::string, nall::string);

	/* One of those, as the table spells it. A null name ends the list. */
	struct Option { const char *name; const char *value; };

	/* A slot on the CARTRIDGE rather than on the console.
	 *
	 * A Super Famicom cartridge may itself have a slot: a BS-X cartridge takes
	 * a Satellaview memory pack, a Sufami Turbo cartridge takes one or two
	 * Sufami Turbo minicarts, a Super Game Boy takes a Game Boy cartridge. The
	 * base cartridge is an ordinary medium in the console's own slot; what goes
	 * INTO it is a second medium of a different kind, read by a different mia
	 * medium, answered at a different node.
	 *
	 * Which one applies is decided by the second file's EXTENSION, because a
	 * machine may offer several and the file is what says which. A null
	 * extensions field ends the list. */
	struct SubSlot
	{
		const char *extensions;  /* space separated, no dots */
		const char *miaMedium;   /* mia's name for what goes in the slot */
		const char *port;        /* the port inside the cartridge, by ares' path */
		const char *node;        /* the node that asks for the pak, by name */
	};

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

		/* Settings that have to be applied BEFORE ares builds the machine.
		 *
		 * Two kinds, and both are load-bearing. A machine with two
		 * implementations of a chip picks between them here - the PC Engine's
		 * video and the Super Famicom's are pointers that are null until an
		 * option names one, and System::load calls straight through them - so a
		 * machine can crash for want of a setting rather than fail to build. And
		 * a machine that models its power-on state from the host's entropy is
		 * not replayable until that is pinned, which is a movie's whole
		 * contract.
		 *
		 * Applied by whoever builds a machine: machine.cpp for a run, and
		 * gen-machines, which builds every one of them to ask what it is made
		 * of and would otherwise hit exactly the same crash. */
		OptionFn option;
		const Option *options;

		/* A machine that needs MORE than one BIOS file lists the rest here,
		 * null-terminated, after the one `firmware` names.
		 *
		 * The 32X wants a vector table and a boot ROM for each of its two
		 * SH-2s; an MSX2 wants a main ROM and a sub ROM. mia takes them all at
		 * once through its own loadMultiple, which upstream already uses for
		 * exactly this, so the only thing this core adds is saying which files
		 * and in what order. Every one of them is declared in the package, so
		 * the frontend asks for all of them and mounts each under its id.
		 *
		 * LAST in this struct on purpose: every entry below is positional, and
		 * a field added in the middle would quietly re-point the ones after it. */
		const char *const *extraFirmware;

		/* Which node in ares' tree the medium belongs to, by name.
		 *
		 * Most machines have one slot and it is obvious. A Mega CD has two - a
		 * disc tray and a cartridge slot for the RAM cart - and handing the disc
		 * to whichever asks first puts a CD image through the cartridge reader.
		 * ares' own front end routes by node name for exactly these machines,
		 * and this is the same answer: the named node gets the medium and any
		 * other gets nothing, which ares reads as an empty slot.
		 *
		 * Null for a machine with one slot, where anything that asks and is not
		 * the system itself is asking for the medium. */
		const char *mediumNode;

		/* A machine whose BIOS is itself a MEDIUM rather than a file the system
		 * pak swallows.
		 *
		 * A PC Engine CD is a PC Engine with a System Card plugged into the
		 * cartridge slot and a disc in the tray. The card is an ordinary HuCard
		 * - mia reads it as a "PC Engine" medium, not as part of the system pak
		 * - so the machine wants THREE paks where every other machine here
		 * wants two: the system, the card in the cartridge slot, and the disc
		 * in the tray.
		 *
		 * `firmwareMedium` is mia's name for the medium the card is read as,
		 * and `firmwareNode` the node in ares' tree that receives it. When
		 * these are set the system pak is loaded with NO file, because the BIOS
		 * is not the system's to hold.
		 *
		 * Both null for every machine whose BIOS is a file. */
		const char *firmwareMedium;
		const char *firmwareNode;

		/* What this machine's CARTRIDGE can take, null-terminated; null for a
		 * machine whose cartridges take nothing. See SubSlot. */
		const SubSlot *subSlots;
	};

	/* Ordered as a person would look for them, not as ares stores them. */
	/* Accuracy over speed, and a machine that starts the same way every time:
	 * the two things a movie needs of a Super Famicom. Its video is the same
	 * two-implementation arrangement the PC Engine's is. */
	inline constexpr Option kSuperFamicomOptions[] = {
		{"Pixel Accuracy", "true"},
		{"Deterministic Entropy", "true"},
		{nullptr, nullptr},
	};

	/* An MSX2's main ROM and the sub ROM it calls into. */
	inline constexpr const char *kMSX2Firmware[] = { "msx2Sub", nullptr };

	/* What a Super Famicom cartridge can have a slot for. The base cartridge
	 * decides whether the slot is there at all - a BS-X cartridge has the
	 * Satellaview one, a Sufami Turbo cartridge has two minicart ones, a Super
	 * Game Boy has the Game Boy one - and the second file's extension decides
	 * which of them it is going into. */
	inline constexpr SubSlot kSuperFamicomSubSlots[] = {
		{"bs", "BS Memory", "BS Memory Slot", "BS Memory Cartridge"},
		{"st", "Sufami Turbo", "Sufami Turbo Slot A", "Sufami Turbo Cartridge"},
		{"gb gbc", "Game Boy Color", "Super Game Boy/Cartridge Slot",
		 "Game Boy Color Cartridge"},
		{nullptr, nullptr, nullptr, nullptr},
	};

	/* The 32X's three boot ROMs, in the order its system pak reads them. */
	inline constexpr const char *kMega32XFirmware[] = { "m32xBootM", "m32xBootS", nullptr };

	/* A Mega CD 32X is both machines at once, so it wants both sets of boot
	 * code: the disc drive's BIOS (which `firmware` names) and then the three
	 * the 32X starts from. */
	inline constexpr const char *kMegaCD32XFirmware[] = {
		"m32xVector", "m32xBootM", "m32xBootS", nullptr,
	};

	/* The PC Engine's video implementation, which is a null pointer until an
	 * option names one. ares forces the accurate renderer itself here - its own
	 * comment says the scanline one is too buggy - so the value is not read. */
	inline constexpr Option kPCEngineOptions[] = {
		{"Pixel Accuracy", "true"},
		{nullptr, nullptr},
	};

	/* Real hardware powers on with genuinely random RDRAM timings, and ares
	 * models that from the host clock. A movie cannot be replayed against a
	 * machine that starts differently every time. */
	inline constexpr Option kNintendo64Options[] = {
		{"Deterministic Entropy", "true"},
		{nullptr, nullptr},
	};

	inline auto all() -> const std::vector<Spec> &
	{
		static const std::vector<Spec> specs = {
			{"N64", "Nintendo 64", "Nintendo 64", "Nintendo 64", ares::Nintendo64::load,
			 "[Nintendo] Nintendo 64 (NTSC)", "[Nintendo] Nintendo 64 (PAL)",
			 640, 576, 640, 480, "n64 v64 z64", "Gamepad Mouse", nullptr, false,
			 0, 0, 0, 0, ares::Nintendo64::option, kNintendo64Options},

			{"NES", "Famicom / NES", "Famicom", "Famicom", ares::Famicom::load,
			 "[Nintendo] Famicom (NTSC-J)", "[Nintendo] Famicom (PAL)",
			 512, 480, 293, 240, "fc nes unf unif", nullptr, nullptr},

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

			{"MYV", "MyVision", "MyVision", "MyVision", ares::MyVision::load,
			 "[Nichibutsu] MyVision", nullptr,
			 284, 243, 284, 192, "myvision", nullptr, nullptr},

			/* ---- and the ones ares cannot build without a console BIOS ----
			 *
			 * The user supplies these; Chimera resolves them, remembers where
			 * they were, and mounts each under the id named here. Nothing of the
			 * kind ships in this package, and the machines below simply do not
			 * appear in a project until their firmware has been found. */

			{"GB", "Game Boy", "Game Boy", "Game Boy", ares::GameBoy::load,
			 "[Nintendo] Game Boy", nullptr,
			 160, 144, 160, 144, "gb", nullptr, "gbBoot"},

			{"GBC", "Game Boy Color", "Game Boy Color", "Game Boy Color", ares::GameBoy::load,
			 "[Nintendo] Game Boy Color", nullptr,
			 160, 144, 160, 144, "gbc", nullptr, "gbcBoot"},

			{"WS", "WonderSwan", "WonderSwan", "WonderSwan", ares::WonderSwan::load,
			 "[Bandai] WonderSwan", nullptr,
			 224, 224, 224, 144, "ws", nullptr, "wsBoot", false,
			 4000, 53, 0, 0},

			{"WSC", "WonderSwan Color", "WonderSwan Color", "WonderSwan Color", ares::WonderSwan::load,
			 "[Bandai] WonderSwan Color", nullptr,
			 224, 224, 224, 144, "wsc", nullptr, "wscBoot", false,
			 4000, 53, 0, 0},

			{"ZXS", "ZX Spectrum", "ZX Spectrum", "ZX Spectrum", ares::ZXSpectrum::load,
			 "[Sinclair] ZX Spectrum", nullptr,
			 352, 296, 352, 296, "z80 tap tzx", nullptr, "zxsBios"},

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
			 284, 243, 284, 192, "msx rom cas wav tzx tsx", nullptr, "msxBios", true},

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

			{"SFC", "Super Famicom / SNES", "Super Famicom", "Super Famicom",
			 ares::SuperFamicom::load,
			 "[Nintendo] Super Famicom (NTSC)", "[Nintendo] Super Famicom (PAL)",
			 564, 576, 282, 242, "sfc smc swc fig", nullptr, "sfcIpl", false,
			 0, 0, 0, 0, ares::SuperFamicom::option, kSuperFamicomOptions,
			 nullptr, nullptr, nullptr, nullptr, kSuperFamicomSubSlots},

			/* A 32X is a Mega Drive with two SH-2s bolted on, so it is ares'
			 * Mega Drive under another configuration, reading its own medium
			 * and needing the three boot ROMs those processors start from. */
			{"32X", "Mega Drive 32X", "Mega 32X", "Mega 32X", ares::MegaDrive::load,
			 "[Sega] Mega 32X (NTSC-U)", "[Sega] Mega 32X (PAL)",
			 1280, 480, 292, 224, "32x", nullptr, "m32xVector", false,
			 0, 0, 0, 0, nullptr, nullptr, kMega32XFirmware},

			/* Twice the MSX's buffer in both directions: an MSX2 has modes the
			 * MSX has not, and ares gives the screen the largest of them. */
			{"MSX2", "MSX2", "MSX2", "MSX2", ares::MSX::load,
			 "[Microsoft] MSX2 (NTSC)", "[Microsoft] MSX2 (PAL)",
			 568, 486, 284, 192, "msx2 mx2 rom cas wav tzx tsx", nullptr, "msx2Main", false,
			 0, 0, 0, 0, nullptr, nullptr, kMSX2Firmware},

			/* A Mega CD is a Mega Drive with a disc drive beside it, so it is
			 * ares' Mega Drive under another configuration - and the one place
			 * this core has to say which slot the medium goes in, because the
			 * machine also has a cartridge slot for its RAM cart. */
			{"MCD", "Mega CD / Sega CD", "Mega CD", "Mega CD", ares::MegaDrive::load,
			 "[Sega] Mega CD (NTSC-U)", "[Sega] Mega CD (PAL)",
			 1280, 480, 292, 224, "cue chd", nullptr, "megaCdBios", false,
			 0, 0, 0, 0, nullptr, nullptr, nullptr, "Mega CD Disc"},

			{"PCE", "PC Engine / TurboGrafx-16", "PC Engine", "PC Engine",
			 ares::PCEngine::load, "[NEC] TurboGrafx 16 (NTSC-U)", nullptr,
			 1176, 263, 258, 218, "pce", nullptr, nullptr, false,
			 0, 0, 0, 0, ares::PCEngine::option, kPCEngineOptions},

			{"SGX", "SuperGrafx", "SuperGrafx", "SuperGrafx",
			 ares::PCEngine::load, "[NEC] SuperGrafx (NTSC-J)", nullptr,
			 1176, 263, 258, 218, "sgx", nullptr, nullptr, false,
			 0, 0, 0, 0, ares::PCEngine::option, kPCEngineOptions},

			/* A TurboDuo is a PC Engine with a System Card in the cartridge
			 * slot and a disc in the tray - so the BIOS is a MEDIUM here, read
			 * as an ordinary HuCard, and the machine wants three paks. The
			 * card decides what a disc can do, and different discs want
			 * different cards, so the one to mount is the user's choice rather
			 * than ours; the package asks for it by this id. */
			/* Both add-ons at once: a disc drive and two SH-2s. ares builds
			 * it from its Mega Drive, the medium is the disc, and the boot code
			 * is the Mega CD's BIOS followed by the 32X's three. */
			{"MCD32X", "Mega CD 32X / Sega CD 32X", "Mega CD 32X", "Mega CD",
			 ares::MegaDrive::load,
			 "[Sega] Mega CD 32X (NTSC-U)", "[Sega] Mega CD 32X (PAL)",
			 1280, 480, 292, 224, "cue chd", nullptr, "megaCdBios", false,
			 0, 0, 0, 0, nullptr, nullptr, kMegaCD32XFirmware, "Mega CD Disc"},

			{"PCECD", "PC Engine CD / TurboDuo", "PC Engine", "PC Engine CD",
			 ares::PCEngine::load, "[NEC] TurboDuo (NTSC-U)", nullptr,
			 1176, 263, 258, 218, "cue chd", nullptr, "pceSystemCard", false,
			 0, 0, 0, 0, ares::PCEngine::option, kPCEngineOptions, nullptr,
			 "PC Engine CD Disc", "PC Engine", "PC Engine Card"},
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
