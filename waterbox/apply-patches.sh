#!/bin/sh
# Overlays the chimera patch set onto the pinned submodules. Idempotent: a patch
# that is already applied is skipped, so configuring twice is harmless.
#
# The pins are pristine upstream; every difference this core needs is a file in
# patches/, which is what keeps "what did we change" answerable. One directory
# per submodule, named after it, so the mapping needs no table.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/.."

# ONE FILE PER PATCH, deliberately. `git apply --check` refuses a patch whose
# hunks are already applied, and it refuses the WHOLE patch - so a patch touching
# two files, one of which was reverted by hand, is silently skipped and the build
# quietly loses a change.
for dir in "$root"/patches/*/; do
	[ -d "$dir" ] || continue
	sub="$(basename "$dir")"
	tree="$root/extern/$sub"
	if [ ! -e "$tree/.git" ]; then
		echo "ERROR: extern/$sub is not a checked-out submodule; run git submodule update --init --recursive" >&2
		exit 1
	fi
	# A clone made without --recursive leaves an empty directory, and git commands
	# run inside it answer for the repository ABOVE it - which would happily apply
	# this whole series to the core repo's own working tree and say nothing.
	top="$(git -C "$tree" rev-parse --show-toplevel)"
	if [ "$(cd "$tree" && pwd -P)" != "$(cd "$top" && pwd -P)" ]; then
		echo "ERROR: extern/$sub is not its own repository (git answers for $top)" >&2
		exit 1
	fi
	for p in "$dir"*.patch; do
		[ -f "$p" ] || continue
		if git -C "$tree" apply --check "$p" 2>/dev/null; then
			git -C "$tree" apply "$p"
			echo "applied $sub/$(basename "$p")"
		elif ! git -C "$tree" apply --check --reverse "$p" 2>/dev/null; then
			echo "WARNING: $sub/$(basename "$p") neither applies nor is applied" >&2
		fi
	done
done
