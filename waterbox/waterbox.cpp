/* core.wbx - ares as a Chimera waterbox core.
 *
 * The machine is machine.cpp, which the native reference builds too; this file
 * is only the ABI over it. There is no serialize/deserialize export: the whole
 * machine lives in guest memory and miniBox savestates the arena, which is the
 * point of the waterbox flavour.
 *
 * Which console a session is comes from the "machine" setting, and everything
 * else follows from it - the buttons, the picture, the medium. The cartridge
 * arrives as a mounted file named by the project's slot map. Nothing here ever
 * sees a host path.
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
	bool g_inited = false;
	char g_loadError[256] = "";
	const uint8_t *g_stateData = nullptr;

	char g_settingBuf[8][64];
	int g_settingsUsed = 0;

	/* Copies a string setting somewhere that outlives this call, or returns the
	 * default. */
	const char *settingString(const char *name, const char *dflt)
	{
		if (g_settingsUsed >= 8) return dflt;
		char *slot = g_settingBuf[g_settingsUsed];
		if (wbx_setting_str(name, slot, (int)sizeof g_settingBuf[0]) < 0) return dflt;
		/* "none" is a decision - leave the port empty - and is not the same as
		 * saying nothing, which lets the machine put its own controller in the
		 * first port. */
		if (!*slot || !strcmp(slot, "none")) return "";
		g_settingsUsed++;
		return slot;
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
	if (!wbx_slot_first("rom", romName, (int)sizeof romName)) snprintf(romName, sizeof romName, "rom");

	/* A machine that boots with nothing in its drive - a PlayStation reaching
	 * its BIOS shell, an MSX reaching its own BASIC - is started with no medium
	 * at all. Rather than ask which machines those are, look.
	 *
	 * The test is EMPTY, not absent. Chimera mounts the rom slot whatever the
	 * project holds, so a project with no game in it still has a file of that
	 * name and it is zero bytes long; asking only whether it opens finds a
	 * medium that is not there, and the machine then fails to load a game
	 * nobody chose. run-wbx mounts real files only, so both runners agree on
	 * this reading and only this one. */
	const char *rom = romName;
	if (FILE *probe = fopen(romName, "rb"))
	{
		if (fseek(probe, 0, SEEK_END) != 0 || ftell(probe) <= 0) rom = nullptr;
		fclose(probe);
	}
	else rom = nullptr;

	/* A slot on the CARTRIDGE, for the machines whose cartridges have one -
	 * a Satellaview pack, a Sufami Turbo minicart, a Game Boy cartridge in a
	 * Super Game Boy. Absent for every other project, and optional in all of
	 * them. Read the same way the rom slot is, including the empty-file test:
	 * Chimera mounts a declared slot whether or not the project filled it. */
	static char subName[256];
	const char *subRom = nullptr;
	if (wbx_slot_first("subcart", subName, (int)sizeof subName))
	{
		subRom = subName;
		if (FILE *probe = fopen(subName, "rb"))
		{
			if (fseek(probe, 0, SEEK_END) != 0 || ftell(probe) <= 0) subRom = nullptr;
			fclose(probe);
		}
		else subRom = nullptr;
	}

	static char machineName[32];
	if (wbx_setting_str("machine", machineName, (int)sizeof machineName) < 0)
	{
		snprintf(machineName, sizeof machineName, "n64");
	}
	/* The setting is lower case because that is what an enum setting looks like
	 * in a project; the machine ids are upper. */
	for (char *c = machineName; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;

	machine::Config config = {};
	config.machine = machineName;
	config.romFile = rom;
	config.subRomFile = subRom;

	char region[16];
	config.pal = wbx_setting_str("region", region, (int)sizeof region) >= 0 && !strcmp(region, "pal");

	/* A movie's own clock. The frontend states it, so a replay next year builds
	 * the same machine as today's. */
	config.initTimeUnix = (uint64_t)wbx_setting_long("initialTime", 0);

	config.fastVI = wbx_setting_bool("fastVI", 0) != 0;
	config.bobDeinterlace = wbx_setting_bool("bobDeinterlace", 0) != 0;

	/* Null when the project said nothing, so the machine chooses its own
	 * ordinary controller - "Gamepad" on a Famicom, "Control Pad" on a Mega
	 * Drive, "Digital Gamepad" on a PlayStation. */
	/* ares' own settings are read by key, because which keys exist depends on
	 * which machine this is - see machines.inc. */
	config.lookupSetting = [](const char *key, char *out, int outSize) -> bool
	{
		return wbx_setting_str(key, out, outSize) >= 0;
	};

	config.port[0] = settingString("port1", nullptr);
	config.port[1] = settingString("port2", nullptr);
	config.port[2] = settingString("port3", nullptr);
	config.port[3] = settingString("port4", nullptr);
	config.portAccessory[0] = settingString("port1Accessory", nullptr);
	config.portAccessory[1] = settingString("port2Accessory", nullptr);

	if (!machine::init(config))
	{
		snprintf(g_loadError, sizeof g_loadError, "%s", machine::error());
		return 0;
	}

	g_inited = true;
	return 1;
}

/* Every machine's controller is wider than a packed word for at least one of
 * them, so input always rides the SetButton channel and FrameAdvance's argument
 * is unused. */
ECL_EXPORT void SetButton(int32_t index, int32_t state) { machine::setButton(index, state != 0); }
ECL_EXPORT void SetAxis(int index, int value) { machine::setAxis(index, value); }

/* A port with nothing in it has its buttons declared but nothing behind them;
 * the frontend hides those rather than offering controls that go nowhere. */
ECL_EXPORT int IsButtonActive(int index) { return machine::buttonActive(index) ? 1 : 0; }
ECL_EXPORT int IsAxisActive(int index) { return machine::axisActive(index) ? 1 : 0; }

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	(void)packed;
	if (!g_inited) return;
	machine::frame();
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

/* Every ares machine describes its own memory to its debugger, and that is an
 * address space rather than a block - so it is published as a bus, which the
 * frontend resolves per access. It is how a machine with no contiguous RAM to
 * point at still gets a RAM search. */
ECL_EXPORT int GetBusCount(void) { return machine::busCount(); }
ECL_EXPORT const char *GetBusName(int i) { return machine::busName(i); }
ECL_EXPORT int64_t GetBusSize(int i) { return machine::busSize(i); }
ECL_EXPORT int GetBusWritable(int i) { return machine::busWritable(i); }
ECL_EXPORT int PeekBus(int bus, int address) { return machine::busPeek(bus, address); }
ECL_EXPORT void PokeBus(int bus, int address, int value) { machine::busPoke(bus, address, value); }

/* The whole machine as ares serialises it, for the equivalence gate. The
 * frontend does not need this - miniBox savestates the arena, which is the
 * point of a waterbox core - but the gate does, because it is the only
 * comparison that covers every register rather than the memory ares happens to
 * show a debugger. Captured on demand, into guest memory the host then reads. */
ECL_EXPORT int64_t CaptureState(void)
{
	const uint8_t *data = nullptr;
	int64_t size = 0;
	if (!machine::captureState(&data, &size)) return 0;
	g_stateData = data;
	return size;
}

ECL_EXPORT const uint8_t *GetStateBuffer(void) { return g_stateData; }

ECL_EXPORT int32_t GetSaveDataFileCount(void) { return machine::saveDataSize() > 0 ? 1 : 0; }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) { return i == 0 ? machine::saveDataName() : nullptr; }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { return i == 0 ? machine::saveDataSize() : 0; }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i) { return i == 0 ? machine::saveData() : nullptr; }

} // extern "C"
