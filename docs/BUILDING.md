# Building the ares core

This builds one file, `ares.chimeraCore`: the ares multi-system emulator as
a sandboxed guest (`core.wbx`) with its declarations, which Chimera loads.
One package holds every machine this core offers (twenty-nine in
`waterbox/waterbox.config`). The steps are the ones
`.github/workflows/chimera.yml` runs from a fresh clone on a public Ubuntu
runner. That workflow is the reference: when this page and the workflow
disagree, the workflow is right.

`<chimera>` below is a checkout of
https://github.com/ToolAssisted-run/chimera, and `<miniBox>` is its
submodule `<chimera>/extern/chimera-common-minibox`.

## Requirements

- Linux, x86-64. CI builds on GitHub's `ubuntu-latest`. The build runs on
  Linux; the package it makes is the same file on Linux and on Windows.
- For the core, the package and the core gate (the workflow's `core-gate`
  job):

      sudo apt-get update
      sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3

  No compiler version is pinned: the build uses the `gcc` and `g++` that
  `build-essential` installs. `meson.build` asks for meson 0.60.0 or newer
  and compiles as `gnu++20`. `git` and `curl` are used too (by the scripts
  and by miniBox's build); the workflow installs neither, so they have to be
  there already.
- The network, once: miniBox's C++ guest toolchain downloads the GCC source
  that matches the host `gcc` (about 84 MB) and builds libstdc++ for the
  guest from it. Nothing in this repository downloads anything.
- For the frontend gate and Chimera's contract tests as well (the
  `frontend-gate` job), what a built Chimera needs:

      sudo apt-get install -y --no-install-recommends \
        meson ninja-build build-essential cmake pkg-config python3 \
        mono-complete xvfb ffmpeg \
        libgl1-mesa-dev libx11-dev libxext-dev libasound2-dev

  and the .NET SDK 8.0. The workflow gets it from `actions/setup-dotnet@v4`
  with `dotnet-version: '8.0'`; by hand, Chimera's README gives

      curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0

## Get the sources

This repository, with its two submodules: `extern/ares` (upstream ares) and
`extern/angrylion-rdp` (the software rasteriser for the Nintendo 64, from
the `ares` branch of TASEmulators/angrylion-rdp). The workflow uses
`actions/checkout@v6` with `submodules: true`, which is:

    git clone https://github.com/ToolAssisted-run/chimera-core-ares.git
    cd chimera-core-ares
    git submodule update --init

A Chimera checkout, for miniBox. The workflow checks out Chimera's `main`.
To build and to run the core gate, only the miniBox submodule is needed:

    git clone https://github.com/ToolAssisted-run/chimera.git <chimera>
    git -C <chimera> submodule update --init extern/chimera-common-minibox

For the frontend gate the workflow checks Chimera out with every submodule
(`submodules: recursive`), which is `git clone --recursive`.

Where the scripts look when they are not told:

