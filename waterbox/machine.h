/* machine.h - the emulated machine, and nothing about how it is driven.
 *
 * One layer, two users: waterbox.cpp (the core the frontend loads) and
 * run-native.cpp (the reference the gate compares against). They share this so
 * that "native and sandbox agree" is a claim about the SANDBOX, not about two
 * hand-written adapters that happen to look alike.
 *
 * The machine is ares. Everything here is bytes in and bytes out: no paths, no
 * host devices, no clock. A file is a name the host has mounted; time is a
 * number the caller supplies.
 */
#ifndef CHIMERA_ARES_MACHINE_H
#define CHIMERA_ARES_MACHINE_H

#include <stdint.h>

namespace machine
{
	/* What a controller port has in it. The N64's port is a slot: the pad is one
	 * device, and what is plugged into the PAD is another. */
	enum class Port : int
	{
		Unplugged = 0,
		Gamepad,
		GamepadWithControllerPak,
		GamepadWithRumblePak,
		Mouse,
	};

	struct Config
	{
		/* Mounted file names, not host paths. Empty means absent. */
		const char *romFile;
		const char *saveDataFile;

		bool pal;
		uint64_t initTimeUnix; /* what the cartridge's RTC starts at */

		Port port[4];

		/* Picture only - neither changes the emulated machine. */
		bool fastVI;
		bool bobDeinterlace;
	};

	struct Pad
	{
		bool up, down, left, right;
		bool a, b, z, start, l, r;
		bool cUp, cDown, cLeft, cRight;
		int8_t x, y; /* analogue stick, -127..127 */
	};

	struct Input
	{
		Pad pad[4];
		bool power; /* a hard reset */
		bool reset; /* the console's own reset button */
	};

	/* Builds the machine. False means it could not be built; error() says why. */
	bool init(const Config &config);
	const char *error(void);

	/* Runs exactly one frame. */
	void frame(const Input &input);

	/* The picture, valid until the next frame(). Width is fixed; height follows
	 * the video mode, which is why the frontend is told a capacity and a live
	 * size separately. */
	const uint32_t *video(void);
	int videoWidth(void);
	int videoHeight(void);

	/* Turbo: with drawing off the rasteriser is skipped entirely. */
	void setRenderingEnabled(bool on);

	/* Signed 16-bit stereo, interleaved, produced by the frame just run. */
	const int16_t *audio(void);
	int audioSamples(void);

	/* Whether the machine read the controllers this frame - a frame that did not
	 * is a lag frame. */
	bool inputWasRead(void);

	/* The nominal refresh, as a rational. It depends on the region, which is why
	 * the frontend asks the core rather than reading one number off the package.
	 * Nominal because the N64's real rate is whatever the game programs its video
	 * interface to produce, and that changes while a game runs. */
	int vsyncNumerator(void);
	int vsyncDenominator(void);

	/* The machine's memory, for RAM search and the gate's digests. */
	int memoryDomainCount(void);
	const char *memoryDomainName(int i);
	uint8_t *memoryDomainData(int i);
	int64_t memoryDomainSize(int i);
	int memoryDomainWritable(int i);

	/* What the cartridge has saved, for Export Save Data. Empty when the cart
	 * has no battery. */
	const uint8_t *saveData(void);
	int64_t saveDataSize(void);
	const char *saveDataName(void);
}

#endif
