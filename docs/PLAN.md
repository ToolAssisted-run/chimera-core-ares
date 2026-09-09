# ares as a Chimera core

Why this port is shaped the way it is, what is done, and what is not.

The companion reading is Chimera's `docs/porting-a-core.md` (the order the work
goes in) and `docs/design-principles.md` (why Chimera wants what it wants).

## What ares is, and why it is worth the trouble

ares is one codebase emulating about thirty machines. Chimera's other fourteen
cores are each one emulator for one family; ares is the first where the
machinery, once in, is shared. The entry point is the **Nintendo 64**, which
Chimera has never had - and the reason to start there is that N64 is the
*hardest* of ares' systems to sandbox, so a port that works for it works for
the rest.

The ambition is therefore explicit: this repository is `chimera-core-ares`, not
`chimera-core-ares64`. The N64 is machine number one.

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

Eight patches, all small, and five of them are things that are simply *wrong*
upstream in a build without Vulkan:

| patch | what |
| --- | --- |
| `ares/0001` | `System::run` calls `vulkan.load` outside the `#if defined(VULKAN)` guard, so a software build does not compile. |
| `ares/0002` | the interpreters become a build option. |
| `ares/0003` | the video interface stopped tracking its dither-filter bit (VI_CONTROL bit 16) when ares handed the VI to paraLLEl-RDP. angrylion needs it. |
| `ares/0004` | the angrylion graft. |
| `ares/0005` | the RTC's clock becomes a seam. |
| `ares/0006` | **RDRAM's hidden bits.** The ninth bit of each RDRAM byte is allocated by the Vulkan path and nowhere else, so in a software build every RDRAM write dereferenced a null pointer. The machine now owns it, which also puts it in savestates where it belongs. |
| `ares/0007` | `nall::Path::user()` dereferences `getpwuid()` without checking it. There is no passwd database in a sandbox. nall's own `inode.hpp` null-checks the identical call, so this is an oversight rather than a design. |
| `angrylion-rdp/0001-2` | the rasteriser includes the header it uses, and its output buffer becomes part of its interface rather than something callers declare `extern` for themselves. |

Patches 0001, 0006 and 0007 are all bugs in ares that have nothing to do with
Chimera, and all three are worth offering upstream.

## The shape of the code

    waterbox/machine.{h,cpp}   the emulated machine, and nothing about how it is driven
    waterbox/waterbox.cpp      the guest ABI over it - the core the frontend loads
    waterbox/run-native.cpp    the same machine, outside the sandbox
    waterbox/gate-harness.h    the schedule, shared by both drivers
    waterbox/run-wbx.c         the sandbox driver the gate uses

The point of `machine.{h,cpp}` is that "native and sandbox agree" is a claim
about the **sandbox**, not about two hand-written adapters that happen to look
alike. The same file is compiled into both, and `gate-harness.h` means both are
driven by one loop, one option parser and one digest.

### mia, and why the other systems are cheap

ares' media layer, `mia`, is what turns a file into the "pak" a machine loads:
it reads the cartridge, works out its region, its CIC variant (by computing the
IPL2 checksum over the boot code - real detection, not a table of CRCs) and
which of the three kinds of save chip it has. It is compiled here with
`MIA_LIBRARY`, which is upstream's own switch for using it without its GUI.

Using mia rather than a hand-rolled loader is the decision that makes the other
systems a build-list change rather than a port. It is also why this core needs
**no firmware**: mia carries the N64's PIF and CIC boot ROMs as compiled-in
resources, about four kilobytes, exactly as ares distributes them.

That last point deserves saying plainly, because it is inherited rather than
chosen: those ROMs are Nintendo's, and this package contains them because ares'
does. The alternative is to drop them and let ares emulate the boot at a high
level instead (`pif/hle.cpp` - it is what runs when the pak has no PIF ROM),
which would be less accurate and would need no such argument. That is Sergio's
call to make, and nothing in the code depends on the answer.

## The gate

`waterbox/run-gate.sh`, twelve legs, about a minute. It runs anywhere, CI
included, because the content is **PeterLemon/N64** - N64 test ROMs under the
Unlicense, vendored in `tests/content/`. This core is in the "upstream ships
something free" category of `porting-a-core.md`.

What the legs prove, in the order they matter:

1. **native == sandbox**, on every digest (video, audio, every memory domain),
   over four runs including one with a button held and the stick over.
2. **the machine survives a savestate**, saved and reloaded before every single
   frame, digests unchanged.
