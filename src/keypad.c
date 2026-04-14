// keypad.c — NeoTrellis 4x4 keypad driver for RP2350 (Pico SDK)
// Sami's I2C keypad module
//
// This file handles:
//   1. Low-level seesaw I2C read/write
//   2. Keypad FIFO polling and event parsing
//   3. NeoPixel LED color updates
//   4. State machine for notes, record/play/stop, instrument select
//   5. Writing into the shared_state_t struct (spinlock-protected)

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "keypad.h"
#include "music_board.h"

// ============ INTERNAL STATE ============

static ui_state_t   ui_state = UI_STATE_NOTES;
static uint8_t      selected_waveform = WAVE_SINE;
static uint32_t     last_event_ms = 0;
static spin_lock_t *shared_lock = NULL;

// Which I2C instance to use (I2C1 for GPIO2/3)
#define KEYPAD_I2C i2c1

// ============ LOW-LEVEL SEESAW I2C ============

// Write to a seesaw register: [module_base, function_reg, ...data]
static bool seesaw_write(uint8_t module_base, uint8_t func_reg,
                          const uint8_t *data, size_t len) {
    uint8_t buf[2 + len];
    buf[0] = module_base;
    buf[1] = func_reg;
    if (data && len > 0) {
        memcpy(&buf[2], data, len);
    }
    int ret = i2c_write_blocking(KEYPAD_I2C, TRELLIS_ADDR, buf, 2 + len, false);
    return (ret == (int)(2 + len));
}

// Read from a seesaw register: write [module_base, func_reg], then read
static bool seesaw_read(uint8_t module_base, uint8_t func_reg,
                         uint8_t *dest, size_t len, uint16_t delay_us) {
    uint8_t cmd[2] = { module_base, func_reg };
    int ret = i2c_write_blocking(KEYPAD_I2C, TRELLIS_ADDR, cmd, 2, true);
    if (ret != 2) return false;

    // Seesaw needs a short delay between write and read
    if (delay_us > 0) {
        sleep_us(delay_us);
    } else {
        sleep_us(250);  // default safe delay
    }

    ret = i2c_read_blocking(KEYPAD_I2C, TRELLIS_ADDR, dest, len, false);
    return (ret == (int)len);
}

// ============ SEESAW HELPERS ============

static bool seesaw_reset(void) {
    uint8_t rst = 0xFF;
    bool ok = seesaw_write(SEESAW_STATUS_BASE, SEESAW_STATUS_SWRST, &rst, 1);
    sleep_ms(500);  // seesaw needs time to reboot after reset
    return ok;
}

static bool seesaw_check_id(void) {
    // Just verify we can talk to the device via I2C ACK
    uint8_t dummy;
    int ret = i2c_read_blocking(KEYPAD_I2C, TRELLIS_ADDR, &dummy, 1, false);
    return (ret >= 0);
}

// Enable keypad event on a specific key + edge
static void keypad_enable_event(uint8_t key, uint8_t edge) {
    // From CircuitPython source: cmd[0] = key, cmd[1] = (1 << (edge+1)) | enable
    // key is the raw seesaw key number (NOT bit-shifted)
    // edge: 0=HIGH, 1=LOW, 2=FALLING, 3=RISING
    uint8_t seesaw_key = NEO_TRELLIS_KEY(key);
    uint8_t cmd[2];
    cmd[0] = seesaw_key;
    cmd[1] = (1 << (edge + 1)) | 0x01;  // enable=1, with edge bitmask
    seesaw_write(SEESAW_KEYPAD_BASE, SEESAW_KEYPAD_EVENT, cmd, 2);
}

// Get number of events in the keypad FIFO
static uint8_t keypad_get_count(void) {
    uint8_t count = 0;
    seesaw_read(SEESAW_KEYPAD_BASE, SEESAW_KEYPAD_COUNT, &count, 1, 5000);
    // 0xFF means the read returned garbage — treat as 0
    if (count == 0xFF) return 0;
    return count;
}

// Read raw events from keypad FIFO
// Each event is 1 byte: bits [7:2] = seesaw key, bits [1:0] = edge
static bool keypad_read_fifo(uint8_t *buf, uint8_t count) {
    return seesaw_read(SEESAW_KEYPAD_BASE, SEESAW_KEYPAD_FIFO, buf, count, 5000);
}

