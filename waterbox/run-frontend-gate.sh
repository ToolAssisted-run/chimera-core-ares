#!/bin/sh
# Runs the package through Chimera itself, headless, and reads back what came
# out of it. This is the leg run-gate.sh cannot be: the gate loads the guest
# directly, and everything between the guest and a video file - the settings a
# project carries, the wizard's file form, the arena Chimera hands the sandbox,
# the rate a movie records - is invisible to it. Four faults lived in that gap
# at once; see docs/PLAN.md, "In the frontend".
#
# It is separate from run-gate.sh because it needs things a public runner does
# not have: a built Chimera, Mono, an X server and ffprobe. It SKIPS rather than
# fails when they are missing, and it says which.
#
#   CHIMERA_BUILD=/path/to/chimera/build ./waterbox/run-frontend-gate.sh
set -u

here="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
root="$(dirname "$here")"
content="$root/tests/content"
firmware="$root/tests/firmware"
native="$root/build/meson-native/waterbox/run-native"
work="${TMPDIR:-/tmp}/ares-frontend-gate.$$"
pass=0; fail=0; skip=0

say_pass() { echo "PASS $1"; pass=$((pass + 1)); }
say_fail() { echo "FAIL $1"; echo "     $2"; fail=$((fail + 1)); }
say_skip() { echo "SKIP $1"; skip=$((skip + 1)); }
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

chimera="${CHIMERA_BUILD:-$root/../../chimera/build}"
package="$root/build/Cores/ares.chimeraCore"
[ -f "$package" ] || package="$chimera/Cores/ares.chimeraCore"

for need in "$native:the reference runner (ninja -C build/meson-native)" \
            "$package:the package (./waterbox/build-package.sh)"; do
	what="${need%%:*}"; why="${need#*:}"
	[ -f "$what" ] || { echo "SKIP everything: no $why"; exit 0; }
done
[ -x "$chimera/ChimeraMono.sh" ] || { echo "SKIP everything: no Chimera at $chimera (set CHIMERA_BUILD)"; exit 0; }
command -v ffprobe >/dev/null 2>&1 || { echo "SKIP everything: no ffprobe"; exit 0; }
command -v xvfb-run >/dev/null 2>&1 || { echo "SKIP everything: no xvfb-run"; exit 0; }
[ -x "$chimera/dll/ffmpeg" ] || { echo "SKIP everything: no ffmpeg in $chimera/dll"; exit 0; }

# What the core itself says, so the frontend's answer is compared against the
# core's rather than against a number typed here.
core_says() {
	id="$1"; rom="$2"; shift 2
	set -- --machine "$id" --quiet --report-refresh --frames "$1"
	[ "$rom" = "-" ] || set -- "$@" --rom "$content/$rom"
	case "$id" in
		GBA) [ -f "$firmware/gbaBios" ] || return 1; set -- "$@" --firmware "$firmware/gbaBios";;
		PS1) [ -f "$firmware/ps1Bios" ] || return 1; set -- "$@" --firmware "$firmware/ps1Bios";;
	esac
	"$native" "$@" 2>/dev/null
}

one() {
	leg="$1"; setting="$2"; id="$3"; rom="$4"; frames="$5"
	said="$(core_says "$id" "$rom" "$frames")" || { say_skip "$leg (no console BIOS in tests/firmware)"; return; }
	want_rate="$(echo "$said" | grep '^refresh ' | awk '{print $5}')"
	want_size="$(echo "$said" | grep -v '^refresh ' | grep -oE '[0-9]+x[0-9]+' | tail -1)"
	[ -n "$want_rate" ] || { say_fail "$leg" "the core reported no refresh rate"; return; }

	w="$work/$setting"
	rm -rf "$w"; mkdir -p "$w/data/Cores" "$w/data/UnpackedCores" "$w/data/Projects"
	cp -r "$chimera/dll" "$w/data/" 2>/dev/null
	cp "$package" "$w/data/Cores/ares-local.chimeraCore"
	set -- --project "$w/p.chimeraProject"
	if [ "$rom" != "-" ]; then cp "$content/$rom" "$w/$rom"; fi
	case "$id" in
		GBA) set -- "$@" --firmware "gbaBios=$firmware/gbaBios";;
		PS1) set -- "$@" --firmware "ps1Bios=$firmware/ps1Bios";;
	esac

	if ! python3 "$here/tests/make-project.py" "$package" "$setting" \
		"$([ "$rom" = "-" ] && echo "" || echo "$w/$rom")" \
		"$w/p.chimeraProject" "$frames" >"$w/mk.err" 2>&1; then
		say_fail "$leg" "the project could not be written: $(tail -1 "$w/mk.err")"; return
	fi

	CHIMERA_DATA_HOME="$w/data" timeout -s KILL 900 xvfb-run -a \
		"$chimera/ChimeraMono.sh" --headless --chromeless "$@" \
		--dump-type=ffmpeg --dump-name="$w/o.avi" --dump-length="$frames" --dump-close \
		>"$w/log.txt" 2>&1

	if [ ! -f "$w/o.avi" ]; then
		why="$(grep -m1 'headless\] text:' "$w/log.txt" | sed 's/.*text: //' | cut -c1-120)"
		[ -n "$why" ] || why="$(tail -3 "$w/log.txt" | tr '\n' ' ' | cut -c1-120)"
		say_fail "$leg" "Chimera wrote no video: $why"; return
	fi

	got_rate="$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$w/o.avi")"
	got_size="$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "$w/o.avi" | tr ',' 'x')"
	got_aud="$(ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate,channels -of csv=p=0 "$w/o.avi")"
	sbrk="$(grep -c 'sbrk heap exhausted' "$w/log.txt")"

	if [ "$got_rate" != "$want_rate" ]; then
		say_fail "$leg" "the recording says $got_rate, the machine says $want_rate"
	elif [ "$got_size" != "$want_size" ]; then
		say_fail "$leg" "the recording is $got_size, the machine draws $want_size"
	elif [ "$got_aud" != "44100,2" ]; then
		say_fail "$leg" "the sound is $got_aud, the package declares 44100,2"
	elif [ "$sbrk" != "0" ]; then
		say_fail "$leg" "the guest ran out of sbrk $sbrk times - raise memoryLayoutMiB"
	else
		say_pass "$leg opens in Chimera and records $got_size at $got_rate"
	fi
}

echo "== the package runs in Chimera and records what the machine drew =="
echo "   Chimera: $chimera"
echo "   package: $package"
echo
one "GB libbet"       gb  GB  libbet.gb          120
one "GBA arm tests"   gba GBA gba-arm.gba        120
one "N64 helloworld"  n64 N64 helloworld-cpu.n64 120
# The PlayStation is deliberately absent. The core will boot one with nothing in
# its drive - that is the gate's PS1 leg - but a project cannot be made that way:
# file_slots.json declares the cartridge slot min:1, and it is one slot shared by
# twenty-one machines, so loosening it would let a Famicom project be made with
# no cartridge in it. A BIOS shell is not a thing anybody records, and the gate
# already compares that machine both ways.
say_skip "PS1 BIOS shell (a project must name a file; see docs/PLAN.md)"

echo
echo "$pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
