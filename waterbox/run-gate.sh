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
# Content is tests/content/ - public domain and permissively licensed test ROMs,
# so this runs anywhere, including a CI runner with nothing licensed on it.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"

native="$root/build/meson-native/waterbox/run-native"
runwbx="$root/build/meson-native/waterbox/run-wbx"
genmachines="$root/build/meson-native/waterbox/gen-machines"
wbx="$root/build/meson-guest/waterbox/core.wbx"
content="$root/tests/content"

for f in "$native" "$runwbx" "$genmachines" "$wbx"; do
	[ -f "$f" ] || { echo "missing $f - see the header of this script" >&2; exit 1; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0

say_pass() { echo "PASS $1"; pass=$((pass + 1)); }
say_fail() { echo "FAIL $1"; echo "     $2"; fail=$((fail + 1)); }

# Runs the reference and the sandbox over the same machine, ROM and options, and
# requires every digest to agree. The sandbox gets a work dir holding the ROM
# under the name its slot map gives, and the settings a project would carry.
# A machine that needs a console BIOS is skipped when the developer has not put
# one in tests/firmware/ - it is not this repository's to carry. `firmware_for`
# echoes the file, or nothing.
# Content a developer may have and this repository may not carry. tests/local/
# is gitignored, exactly as tests/firmware/ is: a leg that needs a commercial
# ROM runs for whoever owns one and is skipped everywhere else, which is the
# only honest way to gate a machine whose only content is somebody's property.
# `local_content <machine>` echoes the first file for it, or nothing.
local_content() {
	dir="$root/tests/local/$1"
	[ -d "$dir" ] || return 0
	for f in "$dir"/*; do
		[ -f "$f" ] && { echo "$f"; return 0; }
	done
	return 0
}

# The same legs, on content only the developer has. `setup_work` takes a name
# inside tests/content, so this copies the file in and calls the leg by hand.
with_local() {
	kind="$1"; id="$2"; shift 2
	rom="$(local_content "$id")"
	if [ -z "$rom" ]; then
		echo "SKIP $id $kind (nothing in tests/local/$id)"
		return
	fi
	cp "$rom" "$content/.local-$id"
	case "$kind" in
		compare) compare "$id local" "$id" ".local-$id" "$@" ;;
		rerecord) rerecord "$id local" "$id" ".local-$id" "$@" ;;
		discipline) input_discipline "$id local" "$id" ".local-$id" "$@" ;;
		rewind) rewind "$id local" "$id" ".local-$id" "$@" ;;
	esac
	rm -f "$content/.local-$id"
}

firmware_for() {
	case "$1" in
		GB)  echo "$root/tests/firmware/gbBoot" ;;
		GBC) echo "$root/tests/firmware/gbcBoot" ;;
		WS)  echo "$root/tests/firmware/wsBoot" ;;
		WSC) echo "$root/tests/firmware/wscBoot" ;;
		ZXS) echo "$root/tests/firmware/zxsBios" ;;
		GBA) echo "$root/tests/firmware/gbaBios" ;;
		CV)  echo "$root/tests/firmware/cvBios" ;;
		MSX) echo "$root/tests/firmware/msxBios" ;;
		PS1) echo "$root/tests/firmware/ps1Bios" ;;
		NGP) echo "$root/tests/firmware/ngpBios" ;;
		NGPC) echo "$root/tests/firmware/ngpcBios" ;;
	esac
}

# A run that produced no digest proves nothing, and two of them prove nothing
# TWICE - which is worse, because the leg passes. It happened: the Game Boy's
# boot ROM stopped being carried in the package, every Game Boy leg started
# failing to init, and the gate went on reporting PASS because both halves
# failed identically. A digest is a line of key/value pairs; anything else is
# not an answer.
is_digest() {
	case "$1" in
		*video\ *audio\ *memory\ *state\ *) return 0 ;;
		*) return 1 ;;
	esac
}

# The arguments the reference needs, and the file the sandbox must mount.
setup_work() {
	id="$1"; rom="$2"
	rm -rf "$work/w"; mkdir -p "$work/w"
	if [ "$rom" = "-" ]; then
		# a machine that starts with nothing in its drive - a PlayStation
		# reaching its BIOS shell, which needs no content at all
		printf '{}' > "$work/w/slots"
	else
		cp "$content/$rom" "$work/w/rom"
		printf '{"rom":["rom"]}' > "$work/w/slots"
	fi
	printf '{"machine":"%s"}' "$(echo "$id" | tr 'A-Z' 'a-z')" > "$work/w/settings"
	fw="$(firmware_for "$id")"
	nativefw=""
	if [ -n "$fw" ]; then
		[ -f "$fw" ] || return 1
		# mounted under the id the package declares, which is what the guest opens
		cp "$fw" "$work/w/$(basename "$fw")"
		nativefw="$fw"
	fi
	return 0
}

compare() {
	leg="$1"; id="$2"; rom="$3"; shift 3
	if ! setup_work "$id" "$rom"; then
		echo "SKIP $leg (no console BIOS in tests/firmware)"
		return
	fi
	set -- "$@"
	[ "$rom" = "-" ] || set -- "$@" --rom "$work/w/rom"
	[ -z "$nativefw" ] || set -- "$@" --firmware "$nativefw"
	a="$("$native" --machine "$id" --quiet "$@" | tail -1)"
	b="$("$runwbx" "$wbx" "$work/w" --machine "$id" --quiet "$@" | tail -1)"
	if ! is_digest "$a" || ! is_digest "$b"; then
		say_fail "$leg (native == sandbox)" "a run produced no digest:
     native  $a
     sandbox $b"
		return
	fi
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
	leg="$1"; id="$2"; rom="$3"; shift 3
	if ! setup_work "$id" "$rom"; then
		echo "SKIP $leg (no console BIOS in tests/firmware)"
		return
	fi
	a="$("$runwbx" "$wbx" "$work/w" --machine "$id" --quiet "$@" | tail -1)"
	b="$("$runwbx" "$wbx" "$work/w" --machine "$id" --quiet --rerecord "$@" | tail -1)"
	if ! is_digest "$a" || ! is_digest "$b"; then
		say_fail "$leg (survives a savestate every frame)" "a run produced no digest:
     straight  $a
     rerecord  $b"
		return
	fi
	if [ "$a" = "$b" ]; then
		say_pass "$leg (survives a savestate every frame)"
	else
		say_fail "$leg (survives a savestate every frame)" "straight  $a
     rerecord  $b"
	fi
}

# A machine that is told the same thing twice must not notice, and a machine put
# back to a frame it has already left must be the machine that left it.
#
# These two go together because the bug they were written for needed both. A
# frontend pushes a button only when it CHANGES, and after loading a state it
# pushes every one again, because the state rewrote what the guest believed was
# held. ares presses the Neo Geo Pocket's power button itself - the BIOS halts
# and the CPU presses it on the user's behalf - so that resend landed on top of
# the machine's own value and asserted the power NMI. Twelve kilobytes of RAM
# diverged one frame later.
#
# The old rerecord leg could not see it: it pushes every button every frame, so
# a resend is nothing new, and it reloads the SAME frame, so state a savestate
# leaves out still holds the right value. --set-on-change is the frontend's
# discipline and --rewind-at goes back to a frame the machine has left.
input_discipline() {
	leg="$1"; id="$2"; rom="$3"; shift 3
	if ! setup_work "$id" "$rom"; then
		echo "SKIP $leg (no console BIOS in tests/firmware)"
		return
	fi
	a="$("$runwbx" "$wbx" "$work/w" --machine "$id" --quiet "$@" | tail -1)"
	b="$("$runwbx" "$wbx" "$work/w" --machine "$id" --quiet --set-on-change "$@" | tail -1)"
	if ! is_digest "$a" || ! is_digest "$b"; then
		say_fail "$leg (told twice is told once)" "a run produced no digest:
     every frame $a
     on change   $b"
		return
	fi
	if [ "$a" = "$b" ]; then
		say_pass "$leg (told twice is told once)"
	else
		say_fail "$leg (told twice is told once)" "every frame $a
     on change   $b"
	fi
}

rewind() {
	leg="$1"; id="$2"; rom="$3"; at="$4"; shift 4
	if ! setup_work "$id" "$rom"; then
		echo "SKIP $leg (no console BIOS in tests/firmware)"
		return
	fi
	a="$("$runwbx" "$wbx" "$work/w" --machine "$id" --quiet --set-on-change "$@" | tail -1)"
	b="$("$runwbx" "$wbx" "$work/w" --machine "$id" --quiet --set-on-change --rewind-at "$at" "$@" | tail -1)"
	if ! is_digest "$a" || ! is_digest "$b"; then
		say_fail "$leg (goes back to a frame it left)" "a run produced no digest:
     straight $a
     rewound  $b"
		return
	fi
	if [ "$a" = "$b" ]; then
		say_pass "$leg (goes back to a frame it left)"
	else
		say_fail "$leg (goes back to a frame it left)" "straight $a
     rewound  $b"
	fi
}

# Two runs of the same thing have to be the same machine. Real hardware powers
# on with random memory and ares models that from the host's entropy unless it
# is told not to; without the pin this leg fails and every movie is
# unreproducible. The STATE digest is what catches it - the picture agreed
# perfectly while the machine did not.
deterministic() {
	leg="$1"; id="$2"; rom="$3"; shift 3
	fw="$(firmware_for "$id")"
	set -- "$@"
	[ "$rom" = "-" ] || set -- "$@" --rom "$content/$rom"
	if [ -n "$fw" ]; then
		[ -f "$fw" ] || { echo "SKIP $leg (no console BIOS in tests/firmware)"; return; }
		set -- "$@" --firmware "$fw"
	fi
	a="$("$native" --machine "$id" --quiet "$@" | tail -1)"
	b="$("$native" --machine "$id" --quiet "$@" | tail -1)"
	if ! is_digest "$a" || ! is_digest "$b"; then
		say_fail "$leg is the same machine twice running" "a run produced no digest:
     first   $a
     second  $b"
		return
	fi
	if [ "$a" = "$b" ]; then
		say_pass "$leg is the same machine twice running"
	else
		say_fail "$leg is the same machine twice running" "first   $a
     second  $b"
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
echo "== every machine is what ares says it is =="
# The order of a controller's buttons IS the wire format a movie records, so it
# is generated from the emulator rather than typed. This leg rebuilds every
# machine, asks ares what it is made of, and fails if the committed declaration
# has moved - which is also how it proves all of them still build.
"$genmachines" "$root/tests/firmware" > "$work/machines.json"
if out="$(python3 "$here/gen-config.py" --check --against "$work/machines.json" 2>&1)"; then
	say_pass "the declared machines match ares"
	echo "     $out"
else
	say_fail "the declared machines match ares" "$out"
fi

echo
echo "== the machine is the same in both flavours =="
# 130 frames is past the Nintendo 64's boot ROM, which takes about ninety, and
# into the part where the game has drawn something.
compare "N64 helloworld-cpu" N64 helloworld-cpu.n64 --frames 130
compare "N64 helloworld-rdp" N64 helloworld-rdp.n64 --frames 130
compare "N64 input-cpu"      N64 input-cpu.n64      --frames 200
compare "N64 input, A held and the stick over" N64 input-cpu.n64 --frames 200 --hold "P1 Gamepad A" --stick 100 -60
compare "GB libbet"          GB  libbet.gb          --frames 200
compare "GB libbet, Start held" GB libbet.gb        --frames 200 --hold Start
compare "GBA arm tests"      GBA gba-arm.gba        --frames 200
compare "GBA arm tests, A held" GBA gba-arm.gba     --frames 200 --hold A
# The PlayStation with nothing in its drive: the BIOS boots, draws its logo and
# settles into the shell. No content, and still a whole machine to compare.
compare "PS1 BIOS shell"     PS1 -                  --frames 400
# An MSX with nothing in its slot boots its own BASIC - a whole machine, and
# the only content this one has, since nobody has a freely redistributable MSX
# cartridge here.
compare "MSX BASIC"          MSX -                  --frames 400

echo
echo "== the machine survives being saved and reloaded =="
rerecord "N64 helloworld-cpu" N64 helloworld-cpu.n64 --frames 130
rerecord "N64 input, A held"  N64 input-cpu.n64      --frames 200 --hold "P1 Gamepad A"
rerecord "GB libbet"          GB  libbet.gb          --frames 200
rerecord "GBA arm tests"      GBA gba-arm.gba        --frames 200
rerecord "PS1 BIOS shell"     PS1 -                  --frames 400
rerecord "MSX BASIC"          MSX -                  --frames 400

echo
echo "== a machine told the same thing twice does not notice =="
input_discipline "N64 helloworld-cpu" N64 helloworld-cpu.n64 --frames 130
input_discipline "N64 input, A held"  N64 input-cpu.n64      --frames 200 --hold "P1 Gamepad A"
input_discipline "GB libbet"          GB  libbet.gb          --frames 200
input_discipline "GBA arm tests"      GBA gba-arm.gba        --frames 200
input_discipline "PS1 BIOS shell"     PS1 -                  --frames 400
input_discipline "MSX BASIC"          MSX -                  --frames 400

# The Neo Geo Pocket is the machine this was written for: ares presses its power
# button itself when the BIOS halts, and it will not boot without a cartridge, so
# the leg can only run for somebody who owns one.
echo
echo "== the same, on content only this developer has =="
with_local discipline NGPC --frames 400
with_local rewind     NGPC 200 --frames 400
with_local rerecord   NGPC --frames 200
# The PC Engine and the SuperGrafx have no freely distributable software either.
with_local compare    PCE  --frames 300
with_local rerecord   PCE  --frames 200
with_local rewind     PCE  120 --frames 300
with_local compare    SGX  --frames 300
with_local rerecord   SGX  --frames 200
with_local rewind     SGX  120 --frames 300

echo
echo "== a machine put back to a frame it has left =="
rewind "N64 helloworld-cpu" N64 helloworld-cpu.n64 60  --frames 130
rewind "N64 input, A held"  N64 input-cpu.n64      120 --frames 200 --hold "P1 Gamepad A"
rewind "GB libbet"          GB  libbet.gb          120 --frames 200
rewind "GBA arm tests"      GBA gba-arm.gba        120 --frames 200
rewind "PS1 BIOS shell"     PS1 -                  200 --frames 400
rewind "MSX BASIC"          MSX -                  200 --frames 400

echo
echo "== the same run twice is the same machine =="
deterministic "N64" N64 helloworld-cpu.n64 --frames 60
deterministic "GB"  GB  libbet.gb          --frames 120
deterministic "GBA" GBA gba-arm.gba        --frames 120
deterministic "PS1" PS1 -                  --frames 300
deterministic "MSX" MSX -                  --frames 300

echo
echo "== the refresh rate is the machine's own =="
# A core that says 60Hz for a machine that runs at 59.7275 makes a recording
# whose sound drifts a second clear of its picture over half an hour, and a
# movie whose length in seconds is wrong by the same amount. Nothing else in
# the gate can see that: every digest is per frame, and a frame is a frame
# whatever rate it is played at.
#
# Two machines here cannot say in time - the Atari 2600 and the WonderSwan
# count the lines their program drew - and DECLARE their rate in machines.h
# instead. This leg is what stops a declared number being wrong: it asks the
# machine what it thinks once it has been running, and the declaration has to
# agree exactly. It can only ask the machines the gate has content for, so the
# declared two are checked by hand against a commercial cartridge; PLAN.md says
# so and says what the numbers were.
refresh_is() {
	leg="$1"; id="$2"; rom="$3"; want="$4"; shift 4
	if [ "$rom" = "-" ]; then set -- "$@"; else set -- --rom "$content/$rom" "$@"; fi
	fw="$(firmware_for "$id")"
	if [ -n "$fw" ]; then
		# the gate skips a machine whose BIOS the developer has not put there,
		# the same way every other leg does
		if [ ! -f "$fw" ]; then echo "SKIP $leg refresh rate (no console BIOS in tests/firmware)"; return; fi
		set -- "$@" --firmware "$fw"
	fi
	line="$("$native" --machine "$id" --quiet --report-refresh "$@" 2>/dev/null | grep '^refresh ')"
	declared="$(echo "$line" | awk '{print $3}')"
	observed="$(echo "$line" | awk '{print $5}')"
	if [ -z "$observed" ]; then
		say_fail "$leg refresh rate" "the core reported nothing"
	elif [ "$observed" != "$want" ]; then
		say_fail "$leg refresh rate" "wanted $want, got $observed"
	elif [ "$declared" != "0/0" ] && [ "$declared" != "$observed" ]; then
		say_fail "$leg refresh rate" "machines.h declares $declared, the machine says $observed"
	else
		if [ "$observed" = "60/1" ] || [ "$observed" = "50/1" ]; then
			say_pass "$leg refresh is $observed, the nominal rate ares hints there"
		else
			say_pass "$leg refresh is $observed, the machine's own and not a flat 60"
		fi
	fi
}
# What each machine's own clock divides out to, and none of them is 60.
refresh_is "GB"  GB  libbet.gb    262144/4389 --frames 120
refresh_is "GBA" GBA gba-arm.gba  262144/4389 --frames 120
# The Nintendo 64 is the exception, and deliberately: ares hints a flat 60 with
# a TODO beside it, and 60 is what mupen and BizHawk record N64 movies at.
refresh_is "N64" N64 helloworld-cpu.n64 60/1 --frames 120
refresh_is "PS1" PS1 -            60/1 --frames 300
refresh_is "MSX" MSX -            183843/3068 --frames 300

echo
echo "== input reaches the machine =="
# A leg that only proved two builds agree would pass just as well with the input
# wire cut. These prove the machine NOTICED - each distinct input has to produce
# a distinct machine.
idle="$("$native" --machine N64 --rom "$content/input-cpu.n64" --quiet --frames 200 | tail -1)"
held="$("$native" --machine N64 --rom "$content/input-cpu.n64" --quiet --frames 200 --hold "P1 Gamepad A" | tail -1)"
other="$("$native" --machine N64 --rom "$content/input-cpu.n64" --quiet --frames 200 --hold "P1 Gamepad Start" | tail -1)"
stick="$("$native" --machine N64 --rom "$content/input-cpu.n64" --quiet --frames 200 --stick 127 0 | tail -1)"
stick2="$("$native" --machine N64 --rom "$content/input-cpu.n64" --quiet --frames 200 --stick 40 0 | tail -1)"
if [ "$idle" != "$held" ] && [ "$held" != "$other" ]; then
	say_pass "N64 buttons (idle, A and Start each make a different machine)"
else
	say_fail "N64 buttons" "idle  $idle
     A     $held
     Start $other"
fi
if [ "$idle" != "$stick" ] && [ "$stick" != "$stick2" ]; then
	say_pass "N64 analogue stick (centre, full and half deflection all differ)"
else
	say_fail "N64 analogue stick" "centre $idle
     full   $stick
     half   $stick2"
fi

gbfw="$(firmware_for GB)"
if [ ! -f "$gbfw" ]; then
	echo "SKIP GB buttons (no console BIOS in tests/firmware)"
	gbidle=skip; gba=skip; gbstart=skip
else
gbidle="$("$native" --machine GB --firmware "$gbfw" --rom "$content/libbet.gb" --quiet --frames 200 | tail -1)"
gba="$("$native" --machine GB --firmware "$gbfw" --rom "$content/libbet.gb" --quiet --frames 200 --hold A | tail -1)"
gbstart="$("$native" --machine GB --firmware "$gbfw" --rom "$content/libbet.gb" --quiet --frames 200 --hold Start | tail -1)"
if ! is_digest "$gbidle"; then
	say_fail "GB buttons" "a run produced no digest: $gbidle"
elif [ "$gbidle" != "$gba" ] && [ "$gba" != "$gbstart" ]; then
	say_pass "GB buttons (idle, A and Start each make a different machine)"
else
	say_fail "GB buttons" "idle  $gbidle
     A     $gba
     Start $gbstart"
fi
fi

echo
echo "== the stick is the byte the movie recorded =="
# A Nintendo 64 movie is written in the signed byte the controller reports,
# which is what mupen and BizHawk record. ares shapes a modern thumbstick
# through a deadzone and an octagonal gate on the way in; patches/ares/0008
# takes that out, and this leg is what says so. The small values matter most:
# before the patch everything under about eight vanished into the deadzone.
stick_exact=1
for pair in "0 0" "1 0" "-1 0" "5 0" "85 -70" "127 127" "-128 -128"; do
	# shellcheck disable=SC2086
	got="$("$native" --machine N64 --rom "$content/input-cpu.n64" --quiet --frames 220 --stick $pair --report-stick | tail -1)"
	want="stick reported x=$(echo "$pair" | cut -d' ' -f1) y=$(echo "$pair" | cut -d' ' -f2)"
	if [ "$got" != "$want" ]; then
		stick_exact=0
		echo "     typed $pair -> $got"
	fi
done
if [ "$stick_exact" -eq 1 ]; then
	say_pass "every stick byte reaches the machine unchanged"
else
	say_fail "every stick byte reaches the machine unchanged" "see above"
fi

echo
echo "== the boot the cartridge is handed =="
# This core has no PIF boot ROM - it is Nintendo's and does not travel here - so
# a Nintendo 64 is booted by PIF::bootHLE, which puts the machine into the state
# the cartridge's own boot code is entitled to find. That claim is only worth
# anything against the real thing, so: boot the same cartridge both ways and
# require the difference to be EXACTLY what an HLE boot cannot reproduce.
#
# Naming the differences rather than counting them is the point. A new one
# appearing is this leg's whole reason to exist, and a table of "known
# differences" that nobody reads would hide it.
pif_rom="$root/tests/firmware/pifNtsc"
if [ ! -f "$pif_rom" ]; then
	echo "SKIP the HLE boot against a real one (no PIF ROM in tests/firmware)"
else
	"$native" --machine N64 --rom "$content/helloworld-cpu.n64" --quiet --frames 200 \
		--boot-probe >"$work/boot-hle.txt" 2>&1
	"$native" --machine N64 --rom "$content/helloworld-cpu.n64" --quiet --frames 200 \
		--boot-probe --pif-rom "$pif_rom" >"$work/boot-real.txt" 2>&1
	grep '^BOOTPROBE' "$work/boot-hle.txt"  | sort > "$work/hle.sorted"
	grep '^BOOTPROBE' "$work/boot-real.txt" | sort > "$work/real.sorted"
	if [ ! -s "$work/hle.sorted" ] || [ ! -s "$work/real.sorted" ]; then
		say_fail "the HLE boot against a real one" "one of the two runs never reached the cartridge"
	else
		# what the machine hands over that an HLE boot cannot have: the cycle
		# counter and the two free-running clocks, the boot ROM's scratch space
		# in RSP instruction memory, the registers its checksum loop left
		# behind, and a serial transfer caught halfway.
		expected="r1 r2 r3 r12 r13 r14 r15 r25 lo hi c9 imem"
		unexpected=""
		for field in $(diff "$work/real.sorted" "$work/hle.sorted" \
			| sed -n 's/^[<>] BOOTPROBE \([^ ]*\) .*/\1/p' | sort -u); do
			case " $expected " in *" $field "*) continue ;; esac
			case "$field" in io) continue ;; esac
			unexpected="$unexpected $field"
		done
		# the two hardware clocks and the mid-transfer serial interface, by address
		for line in $(diff "$work/real.sorted" "$work/hle.sorted" \
			| sed -n 's/^[<>] BOOTPROBE io \([a-z]*\) \([0-9a-f]*\) .*/\1:\2/p' | sort -u); do
			case "$line" in dp:04100010|si:04800018) continue ;; esac
			unexpected="$unexpected $line"
		done
		if [ -z "$unexpected" ]; then
			say_pass "the HLE boot hands over what a real boot hands over"
			echo "     everything but the cycle counter, the boot ROM's scratch and two free-running clocks"
		else
			say_fail "the HLE boot hands over what a real boot hands over" \
				"also differs in:$unexpected"
		fi
	fi