| Script | Option | Default |
| --- | --- | --- |
| `meson setup` (`waterbox/meson.build`) | `-Dminibox_dir=<miniBox>` | `../chimera/extern/chimera-common-minibox`, beside this repository. If it is not there, `run-wbx` is left out of the build with no error |
| `waterbox/setup-guest.sh` | `-m <miniBox>` or `MINIBOX_DIR` | `$HOME/chimera/extern/chimera-common-minibox` |
| `waterbox/build-package.sh` | `-r <chimera>` | `../chimera` beside this repository, then `$HOME/chimera` (then the same two under the older name `miniHawk`) |
| `waterbox/build-package.sh` | `-m <miniBox>` or `MINIBOX_DIR` | `<chimera>/extern/chimera-common-minibox` |
| `waterbox/run-gate.sh` | `MINIBOX_DIR` (for its `check-wbx` leg only) | `$HOME/chimera/extern/chimera-common-minibox` |
| `waterbox/run-frontend-gate.sh` | `CHIMERA_BUILD` (Chimera's `build` directory) | `../../chimera/build`, two levels above this repository |

The defaults are not all the same place. Pass the paths, as the workflow
does.

## Build miniBox

The sandbox host, and the guest toolchain with C++ (ares throws for machine
control flow, so the guest needs libstdc++):

    mb=<miniBox>
    [ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
    meson compile -C "$mb/build/meson-linux"
    [ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
    meson compile -C "$mb/build/meson-cpp"

`build/meson-linux` holds the host library the sandbox driver links
(`source/host/libminiboxhost.so`). `build/meson-cpp` holds the guest sysroot
(`guest-sysroot/`, musl and libstdc++) the core is compiled against. The
workflow caches these two directories with `actions/cache@v4`; on your own
machine they simply stay where they are.

## Build the core

**Patches.** `patches/` holds one directory per submodule: `patches/ares/`
(25 patches) and `patches/angrylion-rdp/` (2). There is no separate step:
`meson.build` runs `waterbox/apply-patches.sh` every time it configures.
The script takes each patch file by itself:

- a patch that applies is applied (`applied <submodule>/<name>`);
- a patch that reverses cleanly is already there, and is skipped;
- a patch that does neither prints a warning,
  `WARNING: <submodule>/<name> neither applies nor is applied`, and the
  script carries on and exits 0.

It stops with an error only when a submodule is not checked out. A warning
therefore does not stop the build: treat one as an error.

Once the patches are applied, `git status` shows `extern/ares` and
`extern/angrylion-rdp` as modified. That is the patch set; it is not
committed.

**The native reference and the sandbox driver.**

    meson setup build/meson-native -Dwaterbox=true -Dminibox_dir=<miniBox>
    ninja -C build/meson-native

This builds three programs in `build/meson-native/waterbox`:

- `run-native`: the same ares sources and the same machine layer
  (`waterbox/machine.cpp`) as the core, built for the host with no sandbox.
  It is what the gates compare the core against, and where debugging is
  done.
- `run-wbx`: runs `core.wbx` through the miniBox host on the same schedule,
  with the same digests. It is built only when meson knows where miniBox
  is.
- `gen-machines`: asks ares what each machine is made of. The gate uses it
  to check the committed declarations.

**The guest.**

    ./waterbox/setup-guest.sh -m <miniBox>
    ninja -C build/meson-guest

`waterbox/setup-guest.sh [-m <miniBox dir>]` writes the meson cross file
`build/guest-cross.ini` (machine-local paths, not committed) and configures
`build/meson-guest`. It stops if `<miniBox>/build/meson-cpp` has no guest
libstdc++. The result is `build/meson-guest/waterbox/core.wbx`.

Both flavours are built with the same defines (`core_defines` in
`meson.build`): the interpreters and not the recompilers, angrylion's
software rasteriser and no Vulkan, one thread, a pinned power-on seed. They
must agree or the two are not the same emulator.

## Build the package

    ./waterbox/build-package.sh -m <miniBox> -r <chimera>

Usage:
`./build-package.sh [-m <miniBox dir>] [-r <chimera root>] [-o <build dir>]`.
`-o` is where the staging directory goes (default: this repository's
`build`); it does not move the package. The script:

1. configures `<miniBox>/build/meson-cpp` with `-Dguest_cpp=true` if it is
   not configured, and runs `ninja` there;
2. configures the guest build if `build/meson-guest` is not configured, and
   runs `ninja -C build/meson-guest`;
3. runs miniBox's `source/guest/check-wbx.sh` on `core.wbx` and stops if the
   guest is not sandbox-clean;
4. stages `core.wbx`, `waterbox.config`, `default_keybinds.json`,
   `file_slots.json`, a `licenses/` folder (the licence texts
   `waterbox/package-licenses.json` names) and a `build.json` (what built
   it) in `<build dir>/package-staging`;
5. stamps the version into the staged `waterbox.config`;
6. writes `<chimera>/build/Cores/ares.chimeraCore`, packs it a second time
   and stops if the two are not byte-identical. It prints
   `package sha1 <hash>` and `packaged -> <path>`;
7. removes any `<chimera>/build/UnpackedCores/ares-*` directory.

It does not need a built Chimera.

**The version** is the commit. CI passes `CORE_VERSION` (the commit SHA).
Without it the script stamps `<commit>+local`, where `<commit>` is twelve
characters, or `<commit>-dirty+local` when `git diff --quiet HEAD` reports a
change. The applied patch set counts as a change (the submodules read as
modified), so a hand build normally says `-dirty`. `versionDate` is the
commit's date in UTC, never the build's. A hand-built package is for
testing: Chimera's publish step refuses a version with `+local` or `-dirty`.

CI publishes what passed both jobs: a rolling `dev` release on every green
push to `main`, and a dated `nightly-YYYY-MM-DD` release from the scheduled
run (cron `0 4 * * *`), only when `main` moved since the last one. Nothing
is published from a pull request. The asset is named
`ares-<version>.chimeraCore`.

## Install it into Chimera

Chimera ships no cores and downloads none: it has no network code. A core
gets there as a file.

- **A Chimera source checkout.** The cores folder is `<chimera>/build/Cores/`
  and `build-package.sh -r <chimera>` has already written the package there.
  Start Chimera (`<chimera>/build/ChimeraMono.sh` on Linux).
- **A release bundle.** Copy `ares.chimeraCore` into the `Cores` folder
  beside `Chimera.exe`, or into the folder chosen in File > Core Manager >
  Change folder... The same file serves a Linux and a Windows Chimera.

File > Core Manager lists the packages in that folder; Refresh List rescans
it. A hand-built version reads as its commit followed by `local`.

To use a published build instead, download `ares-<version>.chimeraCore`
from https://github.com/ToolAssisted-run/chimera-core-ares/releases and put
it in the same folder.

## Run the gates

There are two gate scripts, and CI also runs Chimera's contract tests on
the package. Every leg prints `PASS`, `FAIL` or `SKIP` with the reason.
Read the `SKIP` lines: a skipped leg proved nothing.

### The core gate

    MINIBOX_DIR=<miniBox> ./waterbox/run-gate.sh

The script takes no options. It needs `run-native`, `run-wbx` and
`gen-machines` in `build/meson-native/waterbox`, `core.wbx` in
`build/meson-guest/waterbox`, and `python3`. `MINIBOX_DIR` is read only by
its first leg, `check-wbx`; with no miniBox there that leg is skipped. The
last line is `<n> passed, <n> failed`; skipped legs are not counted. The
exit status is non-zero on any failure.

Each kind of leg compares one thing:

- `native == sandbox`: the reference and the sandbox give the same video,
  audio, memory and state digests.
- `survives a savestate every frame`: the sandbox saves and reloads the
  whole machine before every frame, digests unchanged.
- `told twice is told once`: buttons pushed only when they change, as a
  frontend does, give the same machine.
- `goes back to a frame it left`: save, run on, put the save back.
- `is the same machine twice running`: two native runs agree.
- `refresh is ...`: the rate the machine reports is the one expected, and
  the one `waterbox/machines.h` declares where it declares one.

**With nothing provided** the content is `tests/content/`, which is in the
repository and free to distribute (`tests/content/README.md`). These run:

- `check-wbx`, when miniBox is found;
- `the declared machines match ares`: every machine that needs no console
  BIOS is rebuilt and re-enumerated and must equal what is committed, and
  the generated files must be what `waterbox/gen-config.py` writes. It
  prints how many machines were skipped for want of a BIOS;
- the Nintendo 64 legs, on `helloworld-cpu.n64`, `helloworld-rdp.n64` and
  `input-cpu.n64`: every kind of leg above; buttons and the analogue stick
  each make a different machine; every stick byte reaches the machine
  unchanged; `helloworld-cpu` is pixel-exact against the capture from real
  hardware.

Everything else is skipped, and says so. This is what a public runner can
run.

**With console BIOSes** in `tests/firmware/` (gitignored), each file named
after the firmware id the package declares:

| File in `tests/firmware/` | What it unlocks |
| --- | --- |
| `gbBoot` | the Game Boy legs on `libbet.gb`, and `GB buttons` |
| `gbaBios` | the Game Boy Advance legs on `gba-arm.gba`, and `GBA passes jsmolka's ARM test suite` |
| `ps1Bios` | the PlayStation legs (the BIOS shell; no content needed) |
| `msxBios` | the MSX legs (its own BASIC; no content needed) |
| `pifNtsc` | `the HLE boot hands over what a real boot hands over` |
| `zxsBios`, with a tape in `tests/local/ZXS/` | the two ZX Spectrum tape legs |
| every other BIOS | that machine in `the declared machines match ares` |

A BIOS with variants is `tests/firmware/<id>.<value>`, for example
`megaCdBios.usa` (`docs/PLAN.md`, "A BIOS that is not one file").

**With content of your own** in `tests/local/<MACHINE>/` (gitignored), one
directory per machine id, legs of the same kinds run for: `NGPC`, `PCE`,
`SGX`, `SFC`, `BSX` (run on the Super Famicom), `32X`, `MSX`, `MSX2`,
`MCD`, `PCECD` and `MCD32X`. The gate takes the first entry in the
directory. A disc is a directory holding the `.cue` or `.chd` and every
track. A second cartridge for a slot on the cartridge goes in
`tests/local/<MACHINE>-sub/`. A machine that needs a BIOS still needs it in
`tests/firmware/`. A machine with nothing in its directory is reported
`SKIP`.

### The frontend gate

    CHIMERA_BUILD=<chimera>/build ./waterbox/run-frontend-gate.sh

The script takes no options. It runs the package inside Chimera, headless,
and reads back the video file Chimera recorded. It needs:

- `build/meson-native/waterbox/run-native` (the workflow builds only that
  target in this job: `ninja -C build/meson-native waterbox/run-native`);
- the package: `build/Cores/ares.chimeraCore` in this repository if it is
  there, else `<chimera>/build/Cores/ares.chimeraCore`;
- a built Chimera, with `<chimera>/build/ChimeraMono.sh`, built as the
  workflow builds it:

      cd <chimera>
      meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
      meson compile -C build/meson-linux
      meson install -C build/meson-linux
      dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false

- `mono`, `ffprobe` and `xvfb-run` on the PATH (the `mono-complete`,
  `ffmpeg` and `xvfb` packages);
- an executable `<chimera>/build/dll/ffmpeg`. Chimera's
  `tools/fetch-ffmpeg.sh linux <chimera>/build/dll` puts one there, and the
  workflow runs it; the meson install alone does not.

**When any of these is missing the script prints a line that starts
`SKIP everything:` and exits 0.** That is a skip of the whole gate, not a
pass. Check the first lines of its output. With `FRONTEND_GATE_REQUIRED=1`
in the environment, as CI sets it, the same case is a failure, and so is a
run in which no leg passed.

Its legs, each a project written by `waterbox/tests/make-project.py`,
opened in Chimera and recorded for 120 frames:

| Leg | Needs |
| --- | --- |
| `N64 helloworld` | nothing |
| `GB libbet` | `tests/firmware/gbBoot` |
| `GBA arm tests` | `tests/firmware/gbaBios` |
| `PS1 BIOS shell` | always skipped: a project must name a file |

A leg passes when the recording has the frame rate and the picture size
the native reference reports, 44100 Hz stereo sound, and the log has no
`sbrk heap exhausted`. The last line is
`<n> passed, <n> failed, <n> skipped`.

### Chimera's contract tests

The workflow's `frontend-gate` job then runs Chimera's own tests against
the installed package. They need the built Chimera above:

    cd <chimera>
    CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
      -c Release --nologo \
      --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"

They prove the package is readable, is built for a guest ABI this frontend
runs, becomes a working factory, binds only buttons its controllers
declare, stamps a version, and gives no two controls of one controller the
same movie letter. They need no ROM and no BIOS. They run over every
package in `CHIMERA_CORES_DIR`.

## Files the core needs at run time

None of these is in the repository or in the package. The user provides
them.

**A console BIOS, as project firmware, for nineteen of the twenty-nine
machines.** `waterbox/waterbox.config` declares each with its size and
SHA-1; Chimera asks for it and pins it by hash.

| Machine | Firmware id |
| --- | --- |
| Game Boy | `gbBoot` |
| Game Boy Color | `gbcBoot` |
| Game Boy Advance | `gbaBios` |
| WonderSwan | `wsBoot` |
| WonderSwan Color | `wscBoot` |
| ZX Spectrum | `zxsBios` |
| ColecoVision | `cvBios` |
| MSX | `msxBios` |
| MSX2 | `msx2Main`, `msx2Sub` |
| PlayStation | `ps1Bios` |
| Atari 5200 | `a52Bios` |
| Neo Geo Pocket | `ngpBios` |
| Neo Geo Pocket Color | `ngpcBios` |
| Neo Geo AES | `ngBios` |
| Super Famicom / SNES | `sfcIpl` |
| Mega Drive 32X | `m32xVector`, `m32xBootM`, `m32xBootS` |
| Mega CD / Sega CD | `megaCdBios`: one of three, chosen by the Which Mega CD BIOS setting |
| Mega CD 32X / Sega CD 32X | the Mega CD's and the 32X's |
| PC Engine CD / TurboDuo | `pceSystemCard`: one of six, chosen by the Which System Card setting |

Ten machines declare no firmware: Nintendo 64, Famicom / NES, Mega Drive /
Genesis, Master System, Game Gear, SG-1000, Atari 2600, MyVision, PC Engine
/ TurboGrafx-16 and SuperGrafx.

**The game**, in the slots `waterbox/file_slots.json` declares:

| Slot | Files | Notes |
| --- | --- | --- |
| Cartridge, disc or tape | one file; the formats follow the machine | A disc is a `.cue` or a `.chd`. A PlayStation and an MSX also start with the slot empty; every other machine refuses to. |
| Save data | one `.sav` `.srm` `.eep` `.fla` or `.ram` | Optional. Only the Nintendo 64 exports and reloads saves this way. |
| Cartridge in the cartridge | one `.bs` `.st` `.gb` or `.gbc` | Super Famicom only. |

## Troubleshooting

- **`extern/ares is empty`** from `meson setup`, or
  **`extern/<name> is not a checked-out submodule`** from
  `apply-patches.sh`: the submodules are not there. The message gives the
  command.
- **`WARNING: <submodule>/<patch> neither applies nor is applied`**: a file
  the patch touches was edited, or the submodule was moved without rebasing
  the patches. The build goes on without that change. Run
  `./waterbox/apply-patches.sh` by hand to read what it says.
- **`missing .../run-wbx - see the header of this script`** from
  `run-gate.sh`: meson did not know where miniBox is and left `run-wbx`
  out. Configure `build/meson-native` with `-Dminibox_dir=<miniBox>`.
- **`miniBox C++ guest toolchain missing at <path>`** from
  `setup-guest.sh`: miniBox was built without `-Dguest_cpp=true`. The
  message gives the command.
- **`miniHawk checkout not found; pass -r <path>`** from
  `build-package.sh`: it means the Chimera checkout. Pass `-r`.
- **`SKIP check-wbx (no miniBox at ...)`** in the core gate: set
  `MINIBOX_DIR`.
- **`these machines are not what ares says any more`** or
  **`out of date with ares`** in the core gate: a declaration no longer
  matches the emulator, or a generated file was edited by hand. The message
  gives the command. Read "Rules of this repository" in `AGENTS.md` before
  running it: regenerating without the BIOS files drops the machines that
  need them.
- **`a run produced no digest`**: one of the two runs failed before it
  printed anything, for example on a BIOS it could not load. The leg prints
  what each run said instead.
- **`the guest ran out of sbrk N times - raise memoryLayoutMiB`** in the
  frontend gate: the arena in `waterbox.config` is too small for that
  machine. `waterbox/memory-layout.h` is generated from it.
- **Windows.** The core takes its coroutine stacks with `MAP_STACK`
  (`patches/ares/0015`), and a miniBox too old to know them kills it on its
  first frame there. `README.md` and `docs/PLAN.md` ("Windows, and the
  sandbox bug it found") name the miniBox commits. Building against the
  miniBox that Chimera's `main` pins is what CI does.
- **A moved miniBox.** `build/guest-cross.ini` holds absolute paths. Run
  `waterbox/setup-guest.sh` again after miniBox moves.