// ============ NEOPIXEL HELPERS ============

static void neopixel_init(void) {
    // Set NeoPixel output pin
    uint8_t pin = NEO_TRELLIS_NEOPIX_PIN;
    seesaw_write(SEESAW_NEOPIXEL_BASE, SEESAW_NEOPIXEL_PIN, &pin, 1);

    // Set speed to 800KHz (1 = 800KHz, 0 = 400KHz)
    uint8_t speed = 1;
    seesaw_write(SEESAW_NEOPIXEL_BASE, SEESAW_NEOPIXEL_SPEED, &speed, 1);

    // Set buffer length: 16 pixels * 3 bytes (GRB) = 48 bytes
    uint8_t len_buf[2] = { 0x00, 48 };  // big-endian 16-bit
    seesaw_write(SEESAW_NEOPIXEL_BASE, SEESAW_NEOPIXEL_BUF_LENGTH, len_buf, 2);
}

// Set one pixel color. color is 0xGGRRBB (GRB order).
static void neopixel_set(uint8_t pixel, uint32_t color) {
    uint8_t buf[5];
    // First 2 bytes: offset into the pixel buffer (pixel * 3), big-endian
    uint16_t offset = pixel * 3;
    buf[0] = (offset >> 8) & 0xFF;
    buf[1] = offset & 0xFF;
    // Next 3 bytes: G, R, B
    buf[2] = (color >> 16) & 0xFF;  // Green
    buf[3] = (color >> 8)  & 0xFF;  // Red
    buf[4] = color & 0xFF;          // Blue
    seesaw_write(SEESAW_NEOPIXEL_BASE, SEESAW_NEOPIXEL_BUF, buf, 5);
}

// Push pixel buffer to LEDs
static void neopixel_show(void) {
    seesaw_write(SEESAW_NEOPIXEL_BASE, SEESAW_NEOPIXEL_SHOW, NULL, 0);
}

// ============ LED UPDATE LOGIC ============

// Set all 16 LEDs based on current UI state and system mode
void keypad_update_leds(shared_state_t *state) {
    if (ui_state == UI_STATE_NOTES) {
        // Top 12 keys: note colors
        for (int i = 0; i < 12; i++) {
            if (state->current_note == key_to_note[i]) {
                neopixel_set(i, COLOR_NOTE_ACTIVE);
            } else {
                neopixel_set(i, COLOR_NOTE_IDLE);
            }
        }

        // Bottom row: control keys
        // Record button
        if (state->system_mode == MODE_RECORDING || state->system_mode == MODE_OVERDUB) {
            neopixel_set(KEY_RECORD, COLOR_RECORD_ON);
        } else {
            neopixel_set(KEY_RECORD, COLOR_RECORD);
        }

        // Play button
        if (state->system_mode == MODE_PLAYING || state->system_mode == MODE_OVERDUB) {
            neopixel_set(KEY_PLAY, COLOR_PLAY_ON);
        } else {
            neopixel_set(KEY_PLAY, COLOR_PLAY);
        }

        // Stop button
        neopixel_set(KEY_STOP, COLOR_STOP);

        // Instrument select button
        neopixel_set(KEY_INSTRUMENT, COLOR_INST);

    } else {
        // UI_STATE_INSTRUMENT: show waveform options on first 4 keys
        uint32_t wave_colors[WAVE_COUNT] = {
            COLOR_SINE, COLOR_SQUARE, COLOR_SAW, COLOR_TRIANGLE
        };
        for (int i = 0; i < WAVE_COUNT; i++) {
            if (i == selected_waveform) {
                neopixel_set(i, wave_colors[i]);  // bright = selected
            } else {
                // Dim version: shift right by 4
                uint32_t dim = ((wave_colors[i] >> 4) & 0x0F0F0F);
                neopixel_set(i, dim);
            }
        }
        // Keys 4-14: off
        for (int i = WAVE_COUNT; i < 15; i++) {
            neopixel_set(i, COLOR_OFF);
        }
        // Key 15 (instrument): bright to show we're in this menu
        neopixel_set(KEY_INSTRUMENT, COLOR_INST_ON);
    }

    neopixel_show();
}

