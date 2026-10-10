#!/usr/bin/env python3
"""Turns ares' own account of its machines into the two things that must agree.

`gen-machines` builds every machine this core offers and prints what ares says
it is made of. This script turns that into:

  waterbox/machines.inc     the table the guest binds its inputs against
  waterbox/waterbox.config  the machines[] the frontend renders

Both are committed, and the gate regenerates them and diffs, because the wire
format of a controller is what a movie is written in: index 7 has to mean the
same button next year as it did when the movie was recorded. Deriving it from
the emulator rather than typing it twice is what makes that true.

    ./waterbox/gen-config.py [--check]

--check writes nothing and fails if what is committed is not what ares says.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def numbered_ports(machine):
    """The ports a project numbers: all but a built-in keyboard (MSX, ZX
    Spectrum), which is part of the machine and always connected (machine.cpp,
    connectPorts - chimera#141)."""
    return [p for p in machine["ports"] if p["name"] != "Keyboard"]


def first_port(machine):
    """The port a first player's default keys belong to: the first numbered
    one, or the keyboard on a machine that has nothing else (ZX Spectrum)."""
    ports = numbered_ports(machine) or machine["ports"]
    return ports[0] if ports else None


def port_prefix(name):
    """'Controller Port 2' -> 'P2'; anything else keeps a short readable form."""
    if name.startswith("Controller Port "):
        return "P" + name[len("Controller Port "):]
    if name == "Expansion Port":
        return "Exp"
    if name == "Extension Port":
        return "Ext"
    return name.replace(" Port", "")


def wire_safe(name):
    """A control name a movie can actually carry.

    A movie's log key is written '#' for each group of controls and '|' after
    each name in it, so both characters are STRUCTURE and a control whose own
    name contains one cannot round-trip. The MSX's Japanese keyboard has both:
    a key legended with a yen sign, a pipe and a long vowel mark, and another
    legended with a hash. Read back, the hash started a group in the middle of
    a key's name and split it into two controls - so the frontend believed in
    one control more than the machine has, and walked off the end of every
    entry.

    The name here is ours - it is what the package declares and what a movie
    records; the PATH is what binds it to ares - so the separators are spelt
    out instead. The gate regenerates this and diffs it, so the substitution
    cannot quietly drift.
    """
    return name.replace("|", "(pipe)").replace("#", "(hash)")


# Machines whose medium is a tape somebody has to press play on. ares gives each
# of them an ares::Core::Tape node; the ids are this core's, from machines.h.
TAPE_MACHINES = {"ZXS", "MSX", "MSX2"}


# A keyboard is read the way it is printed. ares lists a ZX Spectrum's keys in
# the order of its matrix - CAPS SHIFT, A, Q, 1, 0, P, ENTER, SPACE, then Z, S,
# W, 2... - which is the order the machine scans them in and nobody's idea of
# where a key is: the controller window and every TAStudio column followed it
# (chimera#232). Listed here top row first, left to right; a key this list does
# not name keeps ares' place, after the ones it does, so none can go missing.
KEY_ORDER = {
    "ZXS": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
            "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
            "A", "S", "D", "F", "G", "H", "J", "K", "L", "ENTER",
            "CAPS SHIFT", "Z", "X", "C", "V", "B", "N", "M", "SYMBOL SHIFT", "SPACE BREAK"],
}


def in_reading_order(machine_id, inputs):
    order = KEY_ORDER.get(machine_id)
    if not order:
        return inputs
    place = {name: n for n, name in enumerate(order)}
    return sorted(inputs, key=lambda i: place.get(i["name"], len(order)))


def declare(machine):
    """The machine's buttons and axes, in the order that IS the wire format.

    Console inputs first, in ares' own order, then each port's devices. A name
    is qualified only as far as it needs to be: a handheld's buttons keep their
    own names, and a port's are prefixed by the port, and by the device too when
    the port offers more than one.
    """
    buttons, axes = [], []

    for i in machine["console"]:
        (axes if i["axis"] else buttons).append((wire_safe(i["name"]), i["path"]))

    for port in machine["ports"]:
        prefix = port_prefix(port["name"])
        many = len(port["devices"]) > 1
        for device in port["devices"]:
            for i in in_reading_order(machine["id"], device["inputs"]):
                name = f"{prefix} {device['name']} {i['name']}" if many else f"{prefix} {i['name']}"
                (axes if i["axis"] else buttons).append((wire_safe(name), i["path"]))

    # The tape transport, LAST so that every index above it keeps the number a
    # movie already recorded.
    #
    # ares' tape is a peripheral with a transport, not a node of inputs:
    # Tape::allocate says setSupportPlay(true), and the signal the machine
    # samples is gated on it - "if(!node || !node->playing()) return 0". The
    # tape sits silent until something presses play, which standalone ares
    # offers in its Tape Manager. This core took .tap and .tzx and offered no
    # way to press it, so LOAD "" waited for a signal that never came
    # (chimera#134).
    #
    # It is an INPUT and not a setting because ares serialises `playing` into
    # the savestate: when the tape starts is part of the machine a movie has to
    # reproduce, not a preference of whoever is watching. The paths begin with
    # @ because there is no node to resolve - machine.cpp reads them itself.
    if machine["id"] in TAPE_MACHINES:
        buttons.append(("Tape Play", "@tape/play"))
        buttons.append(("Tape Stop", "@tape/stop"))

    return buttons, axes


def cxx(text):
    """A C++ string literal body. The MSX's Japanese keyboard has a key named
    literally `@ ' "`, which is a fine name for a key and a broken string."""
    return text.replace("\\", "\\\\").replace('"', '\\"')



# What each of ares' settings actually does, said once. Anything not named here
# gets a plain sentence naming the machine; these are the ones where "Fast Boot"
# or "Interframe Blending" does not explain itself, and where getting it wrong
# quietly changes what a movie replays as.
SETTING_TEXT = {
    "Fast Boot": "Skips the console's boot ROM and starts the game at once. The "
        "machine is in a different state when the game starts, so a movie "
        "recorded with this on does not play back with it off.",
    "Interframe Blending": "Mixes each frame with the one before it, the way the LCD screens of"
        " the time blurred movement. The game runs the same. The recorded "
        "picture is different, so a movie's video changes when this is on.",
    "Color Emulation": "Shows the colours the way the console's own screen showed them, and"
        " not the raw values as a modern display would show them. It changes"
        " the recorded picture.",
    "Phosphor": "Mixes each frame with the one before it, the way a television tube "
        "kept its picture for a moment.",
    "Version": "Which revision of the console this is. Revisions of the real "
        "console behave differently in small ways, and a game that depends "
        "on one may act differently on another.",
    "Revision": "Which revision of the console this is. Revisions of the real "
        "console behave differently in small ways, and a game that depends "
        "on one may act differently on another.",
    "Orientation": "Which way the handheld is held. Some games are played with it "
        "turned on its side.",
    "Headphones": "Whether headphones are plugged in. The console's sound is different"
        " with them.",
    "Show Icons": "Draws the handheld's status icons around the picture.",
    # ares names these and says no more. The Neo Geo ones are the switches of
    # the system board (an arcade board has them as DIP switches); the system
    # ROM reads them. The Super Famicom ones are chip revisions and memory size.
    "Settings Mode": "One of the Neo Geo system board's switches. On an arcade board it opens"
                     " the board's own settings screens when the machine starts.",
    "Two Coin Chutes": "One of the Neo Geo system board's switches. It tells the system ROM"
                       " whether the cabinet has one coin slot or two.",
    "Normal Controller": "One of the Neo Geo system board's switches. It tells the system ROM"
                         " that an ordinary controller is connected, and not a mahjong one.",
    "Multiplayer": "One of the Neo Geo system board's switches. It turns on play between"
                   " linked machines.",
    "Free Play": "One of the Neo Geo system board's switches. It lets a game start without"
                 " coins.",
    "Freeze": "One of the Neo Geo system board's switches. On an arcade board it stops the"
              " game while it is set.",
    "Communication ID": "The number this machine has when several are linked together.",
    "Deep Black Boost": "Makes the darkest colours darker, as a television showed them. It"
                        " changes the picture only.",
    "PPU1 Version": "The revision number of the first of the console's two video chips. A"
                    " game can read it.",
    "PPU2 Version": "The revision number of the second of the console's two video chips. A"
                    " game can read it.",
    "VRAM": "How much video memory the console has. A real console has 64 KB.",
}


