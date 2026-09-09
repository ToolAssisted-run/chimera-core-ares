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

**Twenty-one machines are declared now**: Nintendo 64, Famicom/NES, Game Boy,
Game Boy Color, Mega Drive, Master System, Game Gear, SG-1000, Atari 2600,
WonderSwan, WonderSwan Color, ZX Spectrum, MyVision - which need nothing of the
user but a cartridge - and the Game Boy Advance, ColecoVision and MSX, which
need a console BIOS the user supplies, the PlayStation which loads discs, and
the Atari 5200, Neo Geo, Neo Geo Pocket and Neo Geo Pocket Color. Four are
validated by the gate and fifteen against real commercial games; the rest are declared, built and enumerated but have never
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

**Threading** (`ares/0011`), which bit twice and differently.

ares draws on a second thread and hands frames over through a condition variable
with a **timed wait**. A waterbox has one thread, so the Game Boy's first frame
parked it and the core hung - exactly the trap `porting-a-core.md` warns about.

The PlayStation then failed in the far nastier way. Its GPU rasterises on a
thread of its own (`Accuracy::GPU::Threaded`), and a thread that never runs does
not hang anything: the queue simply fills, every register ends up exactly where
it should be, and **not one pixel is drawn**. The whole four-megabyte machine
state matched between the two flavours except the contents of video memory, and
the sandbox's picture was the noise it had been filled with at power-on. That
looks like a broken renderer, and it is a missing thread.

Both are off under `CHIMERA_SINGLE_THREADED`, in both flavours - the reference
must be single-threaded too, or the two are not running the same program.

**Still threaded, and therefore unusable here**: the LaserActive video prefetch
in `md/mcd/megald.cpp` and `pce/pcd/ldrom2.cpp`. Neither is reached by the
machines this core offers - they are the Mega LD and PC Engine LD media - but
anybody adding those will meet this section again.

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

`waterbox/run-gate.sh`, twenty-six legs, a few minutes. It runs anywhere, CI
included, because the content is free: **PeterLemon/N64** test ROMs (Unlicense)
and **libbet** (Zlib), a real Game Boy homebrew. This core is in the "upstream
ships something free" category of `porting-a-core.md`.

What the legs prove, in the order they matter:

1. **native == sandbox**, on every digest, over nine runs across four machines
   including ones with buttons held and the stick over.
2. **every machine is what ares says it is** - all seventeen rebuilt, their
   inputs re-enumerated, and the committed declaration diffed against them. A
   runner without the console BIOSes checks the thirteen it can and says which
   four it skipped.
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

This distinction is the honest part of a seventeen-machine claim, so it gets its
own heading. There are three tiers, and the middle one is new: a machine can be
checked against a real commercial game on this developer's machine without that
check being something the gate can carry, because the game may not be
redistributed.

### Proven by the gate

A real program runs and the gate compares every digest between the two
flavours, on content anybody may have:

- the **Nintendo 64**, whose picture is compared pixel for pixel against a
  capture from real hardware;
- the **Game Boy**, running a whole homebrew game;
- the **Game Boy Advance**, which passes **jsmolka's ARM test suite** - and the
  gate reads the verdict off the screen rather than comparing a digest, so that
  leg cannot pass by agreeing with itself;
- the **PlayStation**, which reaches its BIOS shell with nothing in its drive.

### Proven against a real game, off the record

Fifteen machines have run a real commercial cartridge here with **native ==
sandbox on every digest** - video, audio, memory and the whole serialised
machine. The ROMs are not this repository's to carry, so the gate cannot say
this; it is written down instead:

| machine | game | speed |
| --- | --- | --- |
| Nintendo 64 | Super Mario 64 | 46 fps (0.76x) |
| Famicom / NES | AccuracyCoin | 278 fps |
| Game Boy | A Fairy Without Wings | 324 fps |
| Game Boy Color | Air Traffic Controller | 323 fps |
| Game Boy Advance | Another World | 193 fps |
| Mega Drive | Amy Rose in Sonic the Hedgehog | 192 fps |
| Master System | Galactic Protector | 441 fps |
| Game Gear | Frogger | 467 fps |
| SG-1000 | Monaco GP | 823 fps |
| Atari 2600 | Alien | 307 fps |
| WonderSwan Color | Saint Seiya | 892 fps |
| ColecoVision | Carnival | 881 fps |
| Neo Geo AES | Samurai Shodown IV | - |
| Neo Geo Pocket Color | Baseball Stars Color | - |
| PlayStation | Ganbare Goemon (a .cue disc) | 23 fps |

Super Mario 64 matching byte for byte between the reference and the sandbox is
the strongest thing anybody has asked of this core so far.

**A Neo Geo romset is identified by its FILE NAME**, not its contents: mia looks
the set up in its database by the name of the zip. `samsho4.zip` works and
`game.zip` does not. A project keeps the file's own name, so this is only a trap
for anybody writing a test.

### Declared, and not to be trusted yet

- **MSX**: its database is compiled in now, but there is no MSX ROM to hand, so
  it has still never run anything.
- **MyVision**: no ROM for it exists anywhere to hand, so it has never run
  anything.

### Known broken

- **ZX Spectrum**. It loads a tape, draws, and its video, memory and whole
  machine state are identical between flavours - but its **audio is not
  reproducible natively**: three runs of the same tape gave two different audio
  digests. It also corrupts the heap, which shows up as a crash in
  `ZXSpectrum::Tape::unload()` when the machine is torn down. Both point the
  same way - something in the tape path reads memory it does not own. A machine
  whose sound changes between runs cannot carry a movie, so this one is listed
  and disowned rather than counted.

### Different for a reason, and understood

- **Atari 5200**. Its video, memory and whole machine state are identical
  between the two flavours, and each flavour is perfectly deterministic - but
  the **audio differs between them**. The POKEY's DAC builds its 325-entry mix
  table with `exp()`, and musl's `exp` and glibc's do not round identically, so
  the table differs in its last bits and every sample after it does too.

  That is a libc difference, not a sandbox one, and it is worth stating plainly
  because it will recur: **any machine that builds a table with libm will do
  this**. What a movie depends on is the guest, which is self-consistent; what
  the gate would like is for the reference to agree, and it cannot while the two
  link different maths libraries. Making it agree means computing that table
  without libm, which is a patch nobody has written.

- **The Mega Drive's picture is 1280 across.** ares renders its VDP into a
  buffer four times as wide as the machine's 320, because a Mega Drive's
  shadow/highlight and its H32/H40 mixing happen between pixels; `setScale(0.25,
  ...)` is how ares' own frontend puts it back. Sampled over a frame of a real
  game, **98.3% of every group of four is identical and 1.7% is not** - so
  dividing by four in the core would be lossy, and it is handed over as ares
  drew it. The package declares 292x224 as the virtual size, which is what
  Chimera displays it at, so the picture is right; what it costs is a recording
  four times wider than the machine. Worth revisiting if anybody minds the file
  size more than the last 1.7%.

- **The Mega Drive's audio runs 0.18% short** of what its refresh rate asks for
  - 734.6 samples a frame against 735.95. Both its streams are short on their
  own (-0.07% and -0.18% measured separately), so it is ares' own thread timing
  and not the mixing above it; every other machine measured is within 0.06%, and
  most within 0.015%. Over an hour it is about six seconds of drift in a
  recording, which is worth knowing and has not been chased upstream.

- **The Nintendo 64 emits audio at half rate for its first sixty frames**, 371
  samples a frame instead of 735, until the game programs the AI. Chimera's A/V
  writer is audio-driven and drops a video frame when too little sound arrives
  with it, so a Super Mario 64 recording is **thirty frames shorter than the
  movie** and its frame numbers do not line up with the movie's until the game
  has booted. The recording itself is right - 210 video frames and 154374
  samples are both exactly 3.5 seconds - and this is what BizHawk does too. It
  is stated here because "the video is thirty frames short" looks like a fault
  and is not one.

