project('chimera-core-ares', 'c', 'cpp',
  version: '0.1.0',
  license: 'ISC',
  default_options: ['cpp_std=gnu++20', 'buildtype=release', 'warning_level=0'],
  meson_version: '>=0.60.0'
)

fs = import('fs')

ares_dir = meson.current_source_dir() / 'extern/ares'
angrylion_dir = meson.current_source_dir() / 'extern/angrylion-rdp/mylittle-nocomment'

if not fs.is_dir(ares_dir / 'ares')
  error('extern/ares is empty - run: git submodule update --init --recursive')
endif

# ---------------------------------------------------------------------------
# The patch series, applied to the pinned submodules before anything compiles.
# Idempotent, so reconfiguring costs nothing.
run_command('waterbox/apply-patches.sh', check: true)

# ---------------------------------------------------------------------------
# Generated sources.
#
# ares keeps its icons and its version string out of the source tree: the icons
# are a .bml turned into a C++ array by ares' own `sourcery`, and the version is
# a @-substituted template. Both are generated into the build directory rather
# than into the submodule, so a checkout stays exactly what its pin says.

sourcery = executable('sourcery',
  [ares_dir / 'tools/sourcery/sourcery.cpp',
   ares_dir / 'nall/nall/main.cpp',
   ares_dir / 'nall/nall/directory.cpp',
   ares_dir / 'nall/nall/inode.cpp',
   ares_dir / 'nall/nall/path.cpp',
   ares_dir / 'nall/nall/file-map.cpp'],
  include_directories: include_directories('extern/ares', 'extern/ares/nall'),
  native: true,
  cpp_args: ['-w'],
  # In a cross build the project's default_options describe the HOST machine, so
  # a native tool gets whatever the build machine's default C++ is - which for
  # nall is not enough (std::span, std::ranges).
  override_options: ['cpp_std=gnu++20'],
  install: false,
)

# sourcery resolves the .bml's file= paths relative to the CURRENT directory, so
# each one is generated in a directory of its own - which is also what keeps the
# two `resource.hpp` outputs from colliding.
subdir('gen/ares/resource')
subdir('gen/mia/resource')

ares_version = 'chimera-' + meson.project_version()
ares_cpp = configure_file(
  input: ares_dir / 'ares/ares/ares.cpp.in',
  output: 'ares.cpp',
  configuration: configuration_data({
    'ARES_NAME': 'ares',
    'ARES_VERSION': ares_version,
    'ARES_LEGAL_COPYRIGHT_SHORT': 'ares team',
    'ARES_WEBSITE': 'ares-emu.net',
  }),
)

# ---------------------------------------------------------------------------
# What the machine is made of.
#
# ares compiles as a handful of translation units that #include the rest, so
# this list is short and matches ares' own CMake targets one for one.

# The machines. Every ares system is one translation unit that #includes the
# rest of itself, with the PC Engine's two video implementations as the one
# exception. Which of them this core actually offers is waterbox/machines.h;
# this list is what has to compile.
ares_systems = [
  'a26', 'fc', 'gb', 'md', 'ms', 'myvision', 'n64', 'sg', 'spec', 'ws',
]

# The chips those machines are built out of. Each is one translation unit too,
# and they are shared - the Z80 in a Master System is the Z80 in an SG-1000 - so
# they are compiled once whatever is enabled.
ares_components = [
  'audio/ay38910', 'audio/msm5205', 'audio/sn76489', 'audio/t6w28',
  'audio/ym2149', 'audio/ym2413', 'audio/ym2612',
  'eeprom/m24c', 'eeprom/m93lcx6', 'flash/sst39sf0x0', 'io/i8255',
  'processor/arm7tdmi', 'processor/armv6m', 'processor/gsu', 'processor/hg51b',
  'processor/huc6280', 'processor/i8080', 'processor/m68000', 'processor/m68hc05',
  'processor/mos6502', 'processor/sh2', 'processor/sm5k', 'processor/sm83',
  'processor/spc700', 'processor/ssp1601', 'processor/tlcs900h', 'processor/upd96050',
  'processor/v30mz', 'processor/wdc65816', 'processor/z80',
  'rtc/s3511a', 'video/tms9918', 'video/v9938',
]