def setting_key(machine_id, name):
    """The name the package, the settings grid and a movie know a setting by.

    Scoped to the machine on purpose. A Game Boy's Version is a DMG revision and
    a Game Boy Color's is a CGB one - same word, different machine, different
    list - and a movie records the value forever, so a name that could mean two
    things is a name that will one day mean the wrong one."""
    parts = name.replace("-", " ").replace("/", " ").split()
    if not parts:
        return machine_id.lower()
    head = parts[0]
    ident = head[0].lower() + head[1:] + "".join(p[0].upper() + p[1:] for p in parts[1:])
    ident = "".join(c for c in ident if c.isalnum())
    return f"{machine_id.lower()}.{ident}"


def machine_settings(m):
    """The settings ares declares on this machine, as (key, path, decl)."""
    out = []
    for s in (m.get("settings") or []):
        out.append((setting_key(m["id"], s["name"]), s["path"], s))
    return out


def render_inc(machines):
    out = []
    out.append("/* GENERATED by waterbox/gen-config.py from waterbox/machines.json - do not edit.")
    out.append(" *")
    out.append(" * Which node each declared input binds to. The order is the wire format a")
    out.append(" * movie records, so it comes from ares itself (waterbox/gen-machines.cpp)")
    out.append(" * rather than from anybody's typing, and the gate checks it has not moved.")
    out.append(" */")
    out.append("")
    for m in machines:
        if not m["loads"]:
            continue
        buttons, axes = declare(m)
        ident = m["id"].replace("-", "_")
        out.append(f"static const machines::InputDecl kButtons_{ident}[] = {{")
        for name, path in buttons:
            out.append(f'\t{{"{cxx(name)}", "{cxx(path)}"}},')
        out.append("};")
        out.append(f"static const machines::InputDecl kAxes_{ident}[] = {{")
        for name, path in axes:
            out.append(f'\t{{"{cxx(name)}", "{cxx(path)}"}},')
        out.append("};")
        settings = machine_settings(m)
        if settings:
            out.append(f"static const machines::SettingDecl kSettings_{ident}[] = {{")
            for key, path, _ in settings:
                out.append(f'\t{{"{cxx(key)}", "{cxx(path)}"}},')
            out.append("};")
        out.append("")
    out.append("static const machines::MachineInputs kMachineInputs[] = {")
    for m in machines:
        if not m["loads"]:
            continue
        buttons, axes = declare(m)
        ident = m["id"].replace("-", "_")
        b = f"kButtons_{ident}" if buttons else "nullptr"
        a = f"kAxes_{ident}" if axes else "nullptr"
        # what the first port takes when nobody says otherwise
        first = first_port(m)["devices"][0]["name"] if first_port(m) else None
        d = f'"{first}"' if first else "nullptr"
        boots = "true" if m.get("bootsWithoutMedium") else "false"
        settings = machine_settings(m)
        st = f"kSettings_{ident}" if settings else "nullptr"
        out.append(f'\t{{"{m["id"]}", {b}, {len(buttons)}, {a}, {len(axes)}, '
                   f'{st}, {len(settings)}, {d}, {boots}}},')
    out.append("};")
    out.append("")
    return "\n".join(out)


