# Machines and Ruffle: what is done and what is left

Updated as the work lands. `[x]` means committed and gated.

## Mechanisms
- [x] **Multi-file firmware** - `Spec.extraFirmware` lists the rest and mia
      takes them together through its own `loadMultiple`.
- [x] **Multi-pak machines** - `Spec.mediumNode` says which slot the medium
      goes in, and `Spec.firmwareMedium` / `firmwareNode` cover a BIOS that is
      itself a cartridge. The gate takes a whole DIRECTORY as one medium, so a
      cue sheet travels with its track files.
- [x] **A second cartridge slot** - `Spec.subSlots`, chosen by the second
      file's extension, plugged in after the cartridge that carries it exists.
      The package asks through a `subcart` file slot shown only where it
      applies.

## Machines
- [x] **PC Engine / TurboGrafx-16** and **SuperGrafx** (ares 3156b55)
- [x] **Super Famicom** - `ipl.rom` supplied; boards and game databases
      compiled in. Coprocessor games (DSP1, CX4, ST010...) still refused: the
      firmware declaration cannot yet say "only for some cartridges".
- [x] **Mega 32X** - vector table and both SH-2 boot ROMs supplied
- [x] **MSX2** - main and sub ROMs supplied; cassettes (.cas/.wav/.tzx) load
      on both MSX machines, and an MSX with a CARTRIDGE no longer crashes
- [x] **Mega CD / Sega CD** - the disc goes to the tray rather than to the
      cartridge slot beside it
- [x] **Mega CD 32X** - both add-ons at once: the CD BIOS plus the 32X's three
- [x] **PC Engine CD / TurboDuo** - the System Card is a HuCard in the
      cartridge slot, so the machine loads three paks
- [x] **Satellaview** - the BS-X cartridge with a memory pack in its slot;
      reaches the BS-X title screen and passes all three local legs
- [ ] **Sufami Turbo** - the mechanism is in; what is missing is the Sufami
      Turbo BASE cartridge ROM, which goes in the console slot with the .st
      minicart inside it. Not in this collection.
- [ ] **Nintendo 64DD** - tried and reverted. `PIF::bootHLE` reads the
      cartridge, and a 64DD boots from its own IPL with no cartridge at all, so
      it segfaults before the first frame. Needs either Nintendo's PIF ROM or an
      HLE boot for the drive's path - and a disk image, which is also missing.
- [ ] **Neo Geo MVS** - diagnosed, not shipped. Two things were missing and
      one still is:
      - the board's own fix-layer tile ROM (`sfix.sfix`, which ares' `power()`
        reads as `static.rom` on an MVS and nowhere else). mia never supplies
        it, so every letter the BIOS drew came out of uninitialised tile
        memory and the machine looked dead.
      - **ares has no uPD4990A.** The real-time clock is commented out
        (`ares/ng/cpu/memory.cpp`, REG_RTCCTRL and REG_STATUS_A bits 6-7 are
        hardcoded to zero), and the MVS BIOS spins on the time pulse for ever
        - about a thousand reads of REG_STATUS_A per frame. Feeding it a
        toggling pulse gets straight past it to a clean `CALENDAR ERROR`, so
        the chip is the whole of what is left. The AES is unaffected: it never
        reads the register.
      This is what the Neo Geo's DIP switches are FOR - an AES has none - so
      the MVS is the machine worth finishing.
- [x] ~~Arcade~~ - dropped (user, 2026-09-11)

## Ruffle
- [x] A getenv per GL call, and a greenzone capturing what it could not afford
      (chimera e00b19c): 112 ms a frame to 48 on New Star Soccer
- [ ] **SetRenderingEnabled** - the guest exports none, so a seek or turbo draws
      every frame at full price
- [ ] **Fewer GL crossings** - ~6,000 an average frame, 50,000 in a heavy one.
      A batching problem rather than a constant-factor one.
