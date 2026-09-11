# Machines and Ruffle: what is done and what is left

Updated as the work lands. `[x]` means committed and gated.

## Mechanisms
- [x] **Multi-file firmware** - `Spec.extraFirmware` lists the rest and mia
      takes them together through its own `loadMultiple`.
- [ ] **Multi-pak machines** - a CD or disk machine is system + firmware +
      medium, with `platform->pak(node)` answering by node name. The core's
      `pak()` answers only "the system, or the cartridge".
- [ ] **A second cartridge slot** - Satellaview and Sufami Turbo plug a second
      medium into a Super Famicom.

## Machines
- [x] **PC Engine / TurboGrafx-16** and **SuperGrafx** (ares 3156b55)
- [x] **Super Famicom** - `ipl.rom` supplied; boards and game databases
      compiled in. Coprocessor games (DSP1, CX4, ST010...) still refused: the
      firmware declaration cannot yet say "only for some cartridges".
- [x] **Mega 32X** - vector table and both SH-2 boot ROMs supplied
- [x] **MSX2** - main and sub ROMs supplied; cassettes (.cas/.wav/.tzx) load
      on both MSX machines, and an MSX with a CARTRIDGE no longer crashes
- [ ] **Nintendo 64DD** - needs an IPL (supplied) and a disk image
- [ ] **Mega CD**, **Mega CD 32X** - BIOS supplied, needs the multi-pak work
- [ ] **PC Engine CD** - system cards supplied, needs the multi-pak work
- [ ] **Satellaview**, **Sufami Turbo** - need the second slot
- [x] ~~Arcade~~ - dropped (user, 2026-09-11)

## Ruffle
- [x] A getenv per GL call, and a greenzone capturing what it could not afford
      (chimera e00b19c): 112 ms a frame to 48 on New Star Soccer
- [ ] **SetRenderingEnabled** - the guest exports none, so a seek or turbo draws
      every frame at full price
- [ ] **Fewer GL crossings** - ~6,000 an average frame, 50,000 in a heavy one.
      A batching problem rather than a constant-factor one.