# The human half of a firmware declaration: what to call it and what to say
# about it. The id, the size and the hash come from the file the generator
# actually built the machine with, so the package pins what was verified rather
# than what somebody hoped would work.
FIRMWARE_TEXT = {
    "megaCdBios": (
        "Mega CD / Sega CD BIOS",
        "The BIOS of the Mega CD disc drive. It is Sega's and you have to "
        "supply it. A disc only starts with the BIOS of its own region. A "
        "USA game needs the Sega CD BIOS and a Japanese game the Japanese "
        "Mega CD BIOS. The Model 1 and Model 2 versions both work. They show"
        " a different menu, and games behave the same on both.",
        "bios_CD_U.bin",
    ),
    "pceSystemCard": (
        "PC Engine System Card",
        "The card that goes in the card slot of a CD-ROM2 or a TurboDuo. "
        "Without it the machine cannot read a disc. It is NEC's and you have"
        " to supply it. A Super CD-ROM2 game needs System Card 3.0 of its "
        "region (the Japanese card for a Japanese disc, the American card "
        "for a US disc). An early CD-ROM2 game works with any card. A Games "
        "Express disc needs the Games Express card. The card shows its name "
        "when the machine starts. A disc that stops at PUSH RUN BUTTON "
        "usually needs a newer card.",
        "syscard3.pce",
    ),
    "sfcIpl": (
        "Super Famicom IPL (SPC700 boot ROM)",
        "The 64 bytes the SNES sound processor runs when the console is "
        "switched on. The main processor uses them to load sound code, so "
        "nothing plays without this file. It is Nintendo's and you have to "
        "supply it. Games with an extra chip in the cartridge (the DSP-1 in "
        "Pilotwings, the CX4 in Mega Man X2) also need that chip's ROM. This"
        " core does not ask for those yet.",
        "ipl.rom",
    ),
    "m32xVector": (
        "Mega 32X vector table",
        "The 256-byte table the Mega Drive's 68000 processor starts from on "
        "a 32X. It is Sega's and you have to supply it. The three 32X files "
        "come from the same dump. The letter in each name says which "
        "processor runs it: G for the Mega Drive side, M and S for the two "
        "SH-2 processors.",
        "32X_G_BIOS.bin",
    ),
    "m32xBootM": (
        "Mega 32X master SH-2 boot ROM",
        "The boot ROM of the first of the 32X's two SH-2 processors (2 KB).",
        "32X_M_BIOS.bin",
    ),
    "m32xBootS": (
        "Mega 32X slave SH-2 boot ROM",
        "The boot ROM of the second of the 32X's two SH-2 processors (1 KB).",
        "32X_S_BIOS.bin",
    ),
    "msx2Main": (
        "MSX2 main BIOS",
        "The MSX2's main ROM (32 KB). It belongs to Microsoft and ASCII, and"
        " you have to supply it. An MSX2 also needs its sub ROM.",
        "msx2.rom",
    ),
    "msx2Sub": (
        "MSX2 sub ROM",
        "The MSX2's sub ROM (16 KB). It holds the extended BIOS that the "
        "main ROM calls.",
        "msx2ext.rom",
    ),
    "gbBoot": (
        "Game Boy boot ROM",
        "The Game Boy's boot ROM (256 bytes). It scrolls the logo and checks"
        " the cartridge. The core runs it and the game starts from the state"
        " it leaves, so it is required. It is Nintendo's and you have to "
        "supply it. The boot ROM of any original Game Boy revision works. "
        "DMG-CPU A is the one ares expects for a plain Game Boy.",
        "dmg_boot.bin",
    ),
    "gbcBoot": (
        "Game Boy Color boot ROM",
        "The Game Boy Color's boot ROM (2 KB). It is larger than the Game "
        "Boy's because it also chooses the colours an original Game Boy "
        "cartridge is shown in.",
        "cgb_boot.bin",
    ),
    "wsBoot": (
        "WonderSwan boot ROM",
        "The WonderSwan's boot ROM. It shows the startup animation, sets the"
        " machine up and reads the owner's name from the console's memory.",
        "ws_boot.rom",
    ),
    "wscBoot": (
        "WonderSwan Color boot ROM",
        "The WonderSwan Color's boot ROM. It is a different file from the "
        "WonderSwan's.",
        "wsc_boot.rom",
    ),
    "zxsBios": (
        "ZX Spectrum BIOS",
        "The ZX Spectrum's ROM (16 KB). It holds the BASIC language, the "
        "editor and the tape loader, so the machine cannot run without it. "
        "Amstrad owns it and allows it to be distributed with emulators. "
        "This core does not include it, and you have to supply it.",
        "spectrum.rom",
    ),
    "gbaBios": (
        "Game Boy Advance BIOS",
        "The Game Boy Advance's boot ROM (16 KB). The real one is needed. "
        "The core runs it, games call functions in it and its timing is part"
        " of the machine. It is Nintendo's and you have to supply it.",
        "GBA_bios.rom",
    ),
    "cvBios": (
        "ColecoVision BIOS",
        "The ColecoVision's boot ROM (8 KB). It holds the console's title "
        "screen and its cartridge checks, and nothing runs without it.",
        "Coleco_Bios.bin",
    ),
    "a52Bios": (
        "Atari 5200 BIOS",
        "The Atari 5200's boot ROM. It must be exactly 2048 bytes, and a "
        "file of any other size is refused.",
        "[BIOS] Atari 5200 (USA).a52",
    ),
    "ngBios": (
        "Neo Geo AES BIOS",
        "The boot ROM of the Neo Geo home console (128 KB). The file is "
        "`neo-epo.bin`. It is usually found inside an `aes.zip` BIOS set and"
        " has to be taken out of the zip first.",
        "neo-epo.bin",
    ),
    "ngpBios": (
        "Neo Geo Pocket BIOS",
        "The boot ROM of the original black-and-white Neo Geo Pocket (64 "
        "KB). Its name usually carries the year 1998, which tells it apart "
        "from the Color one.",
        "SNK Neo-Geo Pocket BIOS (1998)(SNK)(en-ja).bin",
    ),
    "ngpcBios": (
        "Neo Geo Pocket Color BIOS",
        "The boot ROM of the Neo Geo Pocket Color (64 KB). It is a different"
        " ROM from the black-and-white machine's, and one cannot be used in "
        "place of the other.",
        "SNK Neo-Geo Pocket Color BIOS (1999)(SNK)(en-ja).bin",
    ),
    "ps1Bios": (
        "PlayStation BIOS",
        "The PlayStation's boot ROM (512 KB). It is the console's operating "
        "system. Games call it all the time and nothing runs without it. It "
        "is Sony's and you have to supply it. Any retail BIOS works, and "
        "they differ by region and revision. The one named here is the one "
        "this core was tested with. A project records which one it used.",
        "PSX_4.1(A).bin",
    ),
    "msxBios": (
        "MSX BIOS",
        "The MSX's BIOS and BASIC ROM (32 KB). The ROM of any MSX machine "
        "works, and they differ by region. The one named here is the one "
        "this core was tested with. A project records which one it used.",
        "MSX.rom",
    ),
}


# A BIOS that is not ONE file.
#
# A Mega CD BIOS is region specific and a disc will not boot on the wrong one; a
# PC Engine CD's System Card decides what a disc can do at all, and different
# discs want different cards. Pinning one hash for these would have shipped a
# Sega CD that only runs American discs and a TurboDuo that only runs Japanese
# Super CD-ROM2 ones.
#
# The package's own decision language already has the shape for it, and says so:
# variants are separate entries with the SAME id and disjoint conditions,
# selected by a sync setting. So each variant is declared and hashed separately
# and one setting picks between them; the guest opens the id and never knows.
#
# Each variant's file is the developer's own copy at tests/firmware/<id>.<value>
# - a variant with no file there is not declared, so a developer who has one
# region's BIOS ships a package offering that region.
FIRMWARE_VARIANTS = {
    "megaCdBios": ("mcd.bios", "Which Mega CD BIOS",
        "Which region's Mega CD BIOS to use. A disc only starts with the "
        "BIOS of its own region. A USA game needs the Sega CD BIOS, a "
        "Japanese game the Japanese Mega CD BIOS and a European game the "
        "European one. The Model 1 and Model 2 versions show a different "
        "menu, and games behave the same on both.", [
        ("usa", "SCD_m2_us_200.bin"),
        ("japan", "MCD_jp_100s.bin"),
        ("europe", "MCD_eu_200.bin"),
    ]),
    "pceSystemCard": ("pcecd.card", "Which System Card",
        "Which System Card is in the card slot. The card is what reads the "
        "disc. A Super CD-ROM2 game needs System Card 3.0 of its own region."
        " An early CD-ROM2 game works with any card. A Games Express disc "
        "needs the Games Express card. The card shows its name when the "
        "machine starts. A disc that stops at PUSH RUN BUTTON usually needs "
        "a newer card.", [
        ("system3-jp", "syscard3.pce"),
        ("system3-us", "syscard3u.pce"),
        ("system2-jp", "syscard2.pce"),
        ("system2-us", "syscard2u.pce"),
        ("system1-jp", "syscard1.pce"),
        ("games-express", "gecard.pce"),
    ]),
}


