/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "sdkconfig.h"
#ifndef SM_GENERIC_BOARD
#define SM_GENERIC_BOARD 0
#endif
#if SM_GENERIC_BOARD
#if CONFIG_IDF_TARGET_ESP32S2
#define SM_BOARD_NAME "SM S2 UNTESTED"
#else
#define SM_BOARD_NAME "SM S3 UNTESTED"
#endif
#else
#define SM_BOARD_NAME "StreamMaster Onju V3"
#endif
