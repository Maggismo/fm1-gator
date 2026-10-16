/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fw_rate.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_flash.h"

#ifndef FW_NAME
#define FW_NAME "FM-1 PLAYGROUND"
#endif
#ifndef FW_VERSION
#define FW_VERSION "DEV"
#endif
#ifndef FW_UART
#define FW_UART 1
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_900"
#endif
#ifndef FELUCCA_OTA_DRYRUN
#define FELUCCA_OTA_DRYRUN 0
#endif
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0

#include "fw.h"
#include "fw_tables.h"
#include "fw_logo.h"
#include "libc.c"
#include "lcd.c"
#include "gfx.c"
#include "bootguard.h"
#include "usb.c"
#if FW_UART
#include "midi_uart.c"
#endif
#include "input.c"
#include "audio.c"
#include "flash.c"
#include "recovery.c"
#include "app.c"
#include "main.c"
