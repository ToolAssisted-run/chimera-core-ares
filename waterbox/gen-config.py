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


def port_prefix(name):
    """'Controller Port 2' -> 'P2'; anything else keeps a short readable form."""
    if name.startswith("Controller Port "):
        return "P" + name[len("Controller Port "):]
    if name == "Expansion Port":
        return "Exp"
    if name == "Extension Port":
        return "Ext"
    return name.replace(" Port", "")


def declare(machine):
    """The machine's buttons and axes, in the order that IS the wire format.

    Console inputs first, in ares' own order, then each port's devices. A name
    is qualified only as far as it needs to be: a handheld's buttons keep their
    own names, and a port's are prefixed by the port, and by the device too when
    the port offers more than one.
    """
    buttons, axes = [], []

    for i in machine["console"]:
        (axes if i["axis"] else buttons).append((i["name"], i["path"]))

    for port in machine["ports"]:
        prefix = port_prefix(port["name"])
        many = len(port["devices"]) > 1
        for device in port["devices"]:
            for i in device["inputs"]:
                name = f"{prefix} {device['name']} {i['name']}" if many else f"{prefix} {i['name']}"
                (axes if i["axis"] else buttons).append((name, i["path"]))

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
    "Fast Boot": "Skip the console's boot ROM and start the game straight away. It changes"
                 " what the machine does, not just how long it takes, so a movie recorded"
                 " with it on will not replay with it off.",
    "Interframe Blending": "Average each frame with the one before it, the way an LCD of the"
                           " period smeared. It is how the machine LOOKS, not what it does -"
                           " but the frontend records the blended picture, so a movie's video"
                           " differs with it on.",
    "Color Emulation": "Reproduce the screen's own colours rather than sending the machine's"
                       " raw values to a modern display. Changes the recorded picture.",
    "Phosphor": "Blend each frame with the last, the way a television tube held its picture.",
    "Version": "Which revision of the console this is. Real revisions differ in behaviour,"
               " and a game that depends on one will not do the same on another.",
    "Revision": "Which revision of the console this is. Real revisions differ in behaviour,"
                " and a game that depends on one will not do the same on another.",
    "Orientation": "Which way up the handheld is held. Some games are played rotated.",
    "Headphones": "Whether headphones are plugged in. The machine's sound differs.",
    "Show Icons": "Draw the handheld's own status icons around the picture.",
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
        first = m["ports"][0]["devices"][0]["name"] if m["ports"] else None
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
    "gbaBios": (
        "Game Boy Advance BIOS",
        "The Game Boy Advance's 16KB boot ROM. ares needs the real one: it is "
        "executed, games call into it, and its timing is part of the machine. "
        "Nintendo's, so it is yours to supply and not this core's to ship.",
        "GBA_bios.rom",
    ),
    "cvBios": (
        "ColecoVision BIOS",
        "The ColecoVision's 8KB boot ROM - the console's title screen and its "
        "cartridge checks live in it, and nothing runs without it.",
        "Coleco_Bios.bin",
    ),
    "a52Bios": (
        "Atari 5200 BIOS",
        "The Atari 5200's 2KB boot ROM. Exactly 2048 bytes; anything else is "
        "refused.",
        "[BIOS] Atari 5200 (USA).a52",
    ),
    "ngBios": (
        "Neo Geo AES BIOS",
        "The Neo Geo home console's 128KB boot ROM - `neo-epo.bin`, which is "
        "usually found inside an `aes.zip` BIOS set and must be handed over on "
        "its own.",
        "neo-epo.bin",
    ),
    "ngpBios": (
        "Neo Geo Pocket BIOS",
        "The 64KB boot ROM of the original monochrome Neo Geo Pocket, usually "
        "named for its 1998 date to tell it from the Color one.",
        "SNK Neo-Geo Pocket BIOS (1998)(SNK)(en-ja).bin",
    ),
    "ngpcBios": (
        "Neo Geo Pocket Color BIOS",
        "The 64KB boot ROM of the Neo Geo Pocket Color. A different ROM from "
        "the monochrome machine's, and not interchangeable with it.",
        "SNK Neo-Geo Pocket Color BIOS (1999)(SNK)(en-ja).bin",
    ),
    "ps1Bios": (
        "PlayStation BIOS",
        "The PlayStation's 512KB boot ROM. It is the console's operating system: "
        "games call into it constantly, and the machine reaches nothing without "
        "it. Sony's, so it is yours to supply. Any retail BIOS will do and they "
        "differ by region and revision; the one pinned here is what this package "
        "was tested against, and a project records which was used.",
        "PSX_4.1(A).bin",
    ),
    "msxBios": (
        "MSX BIOS",
        "The MSX's 32KB BIOS and BASIC ROM. Any machine's will do and they "
        "differ by region; the one pinned here is what this package was tested "
        "against, and a project records which was used.",
        "MSX.rom",
    ),
}


