/* machine.h - the emulated machine, and nothing about how it is driven.
 *
 * One layer, two users: waterbox.cpp (the core the frontend loads) and
 * run-native.cpp (the reference the gate compares against). They share this so
 * that "native and sandbox agree" is a claim about the SANDBOX, not about two
 * hand-written adapters that happen to look alike.
 *
 * The machine is ares - whichever of its consoles this session is. Everything
 * here is bytes in and bytes out: no paths, no host devices, no clock. A file
 * is a name the host has mounted; time is a number the caller supplies.
 */
#ifndef CHIMERA_ARES_MACHINE_H
#define CHIMERA_ARES_MACHINE_H

#include <stdint.h>

namespace machine
{
	struct Config
	{
		/* Which console this is, by the id waterbox/machines.h gives it -
		 * "N64", "GB", "GEN". A project pins it and a movie cites it. */
		const char *machine;

		/* Mounted file names, not host paths. Null means absent. */
		const char *romFile;
		/* The console BIOS, for a machine that needs one. Mounted under the id
		 * waterbox/machines.h names, which is also what the package declares. */
		const char *firmwareFile;

		bool pal;
		uint64_t initTimeUnix;   /* what a cartridge clock starts at */

		/* A Nintendo 64 PIF boot ROM, by host path, for the REFERENCE RUNNER
		 * ONLY: the core boots that machine from PIF::bootHLE, and this is how
		 * run-gate.sh gets a real boot to compare that against on a machine
		 * where somebody has the ROM. A core is never handed one - it is not
		 * ours to carry - so this is null there and the HLE boot is the only
		 * boot the shipped machine has. */
		const char *pifRomFile;

		/* Print everything the cartridge's boot code can see the moment it is
		 * handed control, and stop. A Nintendo 64 diagnostic; see BootProbe. */
		bool bootProbe;

		/* How the machine asks for one of ares' own settings by the key the
		 * package declares it under - "gb.fastBoot", "ng.settingsMode". The
		 * guest reads the mounted settings JSON; the reference runner reads its
		 * own --set flags. Null means nobody can be asked, and every setting
		 * keeps the value ares powers on with.
		 *
		 * A string for every type, because that is the one interface all of
		 * ares' setting nodes share. */
		bool (*lookupSetting)(const char *key, char *out, int outSize);

		/* What to plug into each of the machine's ports, by ares' own name for
		 * the device - which differs between machines: a Mega Drive port takes a
		 * "Control Pad" and a PlayStation's a "Digital Gamepad". The order is the
		 * machine's own, and it is not all controllers: a PlayStation's ports run
		 * Controller 1, Memory Card 1, Controller 2, Memory Card 2.
		 *
		 * An EMPTY string leaves a port empty. NULL means "nothing was said",
		 * and the first port then gets whatever that machine's ordinary
		 * controller is. A handheld has no ports and ignores all of this. */
		const char *port[8];
		/* What goes in the pad's own slot, where it has one: a Nintendo 64
		 * pad's "Controller Pak" or "Rumble Pak". */
		const char *portAccessory[8];

		/* Picture only - neither changes the emulated machine. Nintendo 64. */
		bool fastVI;
		bool bobDeinterlace;
	};

	/* Builds the machine. False means it could not be built; error() says why. */
	bool init(const Config &config);
	const char *error(void);

	/* Runs exactly one frame. Input is whatever was last given to setButton and
	 * setAxis, which are indexed by the machine's declared order - see
	 * waterbox/machines.inc, which is generated from ares itself. */
	void setButton(int index, bool held);
	void setAxis(int index, int value);
	void frame(void);

	/* How many of each this machine declares, and whether the one at an index
	 * is actually reachable - a port with nothing in it has buttons declared
	 * but nothing behind them. */
	int buttonCount(void);
	int axisCount(void);
	bool buttonActive(int index);
	bool axisActive(int index);

	/* The picture, valid until the next frame(). Width and height follow the
	 * machine and its video mode, which is why the frontend is told a capacity
	 * and a live size separately. */
	const uint32_t *video(void);
	int videoWidth(void);
	int videoHeight(void);

	/* Turbo: with drawing off, the picture is not produced at all. */
	void setRenderingEnabled(bool on);

	/* Signed 16-bit stereo, interleaved, produced by the frame just run. */
	const int16_t *audio(void);
	int audioSamples(void);

	/* Whether the machine read a controller this frame - a frame that did not
	 * is a lag frame. */
	bool inputWasRead(void);

	/* The nominal refresh, as a rational. It depends on the machine and its
	 * region, which is why the frontend asks the core rather than reading one
	 * number off the package. */
	/* What the machine's table declares for machines ares will not name a rate
	 * for in time; 0/0 for every machine that names its own. The gate compares
	 * it against what the machine reports once it has been running. */
	void declaredRefresh(int *numerator, int *denominator);
	int vsyncNumerator(void);
	int vsyncDenominator(void);

	/* Memory as a block, where the machine has one to point at. Fast, so this
	 * is what a RAM search and the gate's digests use. */
	int memoryDomainCount(void);
	const char *memoryDomainName(int i);
	uint8_t *memoryDomainData(int i);
	int64_t memoryDomainSize(int i);
	int memoryDomainWritable(int i);

	/* Memory as an address space, resolved per access. Every ares machine
	 * publishes these, so this is the one that works everywhere. */
	int busCount(void);
	const char *busName(int i);
	int64_t busSize(int i);
	int busWritable(int i);
	int busPeek(int bus, int address);
	void busPoke(int bus, int address, int value);

	/* The WHOLE machine, as ares serialises it. This is what the gate compares:
	 * the buses above are only the memory ares happens to show a debugger, and
	 * two runs can differ in a register neither of them names. Valid until the
	 * next call. Returns false for a machine that will not serialise.
	 *
	 * Asking costs a synchronisation - ares runs its threads to a clean boundary
	 * first - so it is something the gate does, not something a frame does. */
	bool captureState(const uint8_t **data, int64_t *size);
	bool restoreState(const uint8_t *data, int64_t size);

	/* What the cartridge has saved, for Export Save Data. Empty when it has no
	 * battery. */
	const uint8_t *saveData(void);
	int64_t saveDataSize(void);
	const char *saveDataName(void);

	/* What a Nintendo 64 pad most recently reported, as the two signed bytes
	 * the console reads. The gate uses it to prove that the byte a movie holds
	 * is the byte the game gets - see patches/ares/0008. */
	bool padReport(int pad, int *x, int *y);
}

#endif