3. **the same run twice is the same machine** - the entropy leg above.
4. **input reaches the machine**: idle, A and Start each produce a different
   machine, and so do three different stick positions. Without this a leg could
   pass with the input wire cut.
5. **the picture is the one the hardware draws**: PeterLemon ships a reference
   PNG per ROM, and `helloworld-cpu` is compared **pixel for pixel** - all
   76800 of them, exactly - with the video interface's filtering off.

That last one is worth more than it looks: it is an end-to-end check against
real hardware, not against ourselves.

### Two things the gate deliberately does not claim

- **The RDP picture is not compared to its reference PNG.** `helloworld-rdp`
  renders the right image - same layout, same elements - but its colours differ
  from PeterLemon's capture by a few levels (a yellow of `255,231,0` where we
  produce `255,255,0`; black where we produce `8,8,0`). The framebuffer is
  32bpp, so this is not a dither or a 5-to-8-bit expansion. Either the reference
  was not captured from hardware for this program, or angrylion and the console
  disagree about the colour combiner. **Unresolved.** The honest reference for
  the RDP is ares-with-paraLLEl-RDP or `n64-systemtest`, and neither is wired up
  yet.

- **`input-cpu`'s picture never changes.** Its memory does - which is what the
  input legs use - but no button or stick position alters a pixel, while our
  idle picture matches its hardware reference to within one pixel. Most likely a
  property of that program; recorded here rather than explained.

## The analogue stick

Worth its own section, because it is a decision and not just a bug.

Modern ares takes stick input in the **±32767** range of a PC gamepad and puts
it through a deadzone, a response curve and an octagonal gate before the machine
sees it. Passing the console's own -127..127 byte therefore lands *inside the
deadzone* and the stick does nothing whatsoever - which is how this was found,
by an input leg that watched three stick positions produce one machine.

`machine.cpp` now scales -127..127 up to ares' range, so the stick works and the
gate is happy. But note what that means: **the value the game reads is shaped,
not the byte the author typed.** The mupen and BizHawk convention for N64 TASing
is the opposite - the author sets the byte the controller reports, exactly, and
the emulator does not interpret it.

Making that exact needs a patch to ares' `Gamepad::read` to bypass its own
shaping. It is not done, because which behaviour is *right* is a question about
what N64 movies should mean, and that is Sergio's to answer. Until then a movie
recorded here records a stick position, and replays identically - it is just not
byte-comparable with a mupen movie.

## Not done

In rough order of what would matter first:

- **Speed.** Nothing is measured. The CPU and RSP interpreters plus a software
  RDP is the slow end of every choice available, and whether a real game runs at
  a workable rate is unknown. If it does not, the questions are (in order) the
  RSP recompiler, `Accuracy::RSP::SIMD`, and whether miniBox's executable pages
  can host sljit safely.
- **A real game.** Every test here is a homebrew ROM of a few kilobytes. Nothing
  has booted a commercial cartridge, so nothing is known about save chips in
  practice, the 64DD, the Transfer Pak, or the Expansion Pak.
- **`n64-systemtest`** (MIT) is the accuracy suite ares itself is tested against
  and would be a far stronger gate than three homebrew ROMs. It needs a Rust MIPS
  toolchain to build, which is why it is not here yet.
- **The other systems.** None are wired up. The work per system is a build-list
  entry, a `machines[]` block in `waterbox.config` keyed off a machine setting
  (gpgx is the precedent - one `core.wbx`, four machines), a controller
  declaration and a gate leg. The machinery they would share is all in place.
- **Publishing**: no `chimera.yml`, no row in Chimera's `official-cores.json`,
  no release. The package builds and passes Chimera's contract tests, but
  nothing publishes it yet.
- **Optional tooling**: no registers, no trace, no core-rendered surfaces. Memory
  domains and save-data export are done.

## Numbers, as of the first working build

- `core.wbx` is 9.3 MB; the package is built reproducibly (the build script
  packs twice and compares).
- The declared arena is 296 MB, and **mmap is the part that matters**: a 1 MB
  cartridge already needs more than 80 MB there, because the ROM is held three
  times while loading - mia's vector, the pak's copy, and ares' own
  `Memory::Writable`. 256 MB leaves room for the 64 MB carts. Collapsing those
  three copies is the obvious saving and has not been attempted.
- The boot ROM takes about **90 frames** to hand over to the cartridge. Any test
  that runs fewer than that is testing the PIF, not the game - which cost an
  hour the first time, when a 30-frame run drew a black screen and looked like a
  broken renderer.