def render_firmware(machines, already=None):
    """What the frontend asks the user for, and refuses to start a project without.

    The size and the hash come from the developer's own copy under
    tests/firmware, so the package pins the BIOS that was actually verified. A
    machine without one - a CI runner, say - keeps whatever is already declared,
    because re-deriving it is not possible and discarding it would be worse.
    """
    import hashlib
    known = {f["id"]: f for f in (already or [])}
    out = []
    for m in machines:
        fw = m.get("firmware")
        if not fw:
            continue
        display, description, name = FIRMWARE_TEXT[fw["id"]]
        # The developer's own copy, which is where the machine was built from.
        path = os.path.join(HERE, "..", "tests", "firmware", fw["id"])
        if not os.path.exists(path):
            if fw["id"] in known:
                out.append(known[fw["id"]])
                continue
            raise SystemExit(
                f"{fw['id']}: machines.json describes a machine built with this BIOS, and\n"
                f"neither tests/firmware/{fw['id']} nor an existing declaration is there to\n"
                f"take its size and hash from."
            )
        blob = open(path, "rb").read()
        out.append({
            "id": fw["id"],
            "display": display,
            "description": description,
            "size": len(blob),
            "sha1": hashlib.sha1(blob).hexdigest().upper(),
            "name": name,
            "requiredWhen": {"setting": "machine", "is": m["id"].lower()},
        })
    return out


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
            },
            "virtualWidth": m["virtualWidth"],
            "virtualHeight": m["virtualHeight"],
            "extensions": {"." + e: m["id"] for e in m["extensions"].split()},
        }
        if axes:
            entry["input"]["axes"] = [
                {"name": n, "min": -128, "max": 127, "neutral": 0} for n, _ in axes
            ]
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


def render_keybinds(machines):
    """Default bindings for every machine's controller."""
    trollers, analog = {}, {}
    for m in machines:
        if not m["loads"]:
            continue
        buttons, axes = declare(m)
        name = f"{m['label']} Controller"

        # Only what a first player touches: a console's own buttons, and the
        # first port's. Anything further along shares keys it should not.
        first_port = port_prefix(m["ports"][0]["name"]) if m["ports"] else None
        bound = {}
        for declared, path in buttons:
            leaf = path.rsplit("/", 1)[-1]
            if leaf not in DEFAULT_KEYS:
                continue
            owner = declared.split(" ")[0]
            if m["ports"] and owner != first_port:
                continue
            bound.setdefault(DEFAULT_KEYS[leaf], declared)
        # setdefault above keeps the FIRST button that wants a key, so a port's
        # second device cannot steal one from the first.
        trollers[name] = {v: k for k, v in bound.items()}

        bound_axes = {}
        for declared, path in axes:
            leaf = path.rsplit("/", 1)[-1]
            if leaf in DEFAULT_AXES and (not m["ports"] or declared.split(" ")[0] == first_port):
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
    cfg["settings"] = [s for s in cfg["settings"] if not s.get("fromAres")]
    for m in machines:
        if not m["loads"]:
            continue
        for key, _path, decl in machine_settings(m):
            entry = {
                "name": key,
                "display": f'{m["label"]}: {decl["name"]}',
                "description": SETTING_TEXT.get(
                    decl["name"],
                    f'{decl["name"]}, as ares offers it on the {m["label"]}.'),
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
