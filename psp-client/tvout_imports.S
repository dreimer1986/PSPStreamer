	.set noreorder
#include "pspimport.s"
	/* These two exports are provided by dvemgr.prx, loaded through ARK before
	 * the first call.  They are intentionally kept out of the regular PSP SDK
	 * because Sony exposed TV-out only to privileged code. */
	IMPORT_START "pspDveManager",0x40090000
	IMPORT_FUNC "pspDveManager",0x2ACFCB6D,pspDveMgrCheckVideoOut
	IMPORT_FUNC "pspDveManager",0xF9C86C73,pspDveMgrSetVideoOut