def firmware_variant_settings(firmware, declared_ids):
    """The settings that pick between a BIOS's variants, scoped to the machines
    that ask for that BIOS.

    Taken from the DECLARATION that was just rendered rather than from the files
    on disk, because they are not the same thing: a machine without the dumps
    keeps whatever the package already declared (see render_firmware), and a
    setting derived from the disk would then offer nothing to choose. That is
    exactly the shape CI runs in."""
    out = []
    for fwid, (key, display, description, variants) in FIRMWARE_VARIANTS.items():
        order = [v[0] for v in variants]
        declared = []
        for entry in firmware:
            if entry["id"] != fwid:
                continue
            for cond in (entry.get("requiredWhen") or {}).get("all") or []:
                if cond.get("setting") == key and cond.get("is") in order:
                    declared.append(cond["is"])
        if len(declared) < 2 or fwid not in declared_ids:
            continue  # one variant needs no choosing, and neither does none
        declared.sort(key=order.index)
        out.append({
            "name": key,
            "display": display,
            "description": description,
            "type": "enum",
            "default": declared[0],
            "options": declared,
            "when": sorted(declared_ids[fwid]),
        })
    return out


def render_firmware(machines, already=None):
    """What the frontend asks the user for, and refuses to start a project without.

    The size and the hash come from the developer's own copy under
    tests/firmware, so the package pins the BIOS that was actually verified. A
    machine without one - a CI runner, say - keeps whatever is already declared,
    because re-deriving it is not possible and discarding it would be worse.
    """
    import hashlib
    known = {(f["id"], f.get("name")): f for f in (already or [])}
    # WHICH machines want each BIOS, not just the first. A Mega CD 32X wants the
    # Mega CD's BIOS and the 32X's three, and every one of them was already
    # declared for another machine - so declaring each id once and naming only
    # the machine that got there first left the Mega CD 32X asking for nothing
    # and failing to load with nothing to say about it.
    wants = {}
    for m in machines:
        for fw in m.get("firmware") or []:
            wants.setdefault(fw["id"], []).append(m["id"].lower())

    out = []
    for fwid, machineIds in wants.items():
        display, description, name = FIRMWARE_TEXT[fwid]
        base = {"setting": "machine", "in": sorted(set(machineIds))}

        def declare(path, entry_name, extra=None, missing_key=None):
            if not os.path.exists(path):
                cached = known.get((fwid, entry_name))
                if cached is not None:
                    out.append(cached)
                    return True
                return False
            blob = open(path, "rb").read()
            entry = {
                "id": fwid,
                "display": display,
                "description": description,
                "size": len(blob),
                "sha1": hashlib.sha1(blob).hexdigest().upper(),
                "name": entry_name,
                "requiredWhen": {"all": [base, extra]} if extra else base,
            }
            out.append(entry)
            return True

        variants = FIRMWARE_VARIANTS.get(fwid)
        if variants is not None:
            key, _d, _desc, forms = variants
            declared = 0
            present = [v for v in forms
                       if os.path.exists(os.path.join(HERE, "..", "tests", "firmware",
                                                      f"{fwid}.{v[0]}"))]
            for value, filename in forms:
                path = os.path.join(HERE, "..", "tests", "firmware", f"{fwid}.{value}")
                # One variant present needs no setting to choose it, so it is
                # declared plainly and the package stays simple.
                extra = {"setting": key, "is": value} if len(present) > 1 else None
                if declare(path, filename, extra):
                    declared += 1
            if declared:
                continue
            # No variant file: fall through and try the plain one, so a tree
            # that predates this still generates.

        path = os.path.join(HERE, "..", "tests", "firmware", fwid)
        if not declare(path, name):
            raise SystemExit(
                f"{fwid}: machines.json describes a machine built with this BIOS, and\n"
                f"neither tests/firmware/{fwid} nor an existing declaration is there to\n"
                f"take its size and hash from."
            )
    return out


