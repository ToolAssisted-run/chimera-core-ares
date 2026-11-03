/* The Nintendo 64's Software renderer (chimera#481): angrylion's rasteriser,
 * from its own upstream (extern/angrylion-rdp-plus, MAME licence), draws in
 * the place of paraLLEl-RDP when a project's renderer is "software".
 *
 * ares hands it the RDP's command lists and the video interface's frames
 * through Nintendo64::softwareRDP (patches/ares/0043). It runs on the machine's
 * thread, one command list at a time, and writes RDRAM as it draws, so what
 * the CPU finds there never depends on timing. Everything it keeps between
 * calls - TMEM, the tiles, the ninth bits of RDRAM, the last picture - is its
 * own static memory, which is guest memory: the sandbox's savestate carries it
 * whole. ares' own serializer (captureState, the reference runner's
 * --rewind-at) does not.
 *
 * This file is this repository's own work, under its MIT licence. */
#include <n64/n64.hpp>
#include <string.h>

#include "software-rdp.h"

extern "C" {
#include <n64video.h>
}

namespace
{
	using namespace ares::Nintendo64;

	/* angrylion reads registers through pointers to 32-bit words, as an
	 * emulator that keeps them that way would hand them over. ares keeps them
	 * as fields, so each call is given a copy made just before it. */
	uint32_t g_vi[VI_NUM_REG];
	uint32_t g_dp[DP_NUM_REG];
	uint32_t *g_viReg[VI_NUM_REG];
	uint32_t *g_dpReg[DP_NUM_REG];
	uint32_t g_miIntr;
	/* ares keeps the RSP's data memory big-endian and angrylion reads it as
	 * host-order words, which is how ares keeps RDRAM; a list sent from DMEM
	 * is read from this copy, turned the right way round. */
	uint32_t g_dmem[1024];
	n64video_frame_buffer g_fb;
	bool g_bob;

	const uint32_t DpStatusXbusDma = 0x001;

	void syncFull(void) { rdp.syncFull(); }

	void render()
	{
		auto &command = rdp.command;
		if (command.source)
		{
			for (uint32_t i = 0; i < 1024; i++) g_dmem[i] = rsp.dmem.read<Word>(i * 4);
		}
		g_dp[DP_START] = command.start;
		g_dp[DP_END] = command.end;
		g_dp[DP_CURRENT] = command.current;
		g_dp[DP_STATUS] = command.source ? DpStatusXbusDma : 0;
		n64video_process_list();
		command.start = g_dp[DP_START];
		command.current = g_dp[DP_CURRENT];
	}

	/* The VI's registers as the CPU would read them, without the read of the
	 * current line, which makes the CPU catch up. */
	void frame()
	{
		for (uint32_t i = 0; i < VI_NUM_REG; i++)
		{
			if (i == VI_V_CURRENT_LINE) g_vi[i] = (uint32_t)vi.io.field | (uint32_t)vi.io.vcounter << 1;
			else g_vi[i] = vi.readWord(i << 2, cpu);
		}
		n64video_update_screen(&g_fb);
	}

	/* The picture into ares' screen, which hands it to the frontend as every
	 * machine's: 640 pixels wide, 240 (288) lines progressive, which the
	 * frontend side shows twice each, or 480 (576) interlaced. */
	bool refresh()
	{
		auto &screen = vi.screen;
		u32 *out = screen->pixels(1).data();
		const u32 width = 640;
		if (!g_fb.valid || g_fb.pixels == nullptr || g_fb.width == 0 || g_fb.height == 0)
		{
			screen->setViewport(0, 0, 1, 1);
			out[0] = 0;
			return true;
		}
		const u32 full = Region::PAL() ? 576 : 480;
		u32 height = g_fb.height;
		u32 firstRow = 0, rowStep = 1;
		/* The filtered picture is 640 wide already; the unfiltered one is the
		 * game's own size, and is stretched to it. */
		if (height > full / 2 && g_bob)
		{
			firstRow = n64video_lower_field() ? 1 : 0;
			rowStep = 2;
			height /= 2;
		}
		const u32 shown = height > full ? full : height;
		for (u32 y = 0; y < shown; y++)
		{
			const n64video_pixel *row = g_fb.pixels + (size_t)(firstRow + y * rowStep) * g_fb.pitch;
			u32 *target = out + (size_t)y * width;
			for (u32 x = 0; x < width; x++)
			{
				const n64video_pixel &p = row[(size_t)x * g_fb.width / width];
				target[x] = (u32)p.r << 16 | (u32)p.g << 8 | (u32)p.b;
			}
		}
		screen->setViewport(0, 0, width, shown);
		return true;
	}
}

namespace softwareRdp
{
	void start(bool fastVI, bool bobDeinterlace)
	{
		n64video_config config;
		n64video_config_init(&config);
		config.gfx.rdram = rdram.ram.data;
		config.gfx.rdram_size = rdram.ram.size;
		config.gfx.dmem = (uint8_t *)g_dmem;
		for (uint32_t i = 0; i < VI_NUM_REG; i++) g_viReg[i] = &g_vi[i];
		for (uint32_t i = 0; i < DP_NUM_REG; i++) g_dpReg[i] = &g_dp[i];
		config.gfx.vi_reg = g_viReg;
		config.gfx.dp_reg = g_dpReg;
		config.gfx.mi_intr_reg = &g_miIntr;
		config.gfx.mi_intr_cb = syncFull;
		config.vi.mode = fastVI ? VI_MODE_COLOR : VI_MODE_NORMAL;
		config.vi.hide_overscan = false;
		config.vi.vsync = false;
		/* one worker, on the machine's thread: the same picture and the same
		 * RDRAM every run */
		config.parallel = false;
		config.num_workers = 1;
		memset(&g_fb, 0, sizeof g_fb);
		g_bob = bobDeinterlace;
		n64video_init(&config);
		softwareRDP.render = render;
		softwareRDP.frame = frame;
		softwareRDP.refresh = refresh;
	}

	void stop(void)
	{
		if (softwareRDP.render == nullptr) return;
		softwareRDP = {};
		n64video_close();
	}
}
