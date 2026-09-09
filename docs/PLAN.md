# ares as a Chimera core

Why this port is shaped the way it is, what is done, and what is not.

The companion reading is Chimera's `docs/porting-a-core.md` (the order the work
goes in) and `docs/design-principles.md` (why Chimera wants what it wants).

## What ares is, and why it is worth the trouble

ares is one codebase emulating about thirty machines. Chimera's other fourteen
cores are each one emulator for one family; ares is the first where the
machinery, once in, is shared. The entry point was the **Nintendo 64**, which
Chimera has never had - and the reason to start there is that N64 is the
*hardest* of ares' systems to sandbox, so a port that works for it works for
the rest.

**Sixteen machines are declared now**: Nintendo 64, Famicom/NES, Game Boy,
Game Boy Color, Mega Drive, Master System, Game Gear, SG-1000, Atari 2600,
WonderSwan, WonderSwan Color, ZX Spectrum, MyVision - which need nothing of the
user but a cartridge - and the Game Boy Advance, ColecoVision and MSX, which
need a console BIOS the user supplies. Three of them are validated end to end
against real content; the rest are declared, built and enumerated but have never
run a game here. See "What is proven and what is only declared" below, because
the difference matters.

## The three things that had to be solved

### 1. The renderer

ares draws the N64 through **paraLLEl-RDP**, which is a Vulkan compute
implementation of the display processor. That is a non-starter here for two
independent reasons: a waterbox has no graphics driver, and a movie whose
picture depends on somebody's GPU is not a movie anybody can replay.

Chimera's GPU bridge does not help either - it speaks OpenGL, and it makes a
session non-deterministic by design (`docs/gpu-bridge.md`), which is exactly
what a TAS core must not be.

So the RDP is **angrylion's**, a software rasteriser descended from MESS, taken
from the branch BizHawk maintains for exactly this purpose. It is the same
choice BizHawk's Ares64 made, for the same reasons, and it is what makes this
core's picture identical on every machine by construction.

The graft is small, because ares had most of the seam already: `#if
defined(VULKAN)` guards nearly every call into its renderer. Four call sites,
three files (`patches/ares/0004`).

**What it costs:** the MAME licence. angrylion's code may be redistributed but
not sold, which makes the built package non-commercial - the same position the
gpgx, opera and snes9x packages are already in, and declared the same way in
`waterbox/package-licenses.json`. If that is ever unacceptable, the only other
software RDP is another MAME descendant, so the answer would have to be writing
one.

### 2. The recompilers

ares recompiles both the CPU and the RSP through sljit. miniBox does allow
executable pages, so a recompiled core is not impossible in principle - but
generated code is memory the guest writes and then executes, and a movie's
promise is that the same input produces the same machine on every host that
replays it. An interpreter is the version of that promise that can be checked.

`patches/ares/0002` turns ares' existing `Accuracy` switches into build options
(`WANT_CPU_INTERPRETER`, `WANT_RSP_INTERPRETER`), which is precisely what
BizHawk's port did, and both flavours of this core force them on so the
reference and the sandbox are the same emulator. sljit is still compiled and
linked, because ares references its allocator regardless; it is simply never
asked to generate anything.

**Open:** the interpreter is slow. Whether an N64 core is fast enough to TAS
with is a question this port has not measured yet - see "Not done" below.

### 3. Time and entropy

Two seams, both of which make a movie unreplayable if left open, and one of
which was invisible until the gate was pointed at it.

- **The cartridge clock.** ares' N64 RTC calls `time(0)` and `localtime()`
  directly - the host's wall clock and the replayer's timezone. `patches/ares/0005`
  turns that into `Nintendo64::rtcEpoch`, a number the frontend states (the
  `initialTime` setting) and a movie records, read as UTC.

- **Power-on entropy.** Real RDRAM powers on with random timings and ares models
  that, seeding its RNG from `clock()`. This needed no patch - ares already has
  a `Deterministic Entropy` option - but it is off by default, and with it off
  **the same run produced a different machine every time**. Only the picture and
  the audio happened to agree, which is exactly the sort of thing that looks
  fine until a movie desyncs. `machine.cpp` sets it, and the gate has a leg
  whose whole job is to notice if that ever stops happening.

## What else the patches are for

Eleven patches, all small. Six of them are things that are simply *wrong*
upstream in a build like this one, and are worth offering back:

