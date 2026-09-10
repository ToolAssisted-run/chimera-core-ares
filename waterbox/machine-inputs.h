/* machine-inputs.h - the declared inputs of each machine, as plain data.
 *
 * Separate from machines.h because the sandbox DRIVER needs these names and
 * must not link the emulator to get them: run-wbx drives core.wbx through the
 * miniBox host, and pulling ares in beside it would be a second copy of the
 * machine in the same process.
 *
 * The table itself is waterbox/machines.inc, generated from ares.
 */
#ifndef CHIMERA_ARES_MACHINE_INPUTS_H
#define CHIMERA_ARES_MACHINE_INPUTS_H

namespace machines
{
	/* One declared input, and the node in ares' graph it drives. */
	struct InputDecl
	{
		const char *name;  /* what the frontend and a movie call it */
		const char *path;  /* what root->find() resolves, e.g. "Controls/Start" */
	};

	/* One setting ares declares on a machine, and the node it writes to. Every
	 * ares setting type reads and writes through one string-shaped interface on
	 * its base, so the value travels as text and the machine converts it. */
	struct SettingDecl
	{
		const char *key;   /* what the package and a movie call it, e.g. "gb.fastBoot" */
		const char *path;  /* what root->find() resolves, e.g. "Fast Boot" */
	};

	struct MachineInputs
	{
		const char *id;
		const InputDecl *buttons;
		int buttonCount;
		const InputDecl *axes;
		int axisCount;
		const SettingDecl *settings;
		int settingCount;
		/* What goes into the first port when nobody says otherwise: the
		 * ordinary controller, or null for a machine with no ports at all. */
		const char *defaultDevice;
		/* True for a machine that starts with nothing in its drive. */
		bool bootsWithoutMedium;
	};
}

#endif