- **The Nintendo 64 reports a flat 60Hz** where every other machine reports its
  own. ares hints 60 there itself, with `//TODO: More accurate refresh rate
  hint` beside it, so the core has nothing better to pass on. Its audio implies
  about 59.82Hz. 60 is also the figure mupen and BizHawk record N64 movies at,
  which is the same convention the analogue stick follows above, so the two
  agree by accident and there is nothing to fix until ares fixes it.

- **A Game Boy frame is sometimes two frames long.** When a game switches the
  LCD off, ares refreshes the screen late, and that frame carries twice the
  audio. Real, and both the picture and the sound are right; it simply means a
  frame is not a fixed length of machine time on any of these machines, which is
  why the audio is drained per frame rather than counted.

### Absent

Nothing is now absent for want of a BIOS - Sergio supplied the Atari 5200, Neo
Geo and Neo Geo Pocket ones and all four machines were added.

The **Saturn** is a stub upstream - one file - so it is not a machine anybody
could offer. The **Super Famicom** and **PC Engine** build but crash when asked
to power on with no cartridge, which is probably nothing more than that; they
are absent until somebody checks.

### What a sandbox had to be taught along the way

Three shims in `waterbox/guest-syscalls.cpp`, each found by a machine failing:

- `mkdir`, `rmdir`, `unlink`, `rename`, `chmod`, `symlink`, `readlink`,
  `getcwd` - a read-only, empty filesystem, which is what mia is looking at.
- **`access`**, which musl turns into a syscall the sandbox does not answer at
  all, so the guest trapped rather than hearing "no". It has to answer
  *truthfully*: a flat "nothing exists" satisfied mia hunting for its databases
  and then broke every machine that checks the cartridge it was just handed.
  What exists in a core is what the host mounted, so the shim opens the name and
  says whether that worked.

And two things mia itself had to be taught, both because a core has no disk:

- **its game databases travel with it** (`patches/ares/0013`). mia reads
  `Database/<machine>.bml` off a filesystem, and without it cannot answer
  questions a game file does not answer for itself: which of two wirings a 16KB
  Atari 5200 cartridge uses, what board a Famicom cartridge really has, or what
  a Neo Geo romset is at all. The five those machines need are compiled in;
  carrying all twelve would be weight for nothing.
- **an archive that cannot be mapped is read** (`patches/ares/0014`). nall opens
  a zip by memory-mapping it, and a sandbox has no address space to map a
  mounted file into - so a Neo Geo romset could not be opened at all. It falls
  back to reading the file, which costs its size in memory and works anywhere.

## Firmware

Four machines need a console BIOS - the Game Boy Advance, the ColecoVision, the
MSX and the PlayStation - and none of it is in this repository.

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

## In the frontend

The package is not finished when the gate is green - the gate loads the guest
directly, and Chimera loads it through a project, a core manager, an input
system and an encoder. So the whole of that was walked once, headless: a
hand-written `.chimeraProject` naming the package and a Game Boy ROM, opened
with `--headless --chromeless --dump-type ffmpeg`, and the result read back with
`ffprobe`.

It works, and it found one thing that no digest could have.

### The refresh rate was wrong on every machine but one

The core reported **60Hz** (or 50 in PAL), because that is what the two lines it
had said. No machine here actually runs at 60Hz. A Game Boy is
4194304/(456*154) = **59.7275Hz**; a Famicom is 60.0985; a Mega Drive is
59.9228. Half a percent - which is a video file whose audio drifts a second out
over half an hour, and a movie whose length in seconds is wrong by the same
amount.

