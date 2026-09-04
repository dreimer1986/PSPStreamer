	.set noreorder
#include "pspimport.s"
	IMPORT_START "sceMeCore_driver",0x00010000
	IMPORT_FUNC "sceMeCore_driver",0x5DFF5C50,sceMeBootStart660
	IMPORT_START "sceAudio_driver",0x00010000
	IMPORT_FUNC "sceAudio_driver",0x4A0FE97D,sceAudioSetFrequency660