| patch | what |
| --- | --- |
| `ares/0001` | `System::run` calls `vulkan.load` outside the `#if defined(VULKAN)` guard, so a software build does not compile. |
| `ares/0002` | the interpreters become a build option. |
| `ares/0003` | the video interface stopped tracking its dither-filter bit (VI_CONTROL bit 16) when ares handed the VI to paraLLEl-RDP. angrylion needs it. |
| `ares/0004` | the angrylion graft. |
| `ares/0005` | the RTC's clock becomes a seam. |
| `ares/0006` | **RDRAM's hidden bits.** The ninth bit of each RDRAM byte is allocated by the Vulkan path and nowhere else, so in a software build every RDRAM write dereferenced a null pointer. The machine now owns it, which also puts it in savestates where it belongs. |
| `ares/0007` | `nall::Path::user()` dereferences `getpwuid()` without checking it. There is no passwd database in a sandbox. nall's own `inode.hpp` null-checks the identical call, so this is an oversight rather than a design. |
| `ares/0008` | the analogue stick is the byte, not a deflection - see below. |
| `ares/0009` | `nall/decode/mmi.hpp` has no include guard, so including mia and the PC Engine together defines it twice. Only a build with more than one machine in it ever sees this. |
| `ares/0010` | **nothing depends on the host's entropy** - see below. |
| `ares/0011` | **the picture is drawn on the machine's thread** - see below. |
| `angrylion-rdp/0001-2` | the rasteriser includes the header it uses, and its output buffer becomes part of its interface rather than something callers declare `extern` for themselves. |

### Two that only a second machine could have found

Both were invisible while this core was a Nintendo 64 core, and both would have
made every other machine unusable.

**Entropy** (`ares/0010`). ares seeds its generators from `nall::randomSeed()`,
which asks the host for real entropy, and those generators decide what a
console's uninitialised memory holds at power-on - a Game Boy's wave RAM, for
instance. So the same Game Boy run produced a **different machine every time**,
while its picture and its audio agreed perfectly. Worse, the seed comes from
`getrandom`, which miniBox forbids: the sandbox would have been *constant* and
the reference *random*, and the two would have disagreed for a reason nobody
would go looking for. The seed is now a fixed number under
`CHIMERA_DETERMINISTIC_SEED`, in both flavours.

**Threading** (`ares/0011`). ares draws on a second thread and hands frames over
through a condition variable with a **timed wait**. A waterbox has one thread,
so the first frame parked it and the core hung - exactly the trap
`porting-a-core.md` warns about. `ares::Video::Threaded` is now off in both
flavours, so `refresh()` runs inline where `frame()` is called. That is also the
only arrangement in which the picture can be part of a reproducible run.

## The shape of the code

    waterbox/machines.h        which machines this core offers, and how to build each
    waterbox/machines.json     ares' own account of them - GENERATED
    waterbox/machines.inc      what each declared input binds to - GENERATED
    waterbox/machine.{h,cpp}   the emulated machine, and nothing about how it is driven
    waterbox/waterbox.cpp      the guest ABI over it - the core the frontend loads
    waterbox/run-native.cpp    the same machine, outside the sandbox
    waterbox/gate-harness.h    the schedule, shared by both drivers
    waterbox/run-wbx.cpp       the sandbox driver the gate uses

The point of `machine.{h,cpp}` is that "native and sandbox agree" is a claim
about the **sandbox**, not about two hand-written adapters that happen to look
alike. The same file is compiled into both, and `gate-harness.h` means both are
driven by one loop, one option parser and one digest.

### Adding a machine

A row in `machines.h` - ares' name for it, mia's name for it, the function that
builds it, and how big its picture gets - and then regenerate. That is the whole
of it, because everything a movie depends on is asked of ares rather than typed:

    build/meson-native/waterbox/gen-machines > waterbox/machines.json
    ./waterbox/gen-config.py

`gen-machines` builds each machine, walks ares' node graph for the inputs of
every device the core offers, and records the **path** of each one.
`gen-config.py` turns that into the guest's binding table, the frontend's
`machines[]`, and the default keybindings. The gate regenerates both and diffs.

That last part is the reason for all of it. **The order of a controller's
buttons is the wire format a movie is written in**: index 7 has to mean the
same button next year as it did when the movie was recorded. Typing it out
twice - once for the guest, once for the package - is a promise that decays
silently. Deriving it from the emulator means a newer ares that renames or
reorders a button fails the gate instead of renumbering somebody's run.

### mia, and why the other machines are cheap

ares' media layer, `mia`, is what turns a file into the "pak" a machine loads:
it reads the cartridge, works out its region, its save chip, and for the
Nintendo 64 its CIC variant (by computing the IPL2 checksum over the boot code -
real detection, not a table of CRCs). It is compiled with `MIA_LIBRARY`, which
is upstream's own switch for using it without its GUI.

Using mia rather than a hand-rolled loader is the decision that makes a machine
a table entry. It is also why the Nintendo 64 needs **no firmware**: mia carries
its PIF and CIC boot ROMs as compiled-in resources, about four kilobytes,
exactly as ares distributes them.