ares already knows the true figure and offers it: every machine calls
`Platform::refreshRateHint` with a rate it computed from its own master clock.
The core now keeps that number, and `vsyncNumerator`/`vsyncDenominator` recover
the ratio it came from with a **continued fraction** rather than approximating
it - a rate that arrived as a double goes back out as 262144/4389, exactly the
integers the machine divided. A machine that never hints keeps the nominal rate
for its region, which is what the Nintendo 64 gets (ares hints a flat 60 there
itself).

| | reported before | reported now |
| --- | --- | --- |
| Game Boy, Game Boy Advance | 60/1 | 262144/4389 = 59.7275Hz |
| Famicom | 60/1 | 311851/5189 = 60.0985Hz |
| Mega Drive, Master System | 60/1 | 183843/3068 = 59.9228Hz |
| Nintendo 64, Atari 2600 | 60/1 | 60/1 |

The seeds of the continued fraction are worth one line, because getting them
wrong does not look like an error: `h(-1)=1, h(-2)=0` and `k(-1)=0, k(-2)=1`.
With `k(-1)` set to 1 instead the first version reported 0.98Hz, which is
nonsense the code was perfectly happy with.

### Four faults the Game Boy could not have found

A Game Boy has no controller port, one audio stream, stereo sound and a small
cartridge. It is the one machine in the core that misses every one of these, and
it was the machine the frontend had been walked with. A Famicom and a Mega Drive
found all four in an afternoon.

**Audio streams were concatenated rather than mixed.** A machine with more than
one voice - a Mega Drive has the YM2612 and the PSG - had each stream's samples
appended to the frame in turn, so a Mega Drive frame came out twice as long as
it should and the frontend played PSG followed by FM at double speed. 1470
samples a frame where the refresh rate wants 736. A sample is now taken from
every stream and summed, and only when all of them have one waiting, which is
ares' own rule in `desktop-ui/program/platform.cpp`. 735 a frame now.

**Monaural streams left the right channel uninitialised.** `Stream::read` writes
only as many channels as the stream has, and the second slot of the buffer was
never zeroed - so ten of the twenty-one machines put whatever the stack held
into the right speaker. It is the mono signal in both channels now; an NES
encode through Chimera has left byte-identical to right.

**Every machine with a controller port refused to load.** The port settings were
the Nintendo 64's, spelt its way - `gamepad`, `mouse`, `controllerPak` - while
ares knows a Mega Drive's device as "Control Pad", a Neo Geo's as "Arcade
Stick", an Atari 5200's as "Controller", and `Port::allocate` is exact. That
did not show up until now because nothing had ever *sent* a port setting: the
gate writes `{"machine": ...}` and nothing else, so the core took its "nobody
said" path. A project is not like that - Chimera gives every declared setting a
value - so every ported machine got `gamepad` and answered *this machine has no
such device for that port*.

The id is matched to the machine's own name on letters alone now (case ignored,
spaces dropped), and the ports take `default`, which is what the package ships
and the only answer that means the same thing on all twenty-one.

**The wizard would offer no cartridge but a Nintendo 64's.**
`file_slots.json` still declared `.n64/.v64/.z64` alone. Chimera narrows a slot
by the chosen machine's own extensions, so what belongs in the slot is the union
of all of them - which is generated from the machines now rather than typed.

### What let them hide, and what was done about it

The gate could not have caught any of these, and one thing made it worse: **the
gate's runner sandboxed the guest in a different arena from the one the package
asks for** - 384 MB of mmap where `waterbox.config` says 256 - under a comment
claiming the two matched. So `waterbox/memory-layout.h` is generated from the
package and included by `run-wbx.cpp`, and `gen-config.py --check` fails if
anybody edits one without the other. `file_slots.json`'s formats are generated
the same way.

sbrk went from 16 MB to 32 MB while this was being looked at. The Famicom wants
more than 16 while mia works out what board a cartridge has; musl asked, miniBox
said no, and musl went to mmap and carried on - so it cost nothing but a page of
`sbrk heap exhausted` on stderr, and stayed invisible until a machine other than
the Game Boy went through the frontend. Raising it is free: a savestate holds
the pages that were touched, not the ones that were offered.

