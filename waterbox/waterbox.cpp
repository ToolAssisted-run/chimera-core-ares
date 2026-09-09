/* core.wbx - ares as a Chimera waterbox core.
 *
 * The machine is machine.cpp, which the native reference builds too; this file
 * is only the ABI over it. There is no serialize/deserialize export: the whole
 * machine lives in guest memory and miniBox savestates the arena, which is the
 * point of the waterbox flavour.
 *
 * The cartridge arrives as a mounted file, named by the project's slot map, and
 * is read during Init. Nothing here ever sees a host path.
 */
#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "machine.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace
{
	/* The wire format, and the order waterbox.config declares. 66 buttons is
	 * more than a packed word holds, so everything rides the SetButton channel
	 * and FrameAdvance's argument is ignored. */
	enum
	{
		BTN_POWER = 0,
		BTN_RESET,
		BTN_PAD0,           /* 16 per pad, four pads */
		BTN_PER_PAD = 16,
		BTN_COUNT = BTN_PAD0 + 4 * BTN_PER_PAD,
	};

	enum { AXIS_COUNT = 8 };  /* X,Y per pad */

	bool g_buttons[BTN_COUNT];
	int g_axes[AXIS_COUNT];

	bool g_inited = false;
	char g_loadError[256] = "";

	machine::Port g_ports[4] = {
		machine::Port::Gamepad, machine::Port::Unplugged,
		machine::Port::Unplugged, machine::Port::Unplugged,
	};

	machine::Port portFromSetting(const char *name, machine::Port dflt)
	{
		char value[32];
		if (wbx_setting_str(name, value, (int)sizeof value) < 0) return dflt;
		if (strcmp(value, "none") == 0) return machine::Port::Unplugged;
		if (strcmp(value, "gamepad") == 0) return machine::Port::Gamepad;
		if (strcmp(value, "controllerPak") == 0) return machine::Port::GamepadWithControllerPak;
		if (strcmp(value, "rumblePak") == 0) return machine::Port::GamepadWithRumblePak;
		if (strcmp(value, "mouse") == 0) return machine::Port::Mouse;
		return dflt;
	}
}

extern "C"
{

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }

ECL_EXPORT int Init(void)
{
	/* The rom's name comes from the project's slot map; "rom" is the fallback
	 * for a host that mounts one file and no map. */
	static char romName[256];
	if (!wbx_slot_first("rom", romName, (int)sizeof romName))
	{
		snprintf(romName, sizeof romName, "rom");
	}

	machine::Config config = {};
	config.romFile = romName;

	char region[16];
	config.pal = wbx_setting_str("region", region, (int)sizeof region) >= 0
		&& strcmp(region, "pal") == 0;

	/* A movie's own clock. The frontend states it, so a replay next year builds
	 * the same machine as today's. */
	config.initTimeUnix = (uint64_t)wbx_setting_long("initialTime", 0);

	config.fastVI = wbx_setting_bool("fastVI", 0) != 0;
	config.bobDeinterlace = wbx_setting_bool("bobDeinterlace", 0) != 0;

	g_ports[0] = portFromSetting("port1", machine::Port::Gamepad);
	g_ports[1] = portFromSetting("port2", machine::Port::Unplugged);
	g_ports[2] = portFromSetting("port3", machine::Port::Unplugged);
	g_ports[3] = portFromSetting("port4", machine::Port::Unplugged);
	for (int i = 0; i < 4; i++) config.port[i] = g_ports[i];

	if (!machine::init(config))
	{
		snprintf(g_loadError, sizeof g_loadError, "%s", machine::error());
		return 0;
	}

	g_inited = true;
	return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < BTN_COUNT) g_buttons[index] = state != 0;
}

ECL_EXPORT void SetAxis(int index, int value)
{
	if (index >= 0 && index < AXIS_COUNT) g_axes[index] = value;
}

/* A port with nothing in it has no buttons and no axes; the frontend hides
 * them rather than offering controls that go nowhere. */
ECL_EXPORT int IsButtonActive(int index)
{
	if (index == BTN_POWER || index == BTN_RESET) return 1;
	if (index < BTN_PAD0 || index >= BTN_COUNT) return 0;
	int pad = (index - BTN_PAD0) / BTN_PER_PAD;
	return g_ports[pad] != machine::Port::Unplugged;
}

ECL_EXPORT int IsAxisActive(int index)
{
	if (index < 0 || index >= AXIS_COUNT) return 0;
	return g_ports[index / 2] != machine::Port::Unplugged;
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	(void)packed;  /* 66 buttons: input rides the SetButton wide channel */
	if (!g_inited) return;

	machine::Input input = {};
	input.power = g_buttons[BTN_POWER];
	input.reset = g_buttons[BTN_RESET];

	for (int pad = 0; pad < 4; pad++)
	{
		const bool *b = &g_buttons[BTN_PAD0 + pad * BTN_PER_PAD];
		machine::Pad &p = input.pad[pad];
		p.up = b[0];  p.down = b[1];  p.left = b[2];  p.right = b[3];
		p.a = b[4];   p.b = b[5];     p.z = b[6];     p.start = b[7];
		p.l = b[8];   p.r = b[9];
		p.cUp = b[10]; p.cDown = b[11]; p.cLeft = b[12]; p.cRight = b[13];
		/* b[14], b[15] are spare: the pak in the pad is a setting, not a button */
		int x = g_axes[pad * 2 + 0];
		int y = g_axes[pad * 2 + 1];
		if (x < -127) x = -127; if (x > 127) x = 127;
		if (y < -127) y = -127; if (y > 127) y = 127;
		p.x = (int8_t)x;
		p.y = (int8_t)y;
	}

	machine::frame(input);
}

ECL_EXPORT void SetRenderingEnabled(int on) { machine::setRenderingEnabled(on != 0); }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return (uint32_t *)machine::video(); }
ECL_EXPORT int GetVideoWidth(void) { return machine::videoWidth(); }
ECL_EXPORT int GetVideoHeight(void) { return machine::videoHeight(); }

ECL_EXPORT int16_t *GetAudio(void) { return (int16_t *)machine::audio(); }
ECL_EXPORT int GetAudioSampleCount(void) { return machine::audioSamples(); }

ECL_EXPORT int InputWasRead(void) { return machine::inputWasRead() ? 1 : 0; }

ECL_EXPORT int GetVsyncNumerator(void) { return machine::vsyncNumerator(); }
ECL_EXPORT int GetVsyncDenominator(void) { return machine::vsyncDenominator(); }

ECL_EXPORT int GetMemoryDomainCount(void) { return machine::memoryDomainCount(); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return machine::memoryDomainName(i); }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return machine::memoryDomainData(i); }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return machine::memoryDomainSize(i); }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return machine::memoryDomainWritable(i); }

ECL_EXPORT int32_t GetSaveDataFileCount(void) { return machine::saveDataSize() > 0 ? 1 : 0; }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) { return i == 0 ? machine::saveDataName() : nullptr; }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { return i == 0 ? machine::saveDataSize() : 0; }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i) { return i == 0 ? machine::saveData() : nullptr; }

} // extern "C"
