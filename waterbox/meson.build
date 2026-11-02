# The two flavours. A cross build IS the guest, so it selects itself;
# -Dwaterbox=true selects the native reference.

if meson.is_cross_build()
  # ---- the core the frontend loads --------------------------------------
  # miniBox's musl/libstdc++ toolchain arrives through the cross file
  # (waterbox/setup-guest.sh); the link recipe is the guest kit's - library
  # order, --no-relax for the large code model, the weak pthread pulls for
  # libgcc_eh, cxxglue for the unwinder, and an 8MB default thread stack.
  mb = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  # paraLLEl-RDP draws through miniBox's Vulkan bridge: its guest half is the
  # vk* commands, a wrapper each that hands the call to the frontend, generated
  # from miniBox's shared list so an opcode means here what it means there.
  vk_bridge = custom_target('vk-bridge',
    output: ['vk-bridge-guest.c', 'vk-bridge-ops.h', 'vk-bridge-host.inc'],
    command: [find_program('python3'), mb / 'source/vk/gen-vk-bridge.py',
      mb / 'extern/vulkan-headers/registry/vk.xml',
      mb / 'source/vk/vk-entry-points.txt', '@OUTDIR@'])
  guest_libs = [static_library('vk-bridge-guest', [files(mb / 'source/vk/vk-guest.c'), vk_bridge[0], vk_bridge[1]],
    c_args: ['-I' + mb / 'source/vk', '-I' + mb / 'extern/vulkan-headers/include', '-I' + meson.current_build_dir()])]
  guest_args = core_args + ['-DCHIMERA_VK_BRIDGE']

  executable('core',
    core_sources + files('waterbox.cpp', 'guest-syscalls.cpp'),
    name_suffix: 'wbx',
    include_directories: [core_inc, gen_inc],
    c_args: guest_args,
    cpp_args: guest_args,
    link_with: guest_libs,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax', '-Wl,-z,stack-size=8388608',
      '-T', mb / 'source/guest/linkscript.T',
      # nall declares these weak as a portability hack; a weak symbol left
      # unresolved in a static guest link is address zero, and calling it faults
      # with nothing worth reading in the message.
      '-Wl,-u,pthread_once', '-Wl,-u,pthread_cond_wait',
      '-Wl,-u,pthread_cond_broadcast', '-Wl,-u,pthread_key_create',
      mbuild / 'source/guest/cxxglue.c.o',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lstdc++', '-lgcc', '-lgcc_eh', '-lc'],
    install: false,
  )
else
  # ---- the reference the gate compares against ---------------------------
  native_shim = include_directories('native-shim')

  # One build of the machines, shared by everything that drives them.
  ares_lib = static_library('ares-machines',
    core_sources,
    include_directories: [core_inc, gen_inc, native_shim],
    c_args: core_args,
    cpp_args: core_args,
    install: false,
  )

  # ares_resource[1] and mia_resource[1] are the generated headers these sources
  # #include. Meson does not carry that dependency across from the library, so
  # each target that compiles its own translation unit names them.
  executable('run-native',
    [files('run-native.cpp'), ares_resource[1], mia_resource[1]],
    link_with: ares_lib,
    include_directories: [core_inc, gen_inc, native_shim],
    cpp_args: core_args,
    dependencies: [dependency('threads')],
    install: false,
  )

  # Asks ares what its machines are made of. Its output is committed, and the
  # gate regenerates it and diffs - see waterbox/gen-config.py.
  executable('gen-machines',
    [files('gen-machines.cpp'), ares_resource[1], mia_resource[1]],
    link_with: ares_lib,
    include_directories: [core_inc, gen_inc, native_shim],
    cpp_args: core_args,
    dependencies: [dependency('threads')],
    install: false,
  )

  # run-wbx: the standalone sandbox driver the gate uses. Needs miniBox's host
  # library; configure with -Dminibox_dir=<checkout> to build it.
  mbdir = get_option('minibox_dir')
  if mbdir == ''
    mb_guess = meson.current_source_dir() / '../../chimera/extern/chimera-common-minibox'
    if fs.is_dir(mb_guess)
      mbdir = mb_guess
    endif
  endif
  if mbdir != ''
    mbhost = mbdir / 'build/meson-linux/source/host'
    # The Vulkan bridge's host half, the frontend's own file: it links
    # against nothing and finds the computer's Vulkan loader when asked.
    vk_bridge_host = custom_target('vk-bridge-host',
      output: ['vk-bridge-host.inc', 'vk-bridge-ops.h', 'vk-bridge-guest.c'],
      command: [find_program('python3'), mbdir / 'source/vk/gen-vk-bridge.py',
        mbdir / 'extern/vulkan-headers/registry/vk.xml',
        mbdir / 'source/vk/vk-entry-points.txt', '@OUTDIR@'])
    rw_sources = [files('run-wbx.cpp', mbdir / 'source/vk/vk-host.c'), vk_bridge_host[0], vk_bridge_host[1]]
    rw_args = core_args + ['-I' + mbdir / 'source/host', '-DCHIMERA_VK_BRIDGE']
    rw_c_args = ['-DCHIMERA_VK_BRIDGE', '-I' + mbdir / 'source/vk', '-I' + mbdir / 'extern/vulkan-headers/include',
      '-I' + meson.current_build_dir()]
    rw_deps = [meson.get_compiler('c').find_library('dl')]
    executable('run-wbx', rw_sources,
      # -I rather than include_directories(): meson refuses to make an absolute
      # path to a directory inside the source tree, and in CI the Chimera
      # checkout (and so miniBox) sits inside this repository's workspace.
      include_directories: [core_inc],
      c_args: rw_c_args,
      cpp_args: rw_args,
      dependencies: rw_deps,
      link_args: [mbhost / 'libminiboxhost.so'],
      build_rpath: mbhost,
      install: false)
  endif
endif