## Not done

In rough order of what would matter first:

- **Speed on the Nintendo 64** - measured, and it is the real problem. See
  below.
- **Two machines that have never run anything** - the MSX and the MyVision,
  for want of a ROM. Everything else has booted something; fifteen have booted a
  commercial game.
- **A commercial game in the gate.** Fifteen machines were compared against real
  cartridges by hand, native against sandbox, and agreed - but none of that is
  in `run-gate.sh`, because the ROMs cannot be committed. The gate's own legs
  are homebrew. What that leaves untested on a schedule: save chips, the 64DD,
  the Transfer Pak and the Expansion Pak.
- **`n64-systemtest`** (MIT) is the accuracy suite ares itself is tested against
  and would be a far stronger gate than three homebrew ROMs. It needs a Rust
  MIPS toolchain to build, which is why it is not here yet.
- **Save data beyond the Nintendo 64.** Every other machine's saves live inside
  the savestate and cannot be exported, because mia writes them through a host
  path this core does not give it.
- **Publishing**: no `chimera.yml`, no row in Chimera's `official-cores.json`,
  no release, and the repository is not pushed anywhere. The package builds,
  passes Chimera's contract tests and has been run end to end by the frontend -
  but publishing it is Sergio's to authorise and has not been.
- **Optional tooling**: no registers, no trace, no core-rendered surfaces.
  Memory domains, buses and the Nintendo 64's save-data export are done.
- **A gate leg that goes through Chimera.** Everything in "In the frontend"
  above was found by hand, one machine at a time, and none of it is on a
  schedule. A leg that opens a project headless and reads back what came out
  would have caught all four faults on the day they were written.

## Speed

Measured on this machine (20 cores, but the emulation is one thread), 600 frames
per figure, native and sandboxed:

| machine | native | sandbox | against real time |
| --- | --- | --- | --- |
| Nintendo 64 | 59 fps | 69 fps | **about 1x** |
| Game Boy | 318 fps | 320 fps | 5.3x |
| PlayStation | 276 fps | 275 fps | 4.6x |
| Game Boy Advance | 228 fps | 221 fps | 3.8x |

Three of the four have room to work in. **The Nintendo 64 has none**: it runs at
roughly real time on a homebrew ROM that barely draws, which means a real game
will not reach it, and a tool-assisted run wants to go much faster than real
time, not slower.

Two things are worth knowing about that number. The sandbox is not the problem -
it is as fast as the reference, and on the N64 slightly faster. And **the
renderer is not the problem either**: running with drawing off is not faster
(53 fps against 59), so the cost is the CPU and RSP interpreters and the
machine's own stepping, not angrylion.

So the answer, when somebody gets to it, is the recompilers. miniBox does allow
executable pages, and BizHawk ships a recompiled ares64, so it is possible;
what it costs is the argument in "The recompilers" above, and a way to prove a
generated-code core replays identically.

### The SSE4.1 question, measured and deliberately declined

ares has two implementations of the Nintendo 64's RSP vector unit: a SIMD one
that needs SSE4.1, and a scalar fallback. Without `-msse4.1` the fallback is
what compiles - which is neither what ares normally runs nor what BizHawk's port
runs, and PCSX2's core already asks SSE4.1 of a machine, so the floor would not
be new to Chimera.

It was tried. It bought **no speed at all** on the content here, and it
**changed the audio** - video and memory identical, audio and machine state
different. So the two implementations do not agree, ares treats the scalar one
as the accurate path (its Reference profile forces it), and there was nothing to
buy with that difference. The flag is off, with a comment in `meson.build` saying
why. Somebody measuring a game that actually leans on the RSP should revisit it.

## Numbers, as of the twenty-one-machine build

- `core.wbx` is 20.2 MB with every machine in it - it was 9.3 MB with only the
  Nintendo 64, so twenty more machines cost about eleven megabytes between them. The
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