fi

echo
echo "== the picture is the one the hardware draws =="
# PeterLemon's repository ships the picture each program produces on a real
# console. The CPU one is a plain framebuffer, so with the video interface's
# filtering off it should match exactly - and it does, to the pixel. See
# docs/PLAN.md for why the RDP one is not held to the same test yet.
"$native" --machine N64 --rom "$content/helloworld-cpu.n64" --quiet --frames 130 --fast-vi \
	--dump-frame 120 --dump-to "$work/frame.ppm" >/dev/null 2>&1
if out="$(python3 "$here/tests/compare-picture.py" "$content/helloworld-cpu.png" "$work/frame.ppm" 0 2>&1)"; then
	say_pass "N64 helloworld-cpu is pixel-exact against real hardware"
	echo "     $out"
else
	say_fail "N64 helloworld-cpu is pixel-exact against real hardware" "$out"
fi

echo
echo "== the CPU is right, according to somebody else's tests =="
# jsmolka's ARM suite prints its verdict on screen. Reading a picture is a
# roundabout way to run a test suite, and it is the only way a test ROM has to
# talk - so the leg looks for the words rather than for a digest, which also
# means it cannot pass by agreeing with itself.
fw="$root/tests/firmware/gbaBios"
if [ -f "$fw" ]; then
	"$native" --machine GBA --firmware "$fw" --rom "$content/gba-arm.gba" --quiet \
		--frames 300 --dump-frame 280 --dump-to "$work/gba.ppm" >/dev/null 2>&1
	if out="$(python3 "$here/tests/read-verdict.py" "$work/gba.ppm" 2>&1)"; then
		say_pass "GBA passes jsmolka's ARM test suite"
		echo "     it says: $out"
	else
		say_fail "GBA passes jsmolka's ARM test suite" "$out"
	fi
else
	echo "SKIP GBA arm test suite (no console BIOS in tests/firmware)"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
