# Generated here rather than into the submodule, so a checkout stays exactly
# what its pin says. sourcery writes '#include "<the name you gave it>"' into
# the .cpp verbatim and opens both outputs by that same name, so it is run from
# inside its own output directory and told plain file names.
mia_resource = custom_target('mia-resource',
  output: ['resource.cpp', 'resource.hpp'],
  input: ares_dir / 'mia/resource/resource.bml',
  command: ['sh', '-c', 'cd "$1" && "$2" "$3" resource.cpp resource.hpp',
            'sourcery-in', '@OUTDIR@', sourcery.full_path(), '@INPUT@'],
  depends: sourcery,
  build_by_default: true,
)
