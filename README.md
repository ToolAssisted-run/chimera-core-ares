# chimera-core-ares

**ares as a Chimera waterbox core** - the [ares](https://github.com/ares-emulator/ares)
multi-system emulator compiled into
[miniBox](https://github.com/ToolAssisted-run/chimera-common-minibox)'s
deterministic sandbox and packaged as a Chimera core (`core.wbx` +
`waterbox.config`), the same shape as
[chimera-core-dosbox-x](https://github.com/ToolAssisted-run/chimera-core-dosbox-x)
and [chimera-core-gpgx](https://github.com/ToolAssisted-run/chimera-core-gpgx).

Status: **twenty-one machines declared; four proven by the gate and fifteen
proven against real commercial games.**

ares emulates about thirty systems from one codebase, which is why this
repository is named for the emulator rather than for a console. The Nintendo 64
came first because it is the hardest of them to sandbox - a Vulkan renderer, two
dynamic recompilers and a host clock - so the machinery that makes it work is
the machinery the rest need.

| | |
| --- | --- |
| **Proven by the gate** (free content, every digest compared) | Nintendo 64, Game Boy, Game Boy Advance, PlayStation |
| **Proven against a real game** (native == sandbox on a commercial cartridge, off the record - the ROM is not ours to ship) | + Famicom/NES, Game Boy Color, Mega Drive, Master System, Game Gear, SG-1000, Atari 2600, WonderSwan Color, ColecoVision, Neo Geo AES, Neo Geo Pocket Color |
| **Declared, untrusted** | MSX and MyVision - no ROM for either exists to hand |
| **Understood difference** | Atari 5200 - identical machine, but its audio differs between the flavours because its DAC table is built with `exp()` and musl and glibc round differently |
| **Known broken** | ZX Spectrum - its audio is not reproducible even natively, and it corrupts the heap |

Fifteen machines have run a real commercial game here with the reference and the
sandbox agreeing byte for byte - Super Mario 64 among them. The gate cannot say
so, because those ROMs may not be redistributed; `docs/PLAN.md` writes it down
with the speeds instead.

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
- **Firmware only where the console truly needs it.** Fourteen of the twenty-one
  need nothing but a cartridge; the Game Boy Advance, ColecoVision, MSX,
  PlayStation, Atari 5200, Neo Geo and Neo Geo Pocket Color need their console
  BIOS, which the user supplies and Chimera pins by hash. None of it ships here.
- **The refresh rate is the machine's own**, not a nominal 60: a Game Boy
  reports 262144/4389 = 59.7275Hz, taken from ares' own hint and turned back
  into the exact ratio it came from.
- **The analogue stick is the byte** the controller reports, reaching the game
  unchanged - the convention mupen and BizHawk record, so an N64 run made here
  means the same as a run made there.

The equivalence gate (`waterbox/run-gate.sh`) runs twenty-six legs: native
against sandbox on every digest for all four proven machines, the machine
round-tripped through a savestate before every frame, every one of the
twenty-one rebuilt and re-enumerated against its committed declaration, input proven to
reach the machine, the picture compared **pixel for pixel against real
hardware**, and **jsmolka's ARM test suite read off the screen**. Its content is
[PeterLemon/N64](https://github.com/PeterLemon/N64) (public domain),
[libbet](https://github.com/pinobatch/libbet) (Zlib) and
[gba-tests](https://github.com/jsmolka/gba-tests) (MIT), so the whole gate runs
on a public runner with nothing licensed on it - it simply skips the seven
machines whose BIOS is not there, and says so.

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
automatically at configure time). There are fourteen, all small; six of them are
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
