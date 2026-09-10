# chimera-core-ares

**ares as a Chimera waterbox core** - the [ares](https://github.com/ares-emulator/ares)
multi-system emulator compiled into
[miniBox](https://github.com/ToolAssisted-run/chimera-common-minibox)'s
deterministic sandbox and packaged as a Chimera core (`core.wbx` +
`waterbox.config`), the same shape as
[chimera-core-dosbox-x](https://github.com/ToolAssisted-run/chimera-core-dosbox-x)
and [chimera-core-gpgx](https://github.com/ToolAssisted-run/chimera-core-gpgx).

Status: **twenty-one machines declared; five proven by the gate, fifteen proven
against real commercial games, and four run end to end inside Chimera.** It
carries no firmware but ares' own: twelve of the twenty-one ask the user for a
console BIOS, and nine need nothing but a cartridge. It
runs on Windows as well as Linux, but only with **miniBox ab677a2 or newer**:
libco takes its coroutine stacks from `mmap(MAP_STACK)` rather than from malloc
so that the sandbox knows what they are, and an older miniBox either kills this
core on its first frame or quietly loses what those stacks write. `docs/PLAN.md`
has the whole story.

ares emulates about thirty systems from one codebase, which is why this
repository is named for the emulator rather than for a console. The Nintendo 64
came first because it is the hardest of them to sandbox - a Vulkan renderer, two
dynamic recompilers and a host clock - so the machinery that makes it work is
the machinery the rest need.

| | |
| --- | --- |
| **Proven by the gate** (every digest compared) | Nintendo 64, Game Boy, Game Boy Advance, PlayStation, MSX |
| **Proven against a real game** (native == sandbox on a commercial cartridge, off the record - the ROM is not ours to ship) | + Famicom/NES, Game Boy Color, Mega Drive, Master System, Game Gear, SG-1000, Atari 2600, WonderSwan Color, ColecoVision, Neo Geo AES, Neo Geo Pocket Color |
| **Declared, untrusted** | MyVision - no ROM for it exists to hand |
| **Understood difference** | Atari 5200 - identical machine, but its audio differs between the flavours because its DAC table is built with `exp()` and musl and glibc round differently |
| **Known broken** | ZX Spectrum - its audio is not reproducible even natively, and it corrupts the heap |

Fifteen machines have run a real commercial game here with the reference and the
sandbox agreeing byte for byte - Super Mario 64 among them. The gate cannot say
so, because those ROMs may not be redistributed; `docs/PLAN.md` writes it down
with the speeds instead.

Four of them - Game Boy, Famicom, Mega Drive and Nintendo 64 - have also been
opened as a Chimera project and recorded to a video file, which is a different
test from the gate and found four faults the gate structurally could not: audio
streams concatenated instead of mixed, a monaural machine's right channel left
uninitialised, every machine with a controller port refusing to load, and a
wizard that would offer no cartridge but a Nintendo 64's. That pass is a script
now - `waterbox/run-frontend-gate.sh` opens a project headless, records it and
reads the file back with ffprobe - and `docs/PLAN.md` has the details under
"In the frontend".

That table is the honest shape of it, and `docs/PLAN.md` keeps it current.
Adding a machine is a row in `waterbox/machines.h` and a regenerate; making one
*believable* is a free ROM in `tests/content/` and a gate leg.

What the machines have in common:

- **Everything drawn on the CPU.** ares renders the N64 through paraLLEl-RDP on
  Vulkan; this core uses **angrylion's software rasteriser** instead, so the
  picture is decided entirely by code Chimera compiles and is identical on every
  machine. No GPU, no driver, no graphics context in a savestate.
- **Interpreters, not recompilers**, for the N64's CPU and RSP - the same choice
  for the same reason.
- **Nothing depends on the host.** Not the clock a cartridge's RTC starts from,
  not the entropy a console's uninitialised memory is filled with, and not a
  second thread to draw on. All three were real, and all three are patched.
- **The controller wire format is derived from ares**, not typed: `gen-machines`
  asks the emulator what each machine is made of and `gen-config.py` writes the
  declaration, so a newer ares that renames a button fails the gate rather than
  renumbering somebody's movie.
- **This core ships no firmware but ares' own.** ares compiles console boot ROMs
  in as resources - Nintendo's, Sega's, Bandai's, Amstrad's - and a package built
  from it carried all of them. It carries none of them now (`patches/ares/0020`
  and the five after it): nine of the twenty-one machines need nothing but a
  cartridge, and the other twelve ask for their console BIOS, which the user
  supplies and Chimera pins by hash. ares' own material - the game databases it
  wrote - stays. Checked by searching the built `core.wbx` for every firmware
  file ares ships: not one twenty-four byte run of any of them survives.
- **A Nintendo 64 boots without Nintendo's boot ROM.** ares bundles the PIF and
  CIC ROMs; this core does not carry them, and starts the cartridge's own boot
  code the way they would have (`patches/ares/0018`). Every register it hands
  over was measured off a real boot rather than copied from a table, and a gate
  leg boots the same cartridge both ways and names every difference that remains.
  A run recorded here therefore starts at the cartridge, ninety frames earlier
  than one recorded against a real boot ROM, and the two are not interchangeable.
- **The refresh rate is the machine's own**, not a nominal 60: a Game Boy
  reports 262144/4389 = 59.7275Hz, taken from ares' own hint and turned back
  into the exact ratio it came from.
- **The analogue stick is the byte** the controller reports, reaching the game
  unchanged - the convention mupen and BizHawk record, so an N64 run made here
  means the same as a run made there.

The equivalence gate (`waterbox/run-gate.sh`) runs thirty-five legs: native
against sandbox on every digest for all four proven machines, the machine
round-tripped through a savestate before every frame, every one of the
twenty-one rebuilt and re-enumerated against its committed declaration, **each
machine's refresh rate checked against its own clock**, input proven to
reach the machine, the picture compared **pixel for pixel against real
hardware**, **the HLE boot compared against a real one**, and **jsmolka's ARM
test suite read off the screen**. Its content is
[PeterLemon/N64](https://github.com/PeterLemon/N64) (public domain),
[libbet](https://github.com/pinobatch/libbet) (Zlib) and
[gba-tests](https://github.com/jsmolka/gba-tests) (MIT), so it runs on a public
runner with nothing licensed on it - it simply skips the machines whose BIOS is
not there, and says so. That is fourteen of the thirty-five legs, and CI runs
exactly those (`.github/workflows/chimera.yml`); the rest need a BIOS in
`tests/firmware/` and are run by hand.

## Building

Both builds are meson. The native reference:

```
meson setup build/meson-native -Dwaterbox=true -Dminibox_dir=<miniBox checkout>
ninja -C build/meson-native            # run-native + run-wbx
```

The guest needs a miniBox checkout built with its C++ guest toolchain
(`meson setup build/meson-cpp <miniBox> -Dguest_cpp=true`):

```
./waterbox/setup-guest.sh [-m <miniBox>] # generates the cross file
ninja -C build/meson-guest               # core.wbx
./waterbox/build-package.sh -r <chimera> # -> <chimera>/build/Cores/ares.chimeraCore
./waterbox/run-gate.sh                   # the equivalence gate
```

Adding a machine is a row in `waterbox/machines.h`, then

```
build/meson-native/waterbox/gen-machines > waterbox/machines.json
./waterbox/gen-config.py
```

which asks ares what the machine is made of and writes the guest's binding
table, the package's `machines[]` and the default keybindings from it.

The changes this core needs live in `patches/`, one directory per submodule,
applied to the pristine pins by `waterbox/apply-patches.sh` (idempotent, and run
automatically at configure time). There are twenty-one, all small; six of them are
plain bugs in ares that only show up in a build like this one - without Vulkan,
with more than one machine, or inside a sandbox - and are worth offering
upstream. `docs/PLAN.md` explains every one.

## Credits & provenance

All emulation comes from **ares**, by Near, Themaister and contributors, ISC
licensed, vendored unmodified as the submodule [`extern/ares`](extern/ares).
The Reality Display Processor is **angrylion's**, descended from MESS, vendored
as [`extern/angrylion-rdp`](extern/angrylion-rdp) from the branch
[TASEmulators maintains](https://github.com/TASEmulators/angrylion-rdp) for
BizHawk's own ares port. The integration layer is this repository's own work
under the **MIT License**.

**Built artifacts are non-commercial.** angrylion's RDP carries the MAME
licence, which permits redistribution but forbids sale and commercial use, and
the built core inherits that - the same position as the gpgx, opera and snes9x
packages. See [`LICENSE`](LICENSE) and
[`waterbox/package-licenses.json`](waterbox/package-licenses.json).

The prior art this port draws on: **BizHawk's Ares64**
(TASEmulators/BizHawk `waterbox/ares64`), which established that ares' N64 fits
in a waterbox at all and that angrylion is the way to draw it. This is a
re-implementation against a 2026 ares rather than a port of that code;
`docs/PLAN.md` says where the two diverge.