// ============ KEY EVENT HANDLERS ============

// Note name lookup for debug printing
static const char *note_names[12] = {
    "A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"
};

static const char *mode_names[4] = {
    "IDLE", "RECORDING", "PLAYING", "OVERDUB"
};

static const char *wave_names[4] = {
    "SINE", "SQUARE", "SAW", "TRIANGLE"
};

// Handle a note key press (keys 0-11)
static void handle_note_press(uint8_t key, shared_state_t *state) {
    uint8_t note = key_to_note[key];

    printf("[NOTE] Key %d -> %s pressed", key, note_names[note]);

    // Update shared state with spinlock
    uint32_t irq = spin_lock_blocking(shared_lock);
    state->current_note = note;
    spin_unlock(shared_lock, irq);

    // If recording or overdubbing, store the note event
    if (state->system_mode == MODE_RECORDING || state->system_mode == MODE_OVERDUB) {
        uint8_t loop_idx = state->active_loop;
        uint8_t layer = state->loops[loop_idx].layer_count;

        if (layer < MAX_LAYERS) {
            uint16_t evt_idx = state->loops[loop_idx].event_count[layer];

            if (evt_idx < MAX_EVENTS) {
                uint32_t now = to_ms_since_boot(get_absolute_time());
                uint32_t delta = (last_event_ms == 0) ? 0 : (now - last_event_ms);
                last_event_ms = now;

                uint32_t irq2 = spin_lock_blocking(shared_lock);
                state->loops[loop_idx].events[layer][evt_idx].note = note;
                state->loops[loop_idx].events[layer][evt_idx].delta_ms = delta;
                state->loops[loop_idx].event_count[layer] = evt_idx + 1;
                spin_unlock(shared_lock, irq2);

                printf(" | RECORDED loop=%d layer=%d evt=%d delta=%dms",
                       loop_idx, layer, evt_idx, delta);
            } else {
                printf(" | BUFFER FULL!");
            }
        }
    }
    printf("\n");
}

// Handle a note key release (keys 0-11)
static void handle_note_release(shared_state_t *state) {
    printf("[NOTE] Released\n");
    uint32_t irq = spin_lock_blocking(shared_lock);
    state->current_note = NOTE_NONE;
    spin_unlock(shared_lock, irq);
}

// Handle record button (key 12)
static void handle_record(shared_state_t *state) {
    uint32_t irq = spin_lock_blocking(shared_lock);

    switch (state->system_mode) {
        case MODE_IDLE:
            state->loops[state->active_loop].layer_count = 0;
            state->loops[state->active_loop].event_count[0] = 0;
            state->loops[state->active_loop].duration_ms = 0;
            state->system_mode = MODE_RECORDING;
            last_event_ms = 0;
            printf("[REC ] Started fresh recording on loop %d\n", state->active_loop);
            break;

        case MODE_PLAYING:
            {
                uint8_t loop_idx = state->active_loop;
                uint8_t next_layer = state->loops[loop_idx].layer_count;
                if (next_layer < MAX_LAYERS) {
                    state->loops[loop_idx].event_count[next_layer] = 0;
                    state->system_mode = MODE_OVERDUB;
                    last_event_ms = 0;
                    printf("[REC ] Overdub started on loop %d, layer %d\n", loop_idx, next_layer);
                } else {
                    printf("[REC ] Cannot overdub — max layers (%d) reached!\n", MAX_LAYERS);
                }
            }
            break;

        case MODE_RECORDING:
        case MODE_OVERDUB:
            printf("[REC ] Already recording — ignored\n");
            break;
    }

    spin_unlock(shared_lock, irq);
}

