# Gate content

Test programs for the machines this core can prove, and - where one exists -
the picture real hardware produces for them.

Everything here is freely distributable, which is the whole reason it is here
rather than in a gitignored folder: the gate has to run on a public CI runner,
and a gate that needs content nobody may distribute can only prove that the
thing compiles.

The Nintendo 64 material is from
**[PeterLemon/N64](https://github.com/PeterLemon/N64)** (**Unlicense**, public
domain). The Game Boy one is **[libbet and the Blocks of
Doom](https://github.com/pinobatch/libbet)** by Damian Yerrick (**Zlib**), a
real homebrew game rather than a test program - which makes it the better
check, because it exercises the machine the way something written for it does.

| file | what it exercises |
| --- | --- |
| `helloworld-cpu.n64` | the CPU writing a framebuffer, and the video interface scanning it out. No RDP at all, so a failure here is the machine, not the rasteriser. |
| `helloworld-rdp.n64` | the same picture drawn through the display processor - the software rasteriser's path. |
| `input-cpu.n64` | reading the controller and drawing what it read, which is what makes an input leg impossible to pass hollowly. |
| `libbet.gb` | a whole Game Boy game: its CPU, its PPU, its APU and its controller, driven by code that expects them all to work. |

The `.png` beside each ROM is that program's reference picture from its own
repository. `waterbox/run-gate.sh` compares against `helloworld-cpu.png` exactly,
with the video interface's filtering off; see `docs/PLAN.md` for why the RDP
one is not compared the same way.
