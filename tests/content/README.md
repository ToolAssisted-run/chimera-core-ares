# Gate content

Nintendo 64 test ROMs, and the pictures real hardware produces for them.

All of it is from **[PeterLemon/N64](https://github.com/PeterLemon/N64)**, which
is released under the **Unlicense** (public domain). That is the whole reason it
is here rather than in a gitignored folder: the gate has to run on a public CI
runner, and a gate that needs content nobody may distribute can only prove that
the thing compiles.

| file | what it exercises |
| --- | --- |
| `helloworld-cpu.n64` | the CPU writing a framebuffer, and the video interface scanning it out. No RDP at all, so a failure here is the machine, not the rasteriser. |
| `helloworld-rdp.n64` | the same picture drawn through the display processor - the software rasteriser's path. |
| `input-cpu.n64` | reading the controller and drawing what it read, which is what makes an input leg impossible to pass hollowly. |

The `.png` beside each ROM is that program's reference picture from its own
repository. `waterbox/run-gate.sh` compares against `helloworld-cpu.png` exactly,
with the video interface's filtering off; see `docs/PLAN.md` for why the RDP
one is not compared the same way.