// Handle play button (key 13)
static void handle_play(shared_state_t *state) {
    uint32_t irq = spin_lock_blocking(shared_lock);

    if (state->system_mode == MODE_IDLE) {
        uint8_t loop_idx = state->active_loop;
        if (state->loops[loop_idx].layer_count > 0 ||
            state->loops[loop_idx].event_count[0] > 0) {
            state->system_mode = MODE_PLAYING;
            printf("[PLAY] Playing loop %d (%d layers, %d events in layer 0, %dms)\n",
                   loop_idx,
                   state->loops[loop_idx].layer_count,
                   state->loops[loop_idx].event_count[0],
                   state->loops[loop_idx].duration_ms);
        } else {
            printf("[PLAY] Nothing recorded on loop %d — ignored\n", loop_idx);
        }
    } else {
        printf("[PLAY] Not idle (mode=%s) — ignored\n", mode_names[state->system_mode]);
    }

    spin_unlock(shared_lock, irq);
}

// Handle stop button (key 14)
static void handle_stop(shared_state_t *state) {
    uint32_t irq = spin_lock_blocking(shared_lock);

    if (state->system_mode == MODE_RECORDING) {
        uint8_t loop_idx = state->active_loop;
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (last_event_ms > 0) {
            uint32_t total = 0;
            uint16_t count = state->loops[loop_idx].event_count[0];
            for (uint16_t i = 0; i < count; i++) {
                total += state->loops[loop_idx].events[0][i].delta_ms;
            }
            state->loops[loop_idx].duration_ms = total;
        }
        state->loops[loop_idx].layer_count = 1;
        state->system_mode = MODE_IDLE;
        printf("[STOP] Recording stopped. Loop %d: %d events, %dms duration\n",
               loop_idx,
               state->loops[loop_idx].event_count[0],
               state->loops[loop_idx].duration_ms);

        // Dump recorded events
        uint16_t count = state->loops[loop_idx].event_count[0];
        for (uint16_t i = 0; i < count && i < 20; i++) {
            printf("       evt[%d] note=%s delta=%dms\n", i,
                   note_names[state->loops[loop_idx].events[0][i].note],
                   state->loops[loop_idx].events[0][i].delta_ms);
        }
        if (count > 20) printf("       ... (%d more)\n", count - 20);

    } else if (state->system_mode == MODE_OVERDUB) {
        uint8_t loop_idx = state->active_loop;
        uint8_t finished_layer = state->loops[loop_idx].layer_count;
        state->loops[loop_idx].layer_count++;
        state->system_mode = MODE_PLAYING;
        printf("[STOP] Overdub stopped. Loop %d now has %d layers (layer %d: %d events)\n",
               loop_idx,
               state->loops[loop_idx].layer_count,
               finished_layer,
               state->loops[loop_idx].event_count[finished_layer]);

    } else if (state->system_mode == MODE_PLAYING) {
        state->system_mode = MODE_IDLE;
        printf("[STOP] Playback stopped.\n");

    } else {
        printf("[STOP] Already idle — ignored\n");
    }

    state->current_note = NOTE_NONE;
    last_event_ms = 0;

    spin_unlock(shared_lock, irq);
}

// Handle instrument select button (key 15)
static void handle_instrument(shared_state_t *state) {
    if (ui_state == UI_STATE_NOTES) {
        ui_state = UI_STATE_INSTRUMENT;
        printf("[INST] Entering instrument select (current: %s)\n", wave_names[selected_waveform]);
    } else {
        uint32_t irq = spin_lock_blocking(shared_lock);
        state->current_waveform = selected_waveform;
        spin_unlock(shared_lock, irq);
        ui_state = UI_STATE_NOTES;
        printf("[INST] Back to notes. Waveform set to %s\n", wave_names[selected_waveform]);
    }
}

// Handle key press in instrument select mode (State 2)
static void handle_instrument_select(uint8_t key, shared_state_t *state) {
    if (key < WAVE_COUNT) {
        selected_waveform = key;
        uint32_t irq = spin_lock_blocking(shared_lock);
        state->current_waveform = selected_waveform;
        spin_unlock(shared_lock, irq);
        printf("[INST] Selected waveform: %s\n", wave_names[selected_waveform]);
    } else {
        printf("[INST] Key %d not a waveform — ignored\n", key);
    }
}

// ============ PUBLIC API ============

