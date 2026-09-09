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

    # The mnemonic Chimera parses: a column per button, then a field per axis.
    key = "#" + "".join(b + "|" for b in buttons) + "".join(a["name"] + "|" for a in axes)
    row = "|" + "." * len(buttons) + "|" + ",".join("    0" for _ in axes) + ("|" if axes else "")
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
