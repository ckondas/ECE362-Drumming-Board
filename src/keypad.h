#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>
#include <stdbool.h>
#include "music_board.h"

// ============ SEESAW REGISTER MAP ============

// Module base addresses
#define SEESAW_STATUS_BASE    0x00
#define SEESAW_NEOPIXEL_BASE  0x0E
#define SEESAW_KEYPAD_BASE    0x10

// Status module registers
#define SEESAW_STATUS_HW_ID   0x01
#define SEESAW_STATUS_SWRST   0x7F

// Keypad module registers
#define SEESAW_KEYPAD_STATUS   0x00
#define SEESAW_KEYPAD_EVENT    0x01
#define SEESAW_KEYPAD_INTENSET 0x02
#define SEESAW_KEYPAD_INTENCLR 0x03
#define SEESAW_KEYPAD_COUNT    0x04
#define SEESAW_KEYPAD_FIFO     0x10

// Keypad edge types
#define SEESAW_KEYPAD_EDGE_HIGH    0
#define SEESAW_KEYPAD_EDGE_LOW     1
#define SEESAW_KEYPAD_EDGE_FALLING 2
#define SEESAW_KEYPAD_EDGE_RISING  3

// NeoPixel module registers
#define SEESAW_NEOPIXEL_PIN        0x01
#define SEESAW_NEOPIXEL_SPEED      0x02
#define SEESAW_NEOPIXEL_BUF_LENGTH 0x03
#define SEESAW_NEOPIXEL_BUF        0x04
#define SEESAW_NEOPIXEL_SHOW       0x05

// NeoTrellis specifics
#define NEO_TRELLIS_NEOPIX_PIN  3    // seesaw pin for onboard NeoPixels
#define NEO_TRELLIS_NUM_KEYS    16
#define NEO_TRELLIS_NUM_ROWS    4
#define NEO_TRELLIS_NUM_COLS    4

// Key number conversion macros (seesaw uses sparse 8-col layout)
// NeoTrellis key 0-15 → seesaw key number
#define NEO_TRELLIS_KEY(k)       (((k) / 4) * 8 + ((k) % 4))
// Seesaw key number → NeoTrellis key 0-15
#define NEO_TRELLIS_SEESAW_KEY(k) (((k) / 8) * 4 + ((k) % 8))

// ============ LED COLORS (GRB format) ============

#define COLOR_OFF         0x000000
#define COLOR_NOTE_IDLE   0x001400   // dim green
#define COLOR_NOTE_ACTIVE 0x00FF00   // bright green
#define COLOR_RECORD      0x0000FF   // red (GRB: 00,00,FF = red)
#define COLOR_RECORD_ON   0x0000FF   // bright red
#define COLOR_PLAY        0x140014   // dim purple
#define COLOR_PLAY_ON     0xFF00FF   // bright purple
#define COLOR_STOP        0x000A14   // dim orange
#define COLOR_INST        0x140A00   // dim cyan
#define COLOR_INST_ON     0xFF6600   // bright cyan

// Waveform select colors (State 2)
#define COLOR_SINE        0x00FF00   // green
#define COLOR_SQUARE      0x0000FF   // red
#define COLOR_SAW         0xFF6600   // cyan
#define COLOR_TRIANGLE    0xFFFF00   // yellow

// ============ UI STATES ============

typedef enum {
    UI_STATE_NOTES = 0,       // State 1: note input + controls
    UI_STATE_INSTRUMENT       // State 2: waveform selection
} ui_state_t;

// ============ PUBLIC API ============

// Call once at startup. Initializes I2C, resets seesaw, enables keypad events,
// sets up NeoPixels. Returns true on success.
bool keypad_init(void);

// Call this every ~10ms from your main loop on Core 0.
// Reads keypad FIFO, handles key events, updates shared state + LEDs.
void keypad_poll(shared_state_t *state);

// Force-refresh all LEDs to match current UI state.
// Useful after switching between State 1 and State 2.
void keypad_update_leds(shared_state_t *state);

#endif // KEYPAD_H