# ---- what the controls and the system are called ----
# The frontend keeps no table of these: a core says what its own are called.
# MNEMONICS is the letter each button writes into a movie's text and heads its
# input column with, by the button's name - whole, or without its player ("P2
# Up" is found under "Up"), so one line serves every pad. AXIS_HEADERS is the
# short header of each axis's column. (An entry is read by position: a letter
# may change and no movie made before it is harmed.)
MNEMONICS = {
    "Gamepad Up": "U", "Gamepad Down": "D", "Gamepad Left": "L", "Gamepad Right": "R",
    "Gamepad B": "B", "Gamepad A": "A", "Gamepad C-Up": "^", "Gamepad C-Down": "v",
    "Gamepad C-Left": "<", "Gamepad C-Right": ">", "Gamepad L": "l", "Gamepad R": "r",
    "Gamepad Z": "Z", "Gamepad Start": "S", "Mouse Left": "1", "Mouse Right": "2", "Reset": "r",
    "Microphone": "M", "Up": "U", "Down": "D", "Left": "L", "Right": "R", "B": "B", "A": "A",
    "Select": "s", "Start": "S", "C": "C", "Ext Up": "U", "Ext Down": "D", "Ext Left": "L",
    "Ext Right": "R", "Ext A": "A", "Ext B": "B", "Ext C": "C", "Ext Start": "S", "Pause": "p",
    "1": "1", "2": "2", "Left Difficulty": "<", "Right Difficulty": ">", "TV Type": "T",
    "Fire": "F", "UP [B]": "U", "DOWN [C]": "D", "LEFT [A]": "L", "RIGHT [D]": "R",
    "ACTION [E]": "A", "3": "3", "4": "4", "5": "5", "6": "6", "7": "7", "8": "8", "9": "9",
    "10": "a", "11": "b", "12": "c", "13": "d", "14": "e", "Y1": "1", "Y2": "2", "Y3": "3",
    "Y4": "4", "X1": "U", "X2": "R", "X3": "D", "X4": "L", "Volume": "V", "Power": "P",
    "Keyboard CAPS SHIFT": "^", "Keyboard A": "A", "Keyboard Q": "Q", "Keyboard 1": "1",
    "Keyboard 0": "0", "Keyboard P": "P", "Keyboard ENTER": "<", "Keyboard SPACE BREAK": "_",
    "Keyboard Z": "Z", "Keyboard S": "S", "Keyboard W": "W", "Keyboard 2": "2", "Keyboard 9": "9",
    "Keyboard O": "O", "Keyboard L": "L", "Keyboard SYMBOL SHIFT": "$", "Keyboard X": "X",
    "Keyboard D": "D", "Keyboard E": "E", "Keyboard 3": "3", "Keyboard 8": "8", "Keyboard I": "I",
    "Keyboard K": "K", "Keyboard M": "M", "Keyboard C": "C", "Keyboard F": "F", "Keyboard R": "R",
    "Keyboard 4": "4", "Keyboard 7": "7", "Keyboard U": "U", "Keyboard J": "J", "Keyboard N": "N",
    "Keyboard V": "V", "Keyboard G": "G", "Keyboard T": "T", "Keyboard 5": "5", "Keyboard 6": "6",
    "Keyboard ": "?",
    "Keyboard Y": "Y", "Keyboard H": "H", "Keyboard B": "B", "Tape Play": ">", "Tape Stop": "#",
    "L": "l", "R": "r", "*": "*", "0": "0", "(hash)": "(", "Keyboard 0 わ を": "0",
    "Keyboard 1 ! ぬ": "!", "Keyboard 2 \" ふ": "\"", "Keyboard 3 (hash) あ ぁ": "(",
    "Keyboard 4 $ う ぅ": "$", "Keyboard 5 % え ぇ": "%", "Keyboard 6 & お ぉ": "&",
    "Keyboard 7 ’ や ゃ": "7", "Keyboard 8 ( ゆ ゅ": "(", "Keyboard 9 ) よ ょ": ")",
    "Keyboard - = ほ": "=", "Keyboard ^ ~ へ": "~", "Keyboard ¥ (pipe) ー": "(",
    "Keyboard @ ‘ \"": "\"", "Keyboard [ { 。": "{", "Keyboard ; + れ": "+", "Keyboard : * け": "*",
    "Keyboard ] } む": "}", "Keyboard , < ね `": "`", "Keyboard . > る 。": ">",
    "Keyboard / ? め .": "?", "Keyboard - ろ": "-", "Keyboard A ち": "A", "Keyboard B こ": "B",
    "Keyboard C そ": "C", "Keyboard D し": "D", "Keyboard E い ぃ": "E", "Keyboard F は": "F",
    "Keyboard G き": "G", "Keyboard H く": "H", "Keyboard I に": "I", "Keyboard J ま": "J",
    "Keyboard K の": "K", "Keyboard L り": "L", "Keyboard M も": "M", "Keyboard N み": "N",
    "Keyboard O ら": "O", "Keyboard P せ": "P", "Keyboard Q た": "Q", "Keyboard R す": "R",
    "Keyboard S と": "S", "Keyboard T か": "T", "Keyboard U な": "U", "Keyboard V ひ": "V",
    "Keyboard W て": "W", "Keyboard X さ": "X", "Keyboard Y ん": "Y", "Keyboard Z つ っ": "Z",
    "Keyboard SHIFT": "S", "Keyboard CTRL": "C", "Keyboard GRAPH": "G", "Keyboard CAPS": "C",
    "Keyboard かな": "K", "Keyboard F1 F6": "F", "Keyboard F2 F7": "F", "Keyboard F3 F8": "F",
    "Keyboard F4 F9": "F", "Keyboard F5 F10": "F", "Keyboard ESC": "E", "Keyboard TAB": "T",
    "Keyboard STOP": "S", "Keyboard BS": "B", "Keyboard SELECT": "S", "Keyboard RETURN": "R",
    "Keyboard SPACE": "S", "Keyboard CLS/HOME": "C", "Keyboard INS": "I", "Keyboard DEL": "D",
    "Keyboard ←": "K", "Keyboard ↑": "K", "Keyboard ↓": "K", "Keyboard →": "K", "Keyboard *": "*",
    "Keyboard +": "+", "Keyboard /": "/", "Keyboard -": "-", "Keyboard ,": ",", "Keyboard .": "K",
    "Keyboard 実行": "K", "Keyboard 取消": "K", "Cross": "X", "Circle": "O", "Square": "Q",
    "Triangle": "T", "L1": "l", "L2": "[", "R1": "r", "R2": "]", "Top Fire": "F",
    "Bottom Fire": "f", "Option": "O", "Debugger": "d", "D": "d", "Y": "Y", "X": "X",
    "Controller Up": "U", "Controller Down": "D", "Controller Left": "L", "Controller Right": "R",
    "Controller II": "2", "Controller I": "1", "Controller Select": "S", "Controller Run": "r",
}
# BUTTON_HEADERS is what heads a button's column where its letter does not tell
# it from its neighbours (chimera#225): a keyboard has more keys than there are
# characters worth reading, and an MSX has five keys whose letter is F. It is
# what a person reads in TAStudio; the movie's text still carries the letter.
# By the button's whole name, per machine - the Spectrum's row of digits is
# "Keyboard 1" and so is an MSX's numeric pad. A button not named here is
# headed by its letter, which is right for every pad and for a keyboard's
# letters. One to eight characters of ASCII, and no two of one machine alike.
BUTTON_HEADERS = [
    # the tape deck, on every machine that has one
    (None, {"Tape Play": "TPLAY", "Tape Stop": "TSTOP"}),
    (("ZXS",), {
        "Keyboard CAPS SHIFT": "CAPS", "Keyboard SYMBOL SHIFT": "SYMB",
        "Keyboard ENTER": "ENTER", "Keyboard SPACE BREAK": "SPACE",
    }),
    # the Japanese layout: a key is headed by the first thing printed on it
    (("MSX", "MSX2"), {
        "Keyboard 1 ! ぬ": "1", "Keyboard 2 \" ふ": "2", "Keyboard 3 (hash) あ ぁ": "3",
        "Keyboard 4 $ う ぅ": "4", "Keyboard 5 % え ぇ": "5", "Keyboard 6 & お ぉ": "6",
        "Keyboard 8 ( ゆ ゅ": "8", "Keyboard 9 ) よ ょ": "9", "Keyboard - = ほ": "-",
        "Keyboard ^ ~ へ": "^", "Keyboard ¥ (pipe) ー": "YEN", "Keyboard @ ‘ \"": "@",
        "Keyboard [ { 。": "[", "Keyboard ; + れ": ";", "Keyboard : * け": ":",
        "Keyboard ] } む": "]", "Keyboard , < ね `": ",", "Keyboard . > る 。": ".",
        "Keyboard / ? め .": "/", "Keyboard - ろ": "_",
        "Keyboard SHIFT": "SHIFT", "Keyboard CTRL": "CTRL", "Keyboard GRAPH": "GRAPH",
        "Keyboard CAPS": "CAPS", "Keyboard かな": "KANA",
        "Keyboard F1 F6": "F1", "Keyboard F2 F7": "F2", "Keyboard F3 F8": "F3",
        "Keyboard F4 F9": "F4", "Keyboard F5 F10": "F5",
        "Keyboard ESC": "ESC", "Keyboard TAB": "TAB", "Keyboard STOP": "STOP",
        "Keyboard BS": "BS", "Keyboard SELECT": "SELECT", "Keyboard RETURN": "RETURN",
        "Keyboard SPACE": "SPACE", "Keyboard CLS/HOME": "HOME", "Keyboard INS": "INS",
        "Keyboard DEL": "DEL", "Keyboard ←": "LEFT", "Keyboard ↑": "UP",
        "Keyboard ↓": "DOWN", "Keyboard →": "RIGHT",
        # the numeric pad
        "Keyboard *": "N*", "Keyboard +": "N+", "Keyboard /": "N/", "Keyboard -": "N-",
        "Keyboard ,": "N,", "Keyboard .": "N.",
        "Keyboard 0": "N0", "Keyboard 1": "N1", "Keyboard 2": "N2", "Keyboard 3": "N3",
        "Keyboard 4": "N4", "Keyboard 5": "N5", "Keyboard 6": "N6", "Keyboard 7": "N7",
        "Keyboard 8": "N8", "Keyboard 9": "N9",
        "Keyboard 実行": "EXEC", "Keyboard 取消": "CANCEL",
    }),
]
AXIS_HEADERS = {
    "P1 Gamepad X-Axis": "P1GXA", "P1 Gamepad Y-Axis": "P1GYA", "P1 Mouse X": "mX",
    "P1 Mouse Y": "mY", "P2 Gamepad X-Axis": "P2GXA", "P2 Gamepad Y-Axis": "P2GYA",
    "P2 Mouse X": "mX", "P2 Mouse Y": "mY", "P3 Gamepad X-Axis": "P3GXA",
    "P3 Gamepad Y-Axis": "P3GYA", "P3 Mouse X": "mX", "P3 Mouse Y": "mY",
    "P4 Gamepad X-Axis": "P4GXA", "P4 Gamepad Y-Axis": "P4GYA", "P4 Mouse X": "mX",
    "P4 Mouse Y": "mY", "P1 X-Axis": "P1XA", "P1 Y-Axis": "P1YA", "P2 X-Axis": "P2XA",
    "P2 Y-Axis": "P2YA", "P3 X-Axis": "P3XA", "P3 Y-Axis": "P3YA", "P4 X-Axis": "P4XA",
    "P4 Y-Axis": "P4YA",
}
SYSTEM_NAMES = {
    "N64": "Nintendo 64", "NES": "Nintendo Entertainment System", "GEN": "Mega Drive / Genesis",
    "SMS": "Master System", "GG": "Game Gear", "SG": "SG-1000", "A26": "Atari 2600",
    "MYV": "MyVision", "GB": "Game Boy", "GBC": "Game Boy Color", "WS": "WonderSwan",
    "WSC": "WonderSwan Color", "ZXS": "ZX Spectrum", "GBA": "Game Boy Advance",
    "CV": "ColecoVision", "MSX": "MSX", "PS1": "PlayStation", "A52": "Atari 5200",
    "NGP": "Neo Geo Pocket", "NGPC": "Neo Geo Pocket Color", "NG": "Neo Geo AES",
    "SFC": "Super Famicom / SNES", "32X": "Mega Drive 32X", "MSX2": "MSX2",
    "MCD": "Mega CD / Sega CD", "PCE": "PC Engine / TurboGrafx-16", "SGX": "SuperGrafx",
    "MCD32X": "Mega CD 32X / Sega CD 32X", "PCECD": "PC Engine CD",
}