That last point deserves saying plainly, because it is inherited rather than
chosen: those ROMs are Nintendo's, and this package contains them because ares'
does. The alternative is to drop them and let ares emulate the boot at a high
level instead (`pif/hle.cpp` - it is what runs when the pak has no PIF ROM),
which would be less accurate and would need no such argument. That is Sergio's
call to make, and nothing in the code depends on the answer.

### How a machine's state is compared

Three digests, and the third is the one that matters.

**Video** and **audio** are the frames and samples a run produced. They are
what a person would notice, and they are not enough: a machine can differ in
ways that take a while to reach the screen.

**Memory** is what ares publishes to its own debugger, exported as Chimera
buses (`GetBusCount`, `PeekBus`) - a Game Boy's WRAM, HRAM, VRAM, OAM. Every
ares machine has these, which is why they are here, but they are only the memory
ares happens to *name*. The Nintendo 64 additionally publishes blocks, which is
what a RAM search and a fast digest want.

**State** is the whole machine, as ares serialises it. This is the strong
comparison, and it earned its place immediately: on the Game Boy the buses were
byte-identical across three different runs while the state digest differed every
time - which is how the entropy bug above was found. A digest that only reads
the memory somebody remembered to name is a digest that agrees when it should
not.

## The gate

`waterbox/run-gate.sh`, twenty-three legs, a few minutes. It runs anywhere, CI
included, because the content is free: **PeterLemon/N64** test ROMs (Unlicense)
and **libbet** (Zlib), a real Game Boy homebrew. This core is in the "upstream
ships something free" category of `porting-a-core.md`.

What the legs prove, in the order they matter:

1. **native == sandbox**, on every digest, over eight runs across three
   machines including ones with buttons held and the stick over.
2. **every machine is what ares says it is** - all sixteen rebuilt, their inputs
   re-enumerated, and the committed declaration diffed against them. A runner
   without the console BIOSes checks the thirteen it can and says which three it
   skipped.
3. **the machine survives a savestate**, saved and reloaded before every single
   frame, digests unchanged.
4. **the same run twice is the same machine**, for both validated machines. This
   is the entropy leg, and it fails loudly if the seed ever comes unpinned.
5. **input reaches the machine**: on the N64, idle, A and Start each produce a
   different machine, and so do three different stick positions; on the Game
   Boy, idle, A and Start. Without this a leg could pass with the input wire cut.
6. **the stick is the byte**, seven positions compared against what the
   controller actually reported.
7. **the picture is the one the hardware draws**: `helloworld-cpu` compared
   **pixel for pixel** - all 76800 of them, exactly - against PeterLemon's
   capture from a real console, with the video interface's filtering off.
8. **somebody else's test suite says the CPU is right**: the Game Boy Advance
   runs jsmolka's ARM tests and the gate READS THE SCREEN for the verdict
   (`waterbox/tests/read-verdict.py`). Reading a picture to run a test suite is
   roundabout, and it is the only way a test ROM has to talk - which also means
   this leg cannot pass by agreeing with another run of itself, the way a digest
   comparison can.

Those last two are worth more than they look: they are end-to-end checks against
something outside this repository, rather than against ourselves.

## What is proven and what is only declared

This distinction is the honest part of a sixteen-machine claim, so it gets its
own heading.

**Proven**, in the sense that a real program runs on them and the gate compares
every digest between the two flavours:

- the **Nintendo 64**, whose picture is compared pixel for pixel against a
  capture from real hardware;
- the **Game Boy**, running a whole homebrew game;
- the **Game Boy Advance**, which passes **jsmolka's ARM test suite** - and the
  gate reads the verdict off the screen rather than comparing a digest, so that
  leg cannot pass by agreeing with itself.

**Declared**, in the sense that ares builds them, this core enumerates their
ports and inputs, and the package offers them - but no cartridge has ever been
loaded into one here: Famicom/NES, Game Boy Color, Mega Drive, Master System,
Game Gear, SG-1000, Atari 2600, WonderSwan, WonderSwan Color, ZX Spectrum,
MyVision, ColecoVision, MSX. What is known about them is that they build, that
they power on, and that their declared wire format is ares' own. What is not
known is whether they run a game correctly, what their audio sounds like, or
whether their save data comes back. Each needs a freely distributable ROM and a
gate leg before it should be believed.

**Absent** because no BIOS was to hand: the Atari 5200, the Neo Geo (AES and
MVS) and the Neo Geo Pocket. Each is a table row and a firmware declaration away
once one turns up.

**Absent because it is a bigger job**: the **PlayStation**. ares can build it,
and a BIOS is available, but it loads discs rather than cartridges - which means
CD images, a swap list, and a slot shape this core does not have yet. It is the
next substantial machine rather than the next easy one.

**Not in ares at all**: the Saturn is a stub upstream (one file), so it is not a
machine anybody could offer. The Super Famicom and the PC Engine build but crash
when asked to power on with no cartridge, which is probably nothing more than
that - they are absent until somebody checks.

