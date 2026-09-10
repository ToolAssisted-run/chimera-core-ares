#!/usr/bin/env python3
"""Writes the .chimeraProject that run-frontend-gate.sh opens.

A project is what Chimera actually hands the core - the machine setting, a value
for every OTHER declared setting, the file in its slot, and an input log written
in the machine's own button names. That is the part run-gate.sh never exercises:
it mounts a file and writes {"machine": ...}, so a setting that the core cannot
read is invisible to it. Building the project from the package's own
waterbox.config, rather than from a template, is what makes this a test of the
declaration rather than of a copy of it.
"""

import hashlib
import json
import os
import sys
import zipfile


def main():
    if len(sys.argv) != 6:
        print("usage: make-project.py <package> <machine> <rom|''> <out> <frames>",
              file=sys.stderr)
        return 2
    package, setting, rom, out, frames = sys.argv[1:6]
    frames = int(frames)

    blob = open(package, "rb").read()
    cfg = json.loads(zipfile.ZipFile(package).read("waterbox.config"))
    machine = None
    for m in cfg["machines"]:
        if setting in m.get("when", []):
            machine = m
            break
    if machine is None:
        print(f"the package has no machine for {setting!r}", file=sys.stderr)
        return 1

    inputs = machine.get("input") or {}
    buttons = inputs.get("buttons") or []
    axes = inputs.get("axes") or []

    # The mnemonic Chimera parses, which is NOT one column per control in
    # declaration order. It is grouped by player - everything called "P2
    # something" is the second group - and within a group the AXES come first,
    # each written as a value padded to five and closed with a comma, then one
    # character per button. Groups are separated by '|'. See the engine's
    # EntryLayout::generate, which this mirrors.
    def player_of(name):
        if len(name) > 2 and name[0] in "Pp" and name[1].isdigit():
            return int(name[1])
        return 0

    groups = max([player_of(a["name"]) for a in axes] + [player_of(b) for b in buttons] + [0]) + 1
    # The log key is '#' for each GROUP and '|' after each name in it, which is
    # not the same shape as the entry - see the frontend's GenerateLogKey.
    row, key = "", ""
    for g in range(groups):
        row += "|"
        key += "#"
        for a in axes:
            if player_of(a["name"]) != g:
                continue
            row += "%5d," % a.get("neutral", 0)
            key += a["name"] + "|"
        for b in buttons:
            if player_of(b) != g:
                continue
            row += "."
            key += b + "|"
    row += "|"
    log = "[Input]\nLogKey:" + key + "\n" + "\n".join([row] * frames) + "\n[/Input]\n"

    files = []
    if rom:
        files.append({
            "name": os.path.basename(rom),
            "sha1": hashlib.sha1(open(rom, "rb").read()).hexdigest().upper(),
            "slot": "rom",
        })

    # Every declared setting gets a value, because that is what Chimera does -
    # and it is precisely how the ports came to be handed a Nintendo 64 device
    # name on a Mega Drive.
    settings = {}
    for decl in cfg.get("settings", []):
        name = decl["name"]
        settings[name] = setting if name == cfg.get("machineSetting") else decl.get("default")
    settings = {k: v for k, v in settings.items() if v is not None}

    project = {
        "id": ("ares" + setting).ljust(16, "0")[:16],
        "title": machine["label"] + " through Chimera",
        "description": "written by waterbox/run-frontend-gate.sh",
        "core": {
            "name": cfg.get("coreName", "ares"),
            "version": cfg["version"],
            "sha1": hashlib.sha1(blob).hexdigest().upper(),
        },
        "rerecords": 0,
        "files": files,
        "settings": settings,
        "firmware": [],
        "coreCache": [],
        "input": log,
        "markers": [],
        "branches": [],
        "headers": {
            "MovieVersion": "Chimera Project File v1.1",
            "Platform": machine["id"],
            "SHA1": files[0]["sha1"] if files else "",
            "LastInputFrame": "0",
            "VsyncNumerator": "60",
            "VsyncDenominator": "1",
        },
    }
    with open(out, "w") as f:
        json.dump(project, f, indent="\t")
    return 0


if __name__ == "__main__":
    sys.exit(main())