def _bare(name):
    """A control's name without its player: "P2 Up" -> "Up"."""
    head, _, rest = name.partition(" ")
    return rest if rest and head[:1] == "P" and head[1:].isdigit() else name


def mnemonics_for(buttons):
    """The "mnemonics" of an input declaration: a letter for every one of its
    buttons, and for nothing else. A button nobody gave a letter stops the
    build - the engine would give it its rule's guess, and two columns of one
    pad would share a letter with nobody having decided it."""
    out = {}
    for b in buttons:
        key = b if b in MNEMONICS else _bare(b)
        if key not in MNEMONICS:
            raise SystemExit("no mnemonic for the button %r (MNEMONICS in %s)" % (b, __file__))
        out[key] = MNEMONICS[key]
    return out


def button_headers_for(machine_id, buttons, letters):
    """The "headers" of an input declaration: the buttons of this machine that
    BUTTON_HEADERS names. One that is not a header, or that heads two columns
    of the machine, stops the build - the engine would drop the first without
    a word and the second tells nothing apart."""
    named = {}
    for machines, headers in BUTTON_HEADERS:
        if machines is None or machine_id in machines:
            named.update(headers)
    out = {}
    for b in buttons:
        h = named.get(b)
        if h is None:
            continue
        if not (1 <= len(h) <= 8) or h != h.strip() or any(not (" " <= c < "\x7f") for c in h):
            raise SystemExit("%r is not a header for %r (BUTTON_HEADERS in %s)" % (h, b, __file__))
        out[b] = h
    heads = {}
    for b in buttons:
        if b.split(" ")[0][:1] == "P" and b.split(" ")[0][1:].isdigit():
            continue  # a pad's columns are a player's own, and are not headed here
        h = out.get(b, letters.get(b, letters.get(_bare(b))))
        if (b in out or heads.get(h, b) in out) and h in heads:
            raise SystemExit("%s: %r heads both %r and %r (BUTTON_HEADERS in %s)"
                             % (machine_id, h, heads[h], b, __file__))
        heads.setdefault(h, b)
    return out


def with_headers(axes):
    """The axes with their column headers; an axis nobody named stops the build."""
    missing = [a["name"] for a in axes if a["name"] not in AXIS_HEADERS]
    if missing:
        raise SystemExit("no header for the axes %s (AXIS_HEADERS in %s)" % (missing, __file__))
    return [dict(a, header=AXIS_HEADERS[a["name"]]) for a in axes]


def render_machines(machines):
    """The machines[] block of waterbox.config."""
    result = []
    for m in machines:
        if not m["loads"]:
            continue
        buttons, axes = declare(m)
        entry = {
            "id": m["id"],
            "label": m["label"],
            "when": [m["id"].lower()],
            "input": {
                "name": f"{m['label']} Controller",
                "buttons": [n for n, _ in buttons],
                "mnemonics": mnemonics_for([n for n, _ in buttons]),
            },
            "virtualWidth": m["virtualWidth"],
            "virtualHeight": m["virtualHeight"],
            "extensions": {"." + e: m["id"] for e in m["extensions"].split()},
        }
        headers = button_headers_for(m["id"], [n for n, _ in buttons], entry["input"]["mnemonics"])
        if headers:
            entry["input"]["headers"] = headers
        if axes:
            entry["input"]["axes"] = with_headers([
                {"name": n, "min": -128, "max": 127, "neutral": 0} for n, _ in axes
            ])
        result.append(entry)
    return result


# What a button is bound to by default, by its own name. The frontend ships no
# bindings: a package that declares a controller says how it is played, and
# thirteen controllers is more than anybody should type twice.
#
# Only the FIRST port (or a handheld's own buttons) is bound, which is BizHawk's
# choice and the right one - a second pad is something you plug in deliberately,
# and the keys it would take are the ones player one already has.
DEFAULT_KEYS = {
    "Up": "Up, J1 POV1U, X1 DpadUp",
    "Down": "Down, J1 POV1D, X1 DpadDown",
    "Left": "Left, J1 POV1L, X1 DpadLeft",
    "Right": "Right, J1 POV1R, X1 DpadRight",
    "A": "X, J1 B1, X1 A",
    "B": "Z, J1 B3, X1 X",
    "C": "C, J1 B4, X1 Y",
    "X": "S, J1 B4, X1 Y",
    "Y": "A, J1 B2, X1 B",
    "Z": "LeftShift, J1 B7, X1 LeftTrigger",
    "Start": "Enter, J1 B10, X1 Start",
    "Select": "Space, J1 B9, X1 Back",
    "Mode": "Tab",
    "Run": "Enter, J1 B10, X1 Start",
    "L": "Q, J1 B5, X1 LeftShoulder",
    "R": "W, J1 B6, X1 RightShoulder",
    "C-Up": "I, X1 RStickUp",
    "C-Down": "K, X1 RStickDown",
    "C-Left": "J, X1 RStickLeft",
    "C-Right": "L, X1 RStickRight",
    "1": "Z, J1 B3, X1 X",
    "2": "X, J1 B1, X1 A",
    # A WonderSwan is held two ways round: X is the pad you play with, Y the
    # second cross the machine gains when it is turned on its side.
    "X1": "Up, X1 DpadUp",
    "X2": "Right, X1 DpadRight",
    "X3": "Down, X1 DpadDown",
    "X4": "Left, X1 DpadLeft",
    "Y1": "T",
    "Y2": "H",
    "Y3": "G",
    "Y4": "F",
    "Fire": "X, J1 B1, X1 A",
    "Trigger": "X, J1 B1, X1 A",
}