## Firmware

Three machines need a console BIOS, and none of it is in this repository.

The package declares each one - id, size, SHA-1, a suggested filename and the
condition it is needed under (`{"setting": "machine", "is": "gba"}`) - and
Chimera resolves it, remembers where the user keeps it, and mounts it in the
guest under that id. So the guest opens `fopen("gbaBios")` and never sees a
path, which is the same rule everything else in a core follows.

The hash is not chosen: `gen-config.py` takes it from the file under
`tests/firmware/` that `gen-machines` actually built the machine with. The
package therefore pins the BIOS that was *verified to work*, rather than one
somebody believed would. `tests/firmware/` is gitignored, and the gate skips the
legs whose BIOS is missing and says which - so a CI runner with none of them
still checks the other thirteen machines and reports honestly that it did.

The one machine that could stop needing this is the **MSX**: C-BIOS is an
open-source MSX BIOS, and if ares runs cartridges with it then the MSX could
move to the group that needs nothing. Untested.

## The analogue stick

Worth its own section, because it is a decision about what a movie *means*.

Modern ares takes stick input in the **±32767** range of a PC gamepad and puts
it through a deadzone, a response curve and the octagonal gate a real stick is
physically held in before the machine sees it. That is right for somebody
playing with a thumbstick. It is wrong for a movie, and it hid a bug: passing
the console's own signed byte landed *inside the deadzone*, so the stick did
nothing whatsoever - found by an input leg that watched three stick positions
produce one machine.

A Nintendo 64 tool-assisted run is written in **the byte the controller
reports**. That is what mupen and BizHawk record, what every existing N64 movie
contains, and what a frame of one means: not "the player pushed the stick about
this far" but "the game read exactly this number". So `patches/ares/0008` takes
the shaping out and the axes pass straight through - the frontend hands over the
signed byte and the machine reads it back unchanged. The declared range is the
byte itself, -128..127, positive up and right.

Two consequences worth stating. Everything in the byte range is now reachable,
including the small values that the deadzone used to swallow whole. And a run
recorded here means the same thing as a run recorded in mupen, which is the
point of choosing this convention over the physically faithful one.

The gate checks it directly rather than by digest: seven stick positions, each
compared against what the controller actually reported to the machine.

## Not done

In rough order of what would matter first:

- **Speed.** Nothing is measured, on any machine. For the Nintendo 64 the CPU
  and RSP interpreters plus a software RDP is the slow end of every choice
  available, and whether a real game runs at a workable rate is unknown. If it
  does not, the questions are (in order) the RSP recompiler,
  `Accuracy::RSP::SIMD`, and whether miniBox's executable pages can host sljit
  safely.
- **A real game, anywhere.** Every test here is homebrew of a few kilobytes.
  Nothing has booted a commercial cartridge, so nothing is known in practice
  about save chips, the 64DD, the Transfer Pak or the Expansion Pak.
- **Eleven machines that have never run anything** - see above. Each wants a
  freely distributable ROM in `tests/content/` and a gate leg.
- **The machines that need firmware**, which is where the most valuable ones
  are: the Game Boy Advance, the PlayStation, the Neo Geo. The package has to
  declare that firmware and mia has to be handed it; neither is hard, and both
  are undone.
- **`n64-systemtest`** (MIT) is the accuracy suite ares itself is tested against
  and would be a far stronger gate than three homebrew ROMs. It needs a Rust
  MIPS toolchain to build, which is why it is not here yet.
- **Save data beyond the Nintendo 64.** Every other machine's saves live inside
  the savestate and cannot be exported, because mia writes them through a host
  path this core does not give it.
- **Publishing**: no `chimera.yml`, no row in Chimera's `official-cores.json`,
  no release. The package builds and passes Chimera's contract tests, but
  nothing publishes it yet.
- **Optional tooling**: no registers, no trace, no core-rendered surfaces.
  Memory domains, buses and the Nintendo 64's save-data export are done.

## Numbers, as of the thirteen-machine build

- `core.wbx` is 16.3 MB with every machine in it - it was 9.3 MB with only the
  Nintendo 64, so twelve more machines cost seven megabytes between them. The
  package is built reproducibly (the build script packs twice and compares).
- The declared arena is 296 MB, and **mmap is the part that matters**: a 1 MB
  cartridge already needs more than 80 MB there, because the ROM is held three
  times while loading - mia's vector, the pak's copy, and ares' own
  `Memory::Writable`. 256 MB leaves room for the 64 MB carts. Collapsing those
  three copies is the obvious saving and has not been attempted.
- The Nintendo 64's boot ROM takes about **90 frames** to hand over to the
  cartridge. Any test that runs fewer than that is testing the PIF, not the game
  - which cost an hour the first time, when a 30-frame run drew a black screen
  and looked like a broken renderer.
