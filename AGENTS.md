# AGENTS.md - ares core for Chimera

This repository builds the ares multi-system emulator as a core for Chimera
(https://github.com/ToolAssisted-run/chimera), a frontend for tool-assisted
speedruns. It produces one file, `ares.chimeraCore`: the emulator as a
sandboxed guest (`core.wbx`) plus the declarations Chimera reads. One
package holds every machine (twenty-nine); the `machine` setting picks one.
Upstream ares and angrylion's rasteriser are submodules; everything else
here is the machine layer, the generators, the build and the gates.

`<chimera>` is a Chimera checkout and `<miniBox>` is its submodule
`<chimera>/extern/chimera-common-minibox`.

## Layout

- `extern/ares/`, `extern/angrylion-rdp/` - upstream, pinned submodules.
- `patches/<submodule>/` - the patch series, one directory per submodule.
- `meson.build`, `waterbox/meson.build`, `gen/` - both builds. A cross
  build is the guest; anything else is the native reference.
- `waterbox/machines.h` - which machines the core offers and how to build
  each. The one table a person edits to add a machine.
- `waterbox/machine.{h,cpp}` - the emulated machine, in both builds.
  `waterbox/waterbox.cpp` - the guest ABI over it.
- `waterbox/run-native.cpp`, `run-wbx.cpp`, `gate-harness.h` - the two gate
  drivers and the schedule they share.
- `waterbox/gen-machines.cpp` - asks ares what each machine is made of; its
  output is `waterbox/machines.json`.
- `waterbox/gen-config.py` - from `machines.json`, WRITES `machines.inc`,
  `memory-layout.h`, `default_keybinds.json`, the slot formats in
  `file_slots.json`, and the machines, firmware and per-machine settings in
  `waterbox.config`.
- `waterbox/package-licenses.json` - the licences that travel in the package.
- `waterbox/setup-guest.sh`, `build-package.sh`, `apply-patches.sh` - build.
  `waterbox/run-gate.sh`, `run-frontend-gate.sh`, `waterbox/tests/` - gates.
- `tests/content/` - free test ROMs, committed. `tests/firmware/` (your
  console BIOSes) and `tests/local/` (your own games): gitignored.
- `docs/PLAN.md` - every decision and patch. `docs/TASKS.md` - what is left.

## Set up the build environment

    sudo apt-get update
    sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3

    git submodule update --init
    git clone https://github.com/ToolAssisted-run/chimera.git <chimera>
    git -C <chimera> submodule update --init extern/chimera-common-minibox

    mb=<miniBox>
    [ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
    meson compile -C "$mb/build/meson-linux"
    [ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
    meson compile -C "$mb/build/meson-cpp"

The `meson-cpp` build downloads the GCC source matching the host `gcc`,
once. If a Chimera checkout already exists, use it and skip the clone.

## Build

    meson setup build/meson-native -Dwaterbox=true -Dminibox_dir=<miniBox>
    ninja -C build/meson-native
    ./waterbox/build-package.sh -m <miniBox> -r <chimera>

The first two lines build `run-native`, `run-wbx` and `gen-machines` in
`build/meson-native/waterbox`. Always pass `-Dminibox_dir`: without it
`run-wbx` may be left out with no error. The third line configures and
builds the guest (`build/meson-guest/waterbox/core.wbx`), checks it with
miniBox's `check-wbx.sh` and writes `<chimera>/build/Cores/ares.chimeraCore`.
The guest alone: `./waterbox/setup-guest.sh -m <miniBox>`, then
`ninja -C build/meson-guest`.

A hand-built package stamps `<commit>+local` (`-dirty` with changes in the
tree). It is for testing. CI stamps the commit through `CORE_VERSION`.

## Install the core into Chimera

Chimera ships no cores and downloads none. `build-package.sh -r <chimera>`
writes the package into `<chimera>/build/Cores/`, which is the cores folder
of a source checkout. For a release bundle, copy `ares.chimeraCore` into
the `Cores` folder beside `Chimera.exe` (or the folder set in File > Core
Manager > Change folder...). File > Core Manager lists the folder; Refresh
List rescans it. The same file works on Linux and on Windows.

## Test before you commit

    MINIBOX_DIR=<miniBox> ./waterbox/run-gate.sh

It must end with `0 failed`. Read the `SKIP` lines too. With nothing in
`tests/firmware/` and `tests/local/` only the Nintendo 64 legs, the
declaration check and `check-wbx` run; that is all a fresh clone, and so a
public runner, can run. A change to any other machine is proved only with
that machine's BIOS in `tests/firmware/` and, for most, a game in
`tests/local/<MACHINE>/`. Say in the commit which legs ran.

The package inside a built Chimera:

    CHIMERA_BUILD=<chimera>/build ./waterbox/run-frontend-gate.sh

If it prints `SKIP everything: no ...` it tested nothing and still exits 0.
`docs/BUILDING.md` lists what it needs, and gives the command for Chimera's
contract tests, which CI also runs on the package.

## Rules of this repository

- **Upstream is not edited in place.** A change to `extern/ares` or
  `extern/angrylion-rdp` is a numbered patch in `patches/<submodule>/`.
  `meson.build` runs `waterbox/apply-patches.sh` on every configure. Never
  commit inside a submodule. `git status` shows both modified once the
  patches are applied; that is expected.
- **A patch warning is an error.** The script applies each patch that
  applies, skips each that is already there, and only WARNS about one that
  is neither. The build then goes on without it. Read Chimera's
  `docs/porting-a-core.md` before regenerating a patch in mid-series.
- **The declarations are generated.** Never hand-edit `machines.json`,
  `machines.inc` or `memory-layout.h`, nor the generated parts of
  `waterbox.config`, `file_slots.json` and `default_keybinds.json`. The
  order of a controller's buttons is what a movie is written in; it comes
  from ares, and the gate fails when the committed files have moved.
- **Regenerate only with every BIOS present.** To add a machine: a row in
  `waterbox/machines.h`, then
  `build/meson-native/waterbox/gen-machines tests/firmware > waterbox/machines.json`
  and `./waterbox/gen-config.py`. `gen-machines` writes a machine whose BIOS
  it was not given as unbuilt, and `gen-config.py` leaves an unbuilt machine
  out of the package. Without the full `tests/firmware/`, do not regenerate.
- **Determinism is the product.** The guest must not read host time, host
  randomness or anything else that differs between runs; a savestate must
  round-trip. Here that means interpreters and no recompiler, the software
  rasteriser, one thread, a clock that is supplied and a pinned power-on
  seed. `core_defines` in `meson.build` must stay the same for both builds,
  and `-msse4.1` stays off (it changes the Nintendo 64's audio). The gate
  checks it; a change that breaks it is a bug.
- **Run the gate before committing.** A new leg needs a negative control:
  show that it fails when the thing it checks is broken, and say so.
- **Never commit game files, BIOS or firmware.** The package carries no
  firmware but ares' own. `tests/content/` holds only freely distributable
  ROMs. Never add network access.
- **Scripts stay executable** (git mode 100755): every `.sh`, and the
  `.py` files in `waterbox/` and `waterbox/tests/`.
- **Documentation prose is plain ASCII.**
- **Commit messages.** The subject is `type(scope): ` plus a sentence that
  states what is now true ("fix(ports): an MSX's keyboard is part of the
  machine, not Controller Port 1"); some subjects are the sentence alone.
  The body gives the cause, the change, the gate count and any new leg. A
  fix for a reported problem cites `chimera#N`: issues are filed in the
  Chimera repository.
- **Do not edit `.github/workflows`** unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - every build step, option, gate leg and error message.
- `docs/PLAN.md` - why each patch exists, what is proven, what is not.
- `.github/workflows/chimera.yml` - the recipe CI runs; it is authoritative.
- `tests/content/README.md` - what the gate's free content is.
- In Chimera: `docs/porting-a-core.md`, `docs/gates.md`,
  `docs/core-manager.md`.
