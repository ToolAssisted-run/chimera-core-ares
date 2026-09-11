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
- [x] **A BIOS that is not one file** - a Mega CD BIOS is region specific and a
      PC Engine System Card decides what a disc can do, so each is declared per
      variant and a sync setting picks (`mcd.bios`, `pcecd.card`)
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
- [x] **SetRenderingEnabled** (ruffle 4a49986) - a seek or turbo no longer reads
      the frame back off the GPU and converts it. **1.5 ms a frame on a GTX
      1060** (18.59 -> 17.05); the 6 ms first measured was llvmpipe's and does
      not transfer. What is skipped is the readback and nothing else:
      `Player::render` still runs, because it broadcasts Event.RENDER and
      updates the caches, and that is machine state.
- [x] **Fewer GL crossings** - NOT worth building, and now measured rather than
      assumed. A crossing costs **4.0 ns** (`run-wbx --bench-crossings`), so six
      thousand is 24 us and fifty thousand is 200 us. The driver is the cost,
      not the boundary.
- [x] **The per-frame buffer churn** (chimera 96b98d5) - 139 buffers created,
      filled and deleted every frame. The bridge keeps a deleted buffer's name
      and answers the next request from that list: 18.12 ms a frame to 16.97,
      and the driver's share 13.50 to 11.36. Pictures byte-identical on Ruffle
      and Dolphin with it on and off.
- [ ] **The fence waiting** - the largest item left, ~8 ms a frame, and NOT the
      bridge's to fix. Eighteen blocking `glGetSynciv` a frame, every one
      immediately after a draw; the GPU's own span is the same order as the
      frame and the frame tracks GPU work (ruffle `quality` high to low is
      17.07 ms to 14.43). It is wgpu's OpenGL backend synchronising for want of
      persistent buffer mapping, which a sandboxed guest cannot have because a
      persistent map hands back a HOST pointer. Guest-side work: either stop the
      per-frame resource churn inside wgpu/ruffle, or give the bridge a second
      backend so the guest is not forced onto GL.
- [ ] **BitmapCache does not stamp the render epoch** (ruffle fef5fcf) - a
      correctness gap, not a speed one: a project reopened from disk draws
      `cacheAsBitmap` and filters with a handle the previous backend made. The
      fix is four lines; the test has to come first and it needs a second
      process.