core_sources = [
  ares_dir / 'nall/nall/nall.cpp',
  ares_dir / 'nall/nall/sljitAllocator.cpp',
  ares_dir / 'ares/ares/memory/fixed-allocator.cpp',
  ares_dir / 'mia/mia.cpp',
  ares_dir / 'libco/libco.c',
  ares_dir / 'thirdparty/sljit/sljit_src/sljitLir.c',
  ares_dir / 'thirdparty/qon/qon.cpp',
  # The software rasteriser, in paraLLEl-RDP's place.
  angrylion_dir / 'angrylion.cpp',
  angrylion_dir / 'n64video.cpp',
  # The machine as this core drives it.
  files('waterbox/machine.cpp'),
]

foreach s : ares_systems
  core_sources += ares_dir / 'ares' / s / (s + '.cpp')
endforeach

foreach c : ares_components
  core_sources += ares_dir / 'ares/component' / c / (c.split('/')[1] + '.cpp')
endforeach

# The Famicom's expansion audio and the Neo Geo's sound chip are ymfm's.
foreach y : ['adpcm', 'misc', 'opl', 'opm', 'opn', 'opq', 'opz', 'pcm', 'ssg']
  core_sources += ares_dir / 'thirdparty/ymfm/src' / ('ymfm_' + y + '.cpp')
endforeach

# mia reads ZX Spectrum tapes, and TZXFile is a file per block type.
tzx_sources = run_command('sh', '-c',
  'ls ' + ares_dir / 'thirdparty/TZXFile' + '/*.cpp', check: true).stdout().strip().split('\n')
core_sources += tzx_sources

core_sources += ares_cpp

# ares.cpp #includes its resource.cpp, so only the header is listed - naming an
# output of the target is what makes meson generate BOTH before anything that
# needs them compiles. mia compiles its resource.cpp as a unit of its own.
core_sources += ares_resource[1]
core_sources += mia_resource[0]

core_inc = include_directories(
  'extern/ares',
  'extern/ares/ares',
  'extern/ares/nall',
  'extern/ares/mia',
  'extern/ares/thirdparty',
  'extern/ares/thirdparty/TZXFile',
  'extern/ares/thirdparty/ymfm/src',
  'extern/ares/thirdparty/qon',
  'extern/ares/thirdparty/sljit/sljit_src',
  'extern/angrylion-rdp/mylittle-nocomment',
  'waterbox',
)

# The generated headers live beside their generated sources; both flavours find
# them as <ares/resource/resource.hpp> and <mia/resource/resource.hpp>, which is
# how ares' own sources spell them.
gen_inc = include_directories('gen')

# These MUST agree between the two flavours or they are not the same emulator:
#   ANGRYLION           - without it ares' RDP decodes commands and draws nothing
#   VULKAN (absent)     - paraLLEl-RDP is not built at all
#   WANT_*_INTERPRETER  - which of two CPU implementations is compiled
#   MIA_LIBRARY         - mia without its GUI
core_defines = [
  '-DNDEBUG',
  '-DMIA_LIBRARY',
  '-DANGRYLION',
  # ares builds each machine one of two ways. A tool-assisted run wants the one
  # that models the hardware rather than the one that is quick about it.
  '-DPROFILE_ACCURACY',
  # Every machine's power-on state comes from a number, not from the host - see
  # patches/ares/0010. Without it the two flavours disagree, because a sandbox
  # has no getrandom to call and the reference does.
  '-DCHIMERA_DETERMINISTIC_SEED',
  # ares draws on a second thread by default, and hands frames over through a
  # timed wait. A sandbox has one thread; the reference must have one too, or
  # the two are not running the same program. See patches/ares/0011.
  '-DCHIMERA_SINGLE_THREADED',
  '-DSLJIT_HAVE_CONFIG_PRE=1',
  '-DSLJIT_HAVE_CONFIG_POST=1',
  '-DWANT_CPU_INTERPRETER=' + (get_option('cpu_interpreter') ? '1' : '0'),
  '-DWANT_RSP_INTERPRETER=' + (get_option('rsp_interpreter') ? '1' : '0'),
]

core_args = core_defines + [
  # ares leans on signed overflow and type punning in its emulation cores; both
  # are how the emulated hardware behaves, and neither is undefined here.
  '-fno-strict-aliasing',
  '-fwrapv',
]

subdir('waterbox')
