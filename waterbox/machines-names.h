/* machines-names.h - the declared name of each input, for the drivers.
 *
 * The guest does not need these: the frontend knows a button by its index, and
 * the package's waterbox.config carries the names. The gate does need them, so
 * that a leg can say --hold Start rather than --hold 7 and mean the same thing
 * on every machine.
 */
#ifndef CHIMERA_ARES_MACHINE_NAMES_H
#define CHIMERA_ARES_MACHINE_NAMES_H

#include "machine-inputs.h"
#include "machines.inc"

#include <string.h>

namespace machines
{
	inline const MachineInputs *&current(void)
	{
		static const MachineInputs *chosen = nullptr;
		return chosen;
	}

	inline void useMachine(const char *id)
	{
		current() = nullptr;
		for (auto &entry : kMachineInputs) if (id && !strcmp(entry.id, id)) current() = &entry;
	}

	inline int buttonCount(void) { auto *m = current(); return m ? m->buttonCount : 0; }
	inline int axisCount(void) { auto *m = current(); return m ? m->axisCount : 0; }

	inline const char *buttonName(int i)
	{
		auto *m = current();
		return (m && i >= 0 && i < m->buttonCount) ? m->buttons[i].name : nullptr;
	}

	inline const char *axisName(int i)
	{
		auto *m = current();
		return (m && i >= 0 && i < m->axisCount) ? m->axes[i].name : nullptr;
	}

	/* True for a machine that starts with nothing in its drive. Generated, so
	 * the drivers need not link the emulator to ask. */
	inline bool bootsWithoutMedium(const char *id)
	{
		for (auto &entry : kMachineInputs) if (id && !strcmp(entry.id, id)) return entry.bootsWithoutMedium;
		return false;
	}

	/* What to plug into the first port when nobody said - the ordinary
	 * controller for this machine, or null for a handheld with no ports. */
	inline const char *defaultDevice(const char *id)
	{
		for (auto &entry : kMachineInputs) if (id && !strcmp(entry.id, id)) return entry.defaultDevice;
		return nullptr;
	}
}

#endif
