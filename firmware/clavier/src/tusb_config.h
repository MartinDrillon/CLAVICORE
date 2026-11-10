#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#include "tusb_option.h"

#define CFG_TUSB_MCU OPT_MCU_RP2040
#define CFG_TUSB_OS OPT_OS_PICO
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE 64
#ifndef CLAVIER_SERIAL_STUDIO
#define CLAVIER_SERIAL_STUDIO 0
#endif
#if CLAVIER_SERIAL_STUDIO
#define CFG_TUD_CDC 2  // port 1 = journal/console, port 2 = Serial Studio (ADR-0032)
#else
#define CFG_TUD_CDC 1  // journal texte (ADR-0003 addendum CDC)
#endif
#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 512
#define CFG_TUD_CDC_EP_BUFSIZE 64
#define CFG_TUD_MIDI 1
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64

#endif