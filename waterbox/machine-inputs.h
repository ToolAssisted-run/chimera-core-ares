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

	struct MachineInputs
	{
		const char *id;
		const InputDecl *buttons;
		int buttonCount;
		const InputDecl *axes;
		int axisCount;
		/* What goes into the first port when nobody says otherwise: the
		 * ordinary controller, or null for a machine with no ports at all. */
		const char *defaultDevice;
	};
}

#endif
