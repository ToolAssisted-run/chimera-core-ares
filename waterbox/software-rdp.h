#pragma once
/* The Nintendo 64's Software renderer: angrylion's rasteriser in the place of
 * paraLLEl-RDP (chimera#481). See software-rdp.cpp. */
namespace softwareRdp
{
	/* Hands ares' RDP commands and VI frames to angrylion, after the machine is
	 * built (RDRAM must exist). fastVI leaves out the VI's filters;
	 * bobDeinterlace shows each field of an interlaced picture doubled. */
	void start(bool fastVI, bool bobDeinterlace);
	/* Gives them back to ares, for the next machine. */
	void stop(void);
}
