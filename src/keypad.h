#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdbool.h>
#include "music_board.h"

// Initialize I2C1, reset the NeoTrellis, enable key events on all 16 keys,
// and configure the onboard neopixels. Returns true on success.
// Must be called once from core 0 before keypad_poll().
bool keypad_init(void);

// Poll the NeoTrellis FIFO for key events and update `state` accordingly.
// Handles note on/off, state transitions (IDLE/RECORD/PLAY/OVERDUB),
// waveform selection, and loop recording. Also refreshes LEDs when state
// changes. Call at ~100 Hz from the core 0 main loop.
void keypad_poll(shared_state_t *state);

#endif // KEYPAD_H