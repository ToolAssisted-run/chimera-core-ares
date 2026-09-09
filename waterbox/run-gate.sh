#!/bin/sh
# The equivalence gate: every leg compares the reference against the sandbox, or
# the sandbox against itself across a savestate, and says what it compared.
#
#   ./waterbox/run-gate.sh
#
# Needs both builds:
#   meson setup build/meson-native -Dwaterbox=true -Dminibox_dir=<miniBox>
#   ninja -C build/meson-native
#   ./waterbox/setup-guest.sh && ninja -C build/meson-guest
#
# Content is tests/content/ - public domain N64 test ROMs, so this runs anywhere,
# including a CI runner with nothing licensed on it.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"

native="$root/build/meson-native/waterbox/run-native"
runwbx="$root/build/meson-native/waterbox/run-wbx"
wbx="$root/build/meson-guest/waterbox/core.wbx"
content="$root/tests/content"

for f in "$native" "$runwbx" "$wbx"; do
	[ -x "$f" ] || [ -f "$f" ] || { echo "missing $f - see the header of this script" >&2; exit 1; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0

say_pass() { echo "PASS $1"; pass=$((pass + 1)); }
say_fail() { echo "FAIL $1"; echo "     $2"; fail=$((fail + 1)); }

# Runs the reference and the sandbox over the same ROM and options, and requires
# every digest to agree. The sandbox gets a work dir holding the ROM under the
# name its slot map gives, which is what the frontend mounts.
compare() {
	leg="$1"; rom="$2"; shift 2
	rm -rf "$work/w"; mkdir -p "$work/w"
	cp "$content/$rom" "$work/w/rom"
	printf '{"rom":["rom"]}' > "$work/w/slots"
	a="$("$native" --rom "$work/w/rom" --quiet "$@" | tail -1)"
	b="$("$runwbx" "$wbx" "$work/w" --quiet "$@" | tail -1)"
	if [ "$a" = "$b" ]; then
		say_pass "$leg (native == sandbox)"
		echo "     $a"
	else
		say_fail "$leg (native == sandbox)" "native  $a
     sandbox $b"
	fi
}

# The same, but the sandbox saves and reloads the WHOLE machine before every
# frame. Anything of the machine living outside the arena shows up here.
rerecord() {
	leg="$1"; rom="$2"; shift 2
	rm -rf "$work/w"; mkdir -p "$work/w"
	cp "$content/$rom" "$work/w/rom"
	printf '{"rom":["rom"]}' > "$work/w/slots"
	a="$("$runwbx" "$wbx" "$work/w" --quiet "$@" | tail -1)"
	b="$("$runwbx" "$wbx" "$work/w" --quiet --rerecord "$@" | tail -1)"
	if [ "$a" = "$b" ]; then
		say_pass "$leg (survives a savestate every frame)"
	else
		say_fail "$leg (survives a savestate every frame)" "straight  $a
     rerecord  $b"
	fi
}

echo "== the guest is sandbox-clean =="
mb="${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}"
if [ -x "$mb/source/guest/check-wbx.sh" ]; then
	if out="$("$mb/source/guest/check-wbx.sh" "$wbx" 2>&1)"; then
		say_pass "check-wbx"
		echo "     $out"
	else
		say_fail "check-wbx" "$out"
	fi
else
	echo "SKIP check-wbx (no miniBox at $mb)"
fi

echo
echo "== the machine is the same in both flavours =="
# 130 frames is past the boot ROM, which takes about ninety, and into the part
# where the game has programmed the video interface and drawn something.
compare "helloworld-cpu" helloworld-cpu.n64 --frames 130
compare "helloworld-rdp" helloworld-rdp.n64 --frames 130
compare "input-cpu"      input-cpu.n64      --frames 200
compare "input-cpu, A held and the stick over" input-cpu.n64 --frames 200 --hold a --stick 100 -60

echo
echo "== the machine survives being saved and reloaded =="
rerecord "helloworld-cpu" helloworld-cpu.n64 --frames 130
rerecord "helloworld-rdp" helloworld-rdp.n64 --frames 130
rerecord "input-cpu, A held" input-cpu.n64 --frames 200 --hold a

echo
echo "== the same run twice is the same machine =="
# Real hardware powers on with random RDRAM timings and ares seeds its RNG from
# the host clock unless told not to. Without the pinned seed this leg fails and
# every movie is unreproducible, so it is worth its own check.
a="$("$native" --rom "$content/helloworld-cpu.n64" --quiet --frames 60 | tail -1)"
b="$("$native" --rom "$content/helloworld-cpu.n64" --quiet --frames 60 | tail -1)"
if [ "$a" = "$b" ]; then
	say_pass "the entropy seed is pinned"
else
	say_fail "the entropy seed is pinned" "first   $a
     second  $b"
fi

echo
echo "== input reaches the machine =="
# A leg that only proved two builds agree would pass just as well with the input
# wire cut. These prove the machine NOTICED - each distinct input has to produce
# a distinct machine.
idle="$("$native" --rom "$content/input-cpu.n64" --quiet --frames 200 | tail -1)"
held="$("$native" --rom "$content/input-cpu.n64" --quiet --frames 200 --hold a | tail -1)"
other="$("$native" --rom "$content/input-cpu.n64" --quiet --frames 200 --hold start | tail -1)"
stick="$("$native" --rom "$content/input-cpu.n64" --quiet --frames 200 --stick 127 0 | tail -1)"
stick2="$("$native" --rom "$content/input-cpu.n64" --quiet --frames 200 --stick 40 0 | tail -1)"
if [ "$idle" != "$held" ] && [ "$held" != "$other" ]; then
	say_pass "buttons (idle, A and Start each make a different machine)"
else
	say_fail "buttons" "idle  $idle
     A     $held
     Start $other"
fi
if [ "$idle" != "$stick" ] && [ "$stick" != "$stick2" ]; then
	say_pass "the analogue stick (centre, full and half deflection all differ)"
else
	say_fail "the analogue stick" "centre $idle
     full   $stick
     half   $stick2"
fi

echo
echo "== the picture is the one the hardware draws =="
# PeterLemon's repository ships the picture each program produces on a real
# console. The CPU one is a plain framebuffer, so with the video interface's
# filtering off it should match exactly - and it does, to the pixel. See
# docs/PLAN.md for why the RDP one is not held to the same test yet.
"$native" --rom "$content/helloworld-cpu.n64" --quiet --frames 130 --fast-vi \
	--dump-frame 120 --dump-to "$work/frame.ppm" >/dev/null 2>&1
if out="$(python3 "$here/tests/compare-picture.py" "$content/helloworld-cpu.png" "$work/frame.ppm" 0 2>&1)"; then
	say_pass "helloworld-cpu is pixel-exact against real hardware"
	echo "     $out"
else
	say_fail "helloworld-cpu is pixel-exact against real hardware" "$out"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