bool keypad_init(void) {
    // Initialize I2C1 at 100kHz (safe default for seesaw)
    i2c_init(KEYPAD_I2C, 100 * 1000);
    scan_i2c();
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);



    // Claim a hardware spinlock for shared state
    shared_lock = spin_lock_init(SHARED_SPINLOCK_NUM);

    // Reset the seesaw chip and wait for it to fully boot
    printf("Skipping reset.\n");
  sleep_ms(500);

    // Verify we can talk to it
    printf("Checking NeoTrellis...\n");
    if (!seesaw_check_id()) {
        printf("ERROR: NeoTrellis not found at 0x%02X\n", TRELLIS_ADDR);
        return false;
    }
    printf("NeoTrellis found at 0x%02X!\n", TRELLIS_ADDR);

    // Initialize NeoPixels
    neopixel_init();

    // Enable RISING and FALLING events for all 16 keys
    for (int i = 0; i < NEO_TRELLIS_NUM_KEYS; i++) {
        keypad_enable_event(i, SEESAW_KEYPAD_EDGE_RISING);
        sleep_ms(10);  // give seesaw time to process
        keypad_enable_event(i, SEESAW_KEYPAD_EDGE_FALLING);
        sleep_ms(10);
    }
    printf("All key events enabled.\n");

    // Enable keypad interrupt
    uint8_t int_enable = 0x01;
    seesaw_write(SEESAW_KEYPAD_BASE, SEESAW_KEYPAD_INTENSET, &int_enable, 1);

    printf("Keypad initialized. %d keys active.\n", NEO_TRELLIS_NUM_KEYS);
    printf("  UI state: NOTES | Mode: IDLE | Waveform: SINE\n");
    return true;
}

void scan_i2c() {
    printf("Scanning I2C...\n");

    for (int addr = 0; addr < 128; addr++) {
        uint8_t dummy;
        int ret = i2c_read_timeout_us(i2c1, addr, &dummy, 1, false, 2000);

        if (ret >= 0) {
            printf("Found device at 0x%02X\n", addr);
        }
    }

    printf("Scan done.\n");
}

void keypad_poll(shared_state_t *state) {
    // No INT pin wired — always poll the FIFO directly
    uint8_t count = keypad_get_count();

    // Debug: print count every 100 polls so we can see if FIFO ever fills
    static uint32_t poll_num = 0;
    poll_num++;
    if (poll_num % 100 == 0) {
        printf("[POLL] #%d count=%d\n", poll_num, count);
    }
    if (count > 0) {
        printf("[POLL] %d events in FIFO!\n", count);
    }
    if (count == 0) {
        keypad_update_leds(state);
        return;
    }

    // Add 2 extra slots for polling safety (matches Adafruit library behavior)
    uint8_t read_count = count + 2;
    uint8_t events[read_count];

    if (!keypad_read_fifo(events, read_count)) {
        keypad_update_leds(state);
        return;
    }

    // Process each event
    for (int i = 0; i < read_count; i++) {
        uint8_t raw = events[i];
        uint8_t seesaw_key = (raw >> 2) & 0x3F;
        uint8_t edge = raw & 0x03;

        // Convert seesaw key number to NeoTrellis key 0-15
        uint8_t key = NEO_TRELLIS_SEESAW_KEY(seesaw_key);
        if (key >= NEO_TRELLIS_NUM_KEYS) continue;  // invalid

        bool pressed = (edge == SEESAW_KEYPAD_EDGE_RISING);

        // Route based on UI state
        if (ui_state == UI_STATE_INSTRUMENT) {
            // In instrument select mode
            if (pressed) {
                if (key == KEY_INSTRUMENT) {
                    handle_instrument(state);  // toggle back to notes
                } else {
                    handle_instrument_select(key, state);
                }
            }
        } else {
            // Normal note mode
            if (key < 12) {
                // Note key
                if (pressed) {
                    handle_note_press(key, state);
                } else {
                    handle_note_release(state);
                }
            } else if (pressed) {
                // Control keys (only on press, not release)
                switch (key) {
                    case KEY_RECORD:     handle_record(state);     break;
                    case KEY_PLAY:       handle_play(state);       break;
                    case KEY_STOP:       handle_stop(state);       break;
                    case KEY_INSTRUMENT: handle_instrument(state);  break;
                }
            }
        }
    }

    // Refresh LEDs after processing events
    keypad_update_leds(state);
}