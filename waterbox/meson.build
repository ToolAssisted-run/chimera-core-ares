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

  executable('core',
    core_sources + files('waterbox.cpp', 'guest-syscalls.cpp'),
    name_suffix: 'wbx',
    include_directories: [core_inc, gen_inc],
    c_args: core_args,
    cpp_args: core_args,
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

  executable('run-native',
    core_sources + files('run-native.cpp'),
    include_directories: [core_inc, gen_inc, native_shim],
    c_args: core_args,
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
    executable('run-wbx', files('run-wbx.c'),
      # -I rather than include_directories(): meson refuses to make an absolute
      # path to a directory inside the source tree, and in CI the Chimera
      # checkout (and so miniBox) sits inside this repository's workspace.
      include_directories: [core_inc],
      c_args: ['-I' + mbdir / 'source/host'],
      link_args: [mbhost / 'libminiboxhost.so'],
      build_rpath: mbhost,
      install: false)
  endif
endif