# The analogue axes of the first pad.
DEFAULT_AXES = {
    "X-Axis": {"Value": "X1 LStickX", "Mult": 1.0, "Deadzone": 0.1},
    "Y-Axis": {"Value": "X1 LStickY", "Mult": 1.0, "Deadzone": 0.1},
}


# A machine whose controller IS a keyboard is played on the keyboard in front
# of it: each key on the PC key that carries the same letter, and the two
# shifts where a hand expects them. The table above is a gamepad's - it put a
# Spectrum's A on the PC's X, because X is where a pad's A button goes, bound
# seven keys that happened to share a pad button's name, and left the other
# thirty-three with nothing (chimera#232).
def _letters_and_digits():
    keys = {c: c for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"}
    keys.update({d: "Number" + d for d in "0123456789"})
    return keys


KEYBOARD_KEYS = {
    "ZXS": dict(_letters_and_digits(), **{
        "ENTER": "Enter",
        "SPACE BREAK": "Space",
        "CAPS SHIFT": "Shift, LeftShift",
        "SYMBOL SHIFT": "Ctrl, LeftCtrl",
    }),
}


def render_keybinds(machines):
    """Default bindings for every machine's controller."""
    trollers, analog = {}, {}
    for m in machines:
        if not m["loads"]:
            continue
        buttons, axes = declare(m)
        name = f"{m['label']} Controller"

        if m["id"] in KEYBOARD_KEYS:
            keys = KEYBOARD_KEYS[m["id"]]
            trollers[name] = {declared: keys[path.rsplit("/", 1)[-1]]
                              for declared, path in buttons if path.rsplit("/", 1)[-1] in keys}
            continue

        # Only what a first player touches: a console's own buttons, and the
        # first port's. Anything further along shares keys it should not.
        first_port_name = port_prefix(first_port(m)["name"]) if first_port(m) else None
        bound = {}
        for declared, path in buttons:
            leaf = path.rsplit("/", 1)[-1]
            if leaf not in DEFAULT_KEYS:
                continue
            owner = declared.split(" ")[0]
            if m["ports"] and owner != first_port_name:
                continue
            bound.setdefault(DEFAULT_KEYS[leaf], declared)
        # setdefault above keeps the FIRST button that wants a key, so a port's
        # second device cannot steal one from the first.
        trollers[name] = {v: k for k, v in bound.items()}

        bound_axes = {}
        for declared, path in axes:
            leaf = path.rsplit("/", 1)[-1]
            if leaf in DEFAULT_AXES and (not m["ports"] or declared.split(" ")[0] == first_port_name):
                bound_axes.setdefault(leaf, declared)
        if bound_axes:
            analog[name] = {declared: DEFAULT_AXES[leaf] for leaf, declared in bound_axes.items()}

    return {
        "_comment": [
            "GENERATED by waterbox/gen-config.py - do not edit.",
            "Default bindings for the controllers this package declares. The frontend ships none of",
            "its own: a package that declares a controller says how it is played by default, and",
            "Chimera uses that for a controller the user's config has never seen. The user's own",
            "bindings always win, and the controller config's Defaults button comes back here.",
            "Only player one is bound - a second pad is something you plug in deliberately, and the",
            "keys it would take are the ones player one already has.",
        ],
        "AllTrollers": trollers,
        "AllTrollersAutoFire": {},
        "AllTrollersAnalog": analog,
    }


def check_against(fresh_path, committed):
    """Does what ares says now match what was committed?

    Only the machines the fresh run could BUILD are compared: a runner with no
    console BIOSes cannot describe the machines that need one, and refusing to
    check anything because of that would be worse than checking the rest.
    """
    fresh = {m["id"]: m for m in json.load(open(fresh_path))["machines"]}
    known = {m["id"]: m for m in committed}
    checked, wrong, skipped = 0, [], []
    for id, m in fresh.items():
        if not m["loads"]:
            skipped.append(id)
            continue
        if id not in known or known[id] != m:
            wrong.append(id)
        checked += 1
    for id in known:
        if id not in fresh:
            wrong.append(id)
    return checked, wrong, skipped


def render_slots(machines, slots):
    """waterbox/file_slots.json - the wizard's form, with the cartridge slot's
    formats taken from the machines rather than typed. Chimera narrows the slot
    to the chosen machine's own extensions, so what belongs here is the union;
    it had stayed the Nintendo 64's three, which is a wizard that will not let
    anybody pick a Famicom cartridge."""
    every = []
    for m in machines:
        if not m["loads"]:
            continue
        for ext in (m.get("extensions") or "").split():
            if ext not in every:
                every.append(ext)
    for slot in slots["slots"]:
        if slot["id"] == "rom":
            slot["formats"] = every

    # A slot on the CARTRIDGE rather than on the console: a Satellaview memory
    # pack goes into a BS-X cartridge, a Sufami Turbo minicart into a Sufami
    # Turbo cartridge, a Game Boy cartridge into a Super Game Boy. Offered only
    # on the machines whose cartridges have one, and never required - the base
    # cartridge alone is a perfectly good machine.
    subformats, subwhen = [], []
    for m in machines:
        if not m["loads"] or not m.get("subExtensions"):
            continue
        for ext in m["subExtensions"].split():
            if ext not in subformats:
                subformats.append(ext)
        subwhen.extend(m.get("when") or [m["id"].lower()])
    slots["slots"] = [s for s in slots["slots"] if s["id"] != "subcart"]
    if subformats:
        slots["slots"].append({
            "id": "subcart",
            "title": "Cartridge in the cartridge",
            "min": 0,
            "max": 1,
            "formats": subformats,
            "exposedWhen": {"setting": "machine", "in": sorted(set(subwhen))},
            "help": "Some Super Famicom cartridges have a slot of their own, and"
                " this is the cartridge that goes into it. A Satellaview "
                "memory pack (.bs) goes into the BS-X cartridge. Choose the "
                "BS-X as the game above and the pack here. A Sufami Turbo "
                "minicart (.st) goes into the Sufami Turbo cartridge, and a "
                "Game Boy cartridge goes into a Super Game Boy. The core "
                "finds the right slot from the file itself. Leave this empty"
                " for an ordinary cartridge, which is almost every game. A "
                "game that has no such slot reports an error when a file is "
                "given here.",
        })
    return json.dumps(slots, indent=2) + "\n"


def render_layout(mib):
    """waterbox/memory-layout.h - the arena, in the package's own numbers."""
    names = ["sbrk", "sealed", "invisible", "plain", "mmap"]
    lines = [
        "/* GENERATED by waterbox/gen-config.py from waterbox.config - do not edit.",
        " * The arena the package asks Chimera for, so the gate's runner can ask for",
        " * the same one and never test a core nobody will run. */",
        "#pragma once",
        "",
        "#define ARES_MEMORY_LAYOUT_TEMPLATE { \\",
    ]
    for name, mb in zip(names, mib):
        lines.append("\t%uu << 20, /* %s: %u MiB */ \\" % (mb, name, mb))
    lines.append("}")
    return "\n".join(lines) + "\n"


def main():
    check = "--check" in sys.argv
    fresh_path = None
    for i, a in enumerate(sys.argv):
        if a == "--against" and i + 1 < len(sys.argv):
            fresh_path = sys.argv[i + 1]
    data = json.load(open(os.path.join(HERE, "machines.json")))
    machines = data["machines"]

    inc = render_inc(machines) + "\n"
    inc_path = os.path.join(HERE, "machines.inc")

    cfg_path = os.path.join(HERE, "waterbox.config")
    cfg = json.load(open(cfg_path))
    cfg["machines"] = render_machines(machines)
    # what each machine's system is called in front of a person: this core's word for it
    cfg["systemNames"] = {m["id"]: SYSTEM_NAMES[m["id"]] for m in cfg["machines"]}
    firmware = render_firmware(machines, cfg.get("firmware"))
    if firmware:
        cfg["firmware"] = firmware
    else:
        cfg.pop("firmware", None)
    cfg["machineSetting"] = "machine"
    # The buffer has to hold the largest picture any machine draws.
    cfg["video"]["width"] = max(m["maxWidth"] for m in machines if m["loads"])
    cfg["video"]["height"] = max(m["maxHeight"] for m in machines if m["loads"])
    # The machine setting's options are the machines themselves.
    loaded = [m for m in machines if m["loads"]]
    for s in cfg["settings"]:
        if s["name"] == "machine":
            s["options"] = [m["id"].lower() for m in loaded]

    # The settings this core wrote itself are not all shared either. Three of
    # them are the Nintendo 64's alone - its real-time clock, its video
    # interface, its deinterlacer - and region belongs only to the machines that
    # actually have a PAL form. Scoping them is the same "when" the machines
    # use, worked out from the machine table rather than typed.
    n64_only = {"initialTime", "fastVI", "bobDeinterlace"}
    pal_capable = [w for m in loaded if "pal" in (m.get("regions") or [])
                     for w in (m.get("when") or [m["id"].lower()])]
    ports = {f"port{i}" for i in range(1, 9)}
    has_ports = [w for m in loaded if m.get("ports")
                   for w in (m.get("when") or [m["id"].lower()])]
    for s in cfg["settings"]:
        if s.get("fromAres"):
            continue
        if s["name"] in n64_only:
            s["when"] = ["n64"]
        elif s["name"] == "region":
            s["when"] = pal_capable
        elif s["name"] in ports:
            s["when"] = has_ports
        else:
            s.pop("when", None)

    # Everything ares itself offers on a machine, declared as a setting of this
    # package and shown only when that machine is the one loaded. These are not
    # decoration: a Game Boy's DMG revision, a Master System's VDP revision and
    # whether the boot ROM is skipped all change what the machine DOES, so a
    # movie has to carry them, which means the package has to declare them.
    cfg["settings"] = [s for s in cfg["settings"]
                       if not s.get("fromAres") and not s.get("fromVariants")]
    # Which of a BIOS's variants this project uses. A sync setting like any
    # other, because it decides which bytes the machine boots from and a movie
    # has to carry that.
    wants = {}
    for m in machines:
        for fw in m.get("firmware") or []:
            wants.setdefault(fw["id"], set()).add(m["id"].lower())
    for entry in firmware_variant_settings(cfg.get("firmware") or [], wants):
        entry["fromVariants"] = True
        cfg["settings"].append(entry)
    for m in machines:
        if not m["loads"]:
            continue
        for key, _path, decl in machine_settings(m):
            entry = {
                "name": key,
                "display": f'{m["label"]}: {decl["name"]}',
                "description": SETTING_TEXT.get(
                    decl["name"],
                    f'The {m["label"]} option that ares calls "{decl["name"]}". It has no description yet.'),
                "type": decl["type"],
                "fromAres": True,
                "when": list(m["when"]) if m.get("when") else [m["id"].lower()],
            }
            if decl["type"] == "bool":
                entry["default"] = decl["value"] == "true"
            elif decl["type"] == "int":
                entry["default"] = int(decl["value"]) if decl["value"].lstrip("-").isdigit() else 0
                if decl["options"]:
                    entry["options"] = list(decl["options"])
                    entry["type"] = "enum"
                    entry["default"] = decl["value"]
            else:
                entry["default"] = decl["value"]
                if decl["options"]:
                    entry["options"] = list(decl["options"])
            cfg["settings"].append(entry)

    text = json.dumps(cfg, indent=2) + "\n"

    keys_path = os.path.join(HERE, "default_keybinds.json")
    keys = json.dumps(render_keybinds(machines), indent=2) + "\n"

    # The gate's runner has to sandbox the guest in exactly the arena the
    # package asks Chimera for, or it is testing a core nobody will run. It used
    # to carry its own copy of the numbers with a comment claiming they matched,
    # and they had drifted - the runner gave 384MB of mmap where the package
    # declares 256. So the numbers are written out here and the runner includes
    # them, and --check fails if anybody edits one without the other.
    layout_path = os.path.join(HERE, "memory-layout.h")
    layout = render_layout(cfg["memoryLayoutMiB"])

    slots_path = os.path.join(HERE, "file_slots.json")
    slots = render_slots(machines, json.load(open(slots_path)))

    if check:
        bad = []
        if open(inc_path).read() != inc:
            bad.append("waterbox/machines.inc")
        if open(cfg_path).read() != text:
            bad.append("waterbox/waterbox.config")
        if open(keys_path).read() != keys:
            bad.append("waterbox/default_keybinds.json")
        if open(layout_path).read() != layout:
            bad.append("waterbox/memory-layout.h")
        if open(slots_path).read() != slots:
            bad.append("waterbox/file_slots.json")
        note = ""
        if fresh_path:
            checked, wrong, skipped = check_against(fresh_path, machines)
            if wrong:
                print("these machines are not what ares says any more: " + ", ".join(wrong),
                      file=sys.stderr)
                print("run: gen-machines tests/firmware > waterbox/machines.json && ./waterbox/gen-config.py",
                      file=sys.stderr)
                return 1
            note = f"; {checked} machines rebuilt and re-enumerated"
            if skipped:
                note += f", {len(skipped)} skipped for want of a console BIOS ({', '.join(skipped)})"
        if bad:
            print("out of date with ares: " + ", ".join(bad), file=sys.stderr)
            print("run ./waterbox/gen-config.py", file=sys.stderr)
            return 1
        print(f"the generated files match what ares says{note}")
        return 0

    open(inc_path, "w").write(inc)
    open(cfg_path, "w").write(text)
    open(keys_path, "w").write(keys)
    open(layout_path, "w").write(layout)
    open(slots_path, "w").write(slots)
    total = sum(1 for m in machines if m["loads"])
    print(f"wrote machines.inc, waterbox.config, default_keybinds.json, file_slots.json and memory-layout.h: {total} machines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
