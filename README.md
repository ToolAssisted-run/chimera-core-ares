# chimera-core-ares

**ares as a Chimera waterbox core** - the [ares](https://github.com/ares-emulator/ares)
multi-system emulator compiled into
[miniBox](https://github.com/ToolAssisted-run/chimera-common-minibox)'s
deterministic sandbox and packaged as a Chimera core (`core.wbx` +
`waterbox.config`), the same shape as
[chimera-core-dosbox-x](https://github.com/ToolAssisted-run/chimera-core-dosbox-x)
and [chimera-core-gpgx](https://github.com/ToolAssisted-run/chimera-core-gpgx).

Status: **the Nintendo 64 works; it is the only machine wired up so far.**

ares emulates about thirty systems from one codebase, which is why this
repository is named for the emulator rather than for a console. The N64 came
first because it is the hardest of them to sandbox - a Vulkan renderer, two
dynamic recompilers and a host clock - so the machinery that makes it work is
the machinery the rest need. Adding another ares system is now a build-list
entry and a declaration, not a port.

What the Nintendo 64 machine has:

- **Everything drawn on the CPU.** ares renders the N64 through paraLLEl-RDP on
  Vulkan; this core uses **angrylion's software rasteriser** instead, so the
  picture is decided entirely by code Chimera compiles and is identical on every
  machine. No GPU, no driver, no graphics context in a savestate.
- **Interpreters, not recompilers**, for both the CPU and the RSP - the same
  choice for the same reason.
- **No firmware to find.** The console's boot ROMs travel with the core, as they
  do with ares itself, so a project is a cartridge and nothing else.
- **Four controller ports**, each taking a pad (optionally with a Controller Pak
  or a Rumble Pak) or a mouse. The analogue stick is the signed byte the
  controller reports, reaching the game unchanged - the convention mupen and
  BizHawk record, so a run made here means the same as a run made there.
- **Save chips**: EEPROM, SRAM and Flash, detected from the cartridge, exported
  and reloaded through Chimera's save-data channel.
- **A pinned clock and a pinned power-on seed**, because a movie replayed next
  year has to build the same machine as today's.

The equivalence gate (`waterbox/run-gate.sh`) runs twelve legs in about a
minute: native against sandbox on every digest, the machine round-tripped
through a savestate before every frame, input proven to reach the machine, and
the picture compared **pixel for pixel against real hardware**. Its content is
[PeterLemon/N64](https://github.com/PeterLemon/N64) test ROMs, which are public
domain, so the whole gate runs on a public runner with nothing licensed on it.

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

The changes this core needs live in `patches/`, one directory per submodule,
applied to the pristine pins by `waterbox/apply-patches.sh` (idempotent, and run
automatically at configure time). There are eight, all small; three of them are
plain bugs in ares that show up only in a build without Vulkan, and are worth
offering upstream. `docs/PLAN.md` explains every one.

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
