// keypad.c — Adafruit NeoTrellis driver for the RP2350 (Purdue Proton board)
//
// SDA=GP2, SCL=GP3
//
// IDLE <--(4)--> INSTRUMENT_SELECT (sub-mode, flag in shared_state)
// IDLE  --(1)--> RECORD  --(3)--> IDLE
// IDLE  --(2)--> PLAY    --(3)--> IDLE
// PLAY  --(1)--> OVERDUB --(2)--> PLAY
// OVERDUB --(1)--> OVERDUB (commit current layer, start next)
// OVERDUB --(3)--> IDLE
//
//     0  1  2  3     A  A#  B  C
//     4  5  6  7     C# D   D# E
//     8  9 10 11     F  F#  G  G#
//    12 13 14 15     REC PLAY STOP INSTR

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "music_board.h"
#include "keypad.h"

// SEESAW PROTOCALL CONSTANTS

// Module base addresses
#define SEESAW_STATUS_BASE        0x00
#define SEESAW_NEOPIXEL_BASE      0x0E
#define SEESAW_KEYPAD_BASE        0x10

// Status module
#define SEESAW_STATUS_HW_ID       0x01
#define SEESAW_STATUS_SWRST       0x7F

// Keypad module
#define SEESAW_KEYPAD_STATUS      0x00
#define SEESAW_KEYPAD_EVENT       0x01
#define SEESAW_KEYPAD_INTENSET    0x02
#define SEESAW_KEYPAD_INTENCLR    0x03
#define SEESAW_KEYPAD_COUNT       0x04
#define SEESAW_KEYPAD_FIFO        0x10

// Keypad edge codes (as packed into event byte bits [0:1])
#define KEY_EDGE_HIGH             0
#define KEY_EDGE_LOW              1
#define KEY_EDGE_FALLING          2   // key pressed
#define KEY_EDGE_RISING           3   // key released

// Neopixel module
#define SEESAW_NEOPIXEL_STATUS        0x00
#define SEESAW_NEOPIXEL_PIN           0x01
#define SEESAW_NEOPIXEL_SPEED         0x02
#define SEESAW_NEOPIXEL_BUF_LENGTH    0x03
#define SEESAW_NEOPIXEL_BUF           0x04
#define SEESAW_NEOPIXEL_SHOW          0x05

// NeoTrellis specifics
#define NEOTRELLIS_NEOPIXEL_PIN   3
#define NUM_KEYS                  16

// Seesaw keypad layout has gaps: key k (0..15) maps to seesaw input
// (k/4)*8 + (k%4). Inverse is (s/8)*4 + (s%4).
#define NEO_KEY(k)                (((k) / 4) * 8 + ((k) % 4))

static inline uint8_t seesaw_to_key(uint8_t s) {
    return (uint8_t)((s / 8) * 4 + (s % 4));
}

// I2C configuration

#define I2C_PORT        i2c1
#define I2C_BAUDRATE    400000

// -------------------------------------------------------------------
// LED colors — WS2812 is GRB
// -------------------------------------------------------------------

#define COLOR_RGB(r, g, b)  (((uint32_t)(g) << 16) | ((uint32_t)(r) << 8) | (uint32_t)(b))
#define COLOR_OFF           COLOR_RGB(0,  0,  0)
#define COLOR_WHITE         COLOR_RGB(40, 40, 40)
#define COLOR_DIM_WHITE     COLOR_RGB(10, 10, 10)
#define COLOR_DARK_WHITE    COLOR_RGB(2,  2,  2)
#define COLOR_RED           COLOR_RGB(48, 0,  0)
#define COLOR_DIM_RED       COLOR_RGB(12, 0,  0)
#define COLOR_GREEN         COLOR_RGB(0,  48, 0)
#define COLOR_DIM_GREEN     COLOR_RGB(0,  12, 0)
#define COLOR_BLUE          COLOR_RGB(0,  0,  48)
#define COLOR_DIM_BLUE      COLOR_RGB(0,  0,  12)
#define COLOR_YELLOW        COLOR_RGB(36, 36, 0)
#define COLOR_DIM_YELLOW    COLOR_RGB(9,  9,  0)
#define COLOR_DIM_GREEN_2   COLOR_RGB(0,  9,  0)
#define COLOR_MAGENTA       COLOR_RGB(36, 0,  36)
#define COLOR_DIM_MAGENTA   COLOR_RGB(9,  0,  9)

// -------------------------------------------------------------------
// Module-local state
// -------------------------------------------------------------------

static bool           trellis_ok            = false;
static bool           leds_dirty            = true;
static uint8_t        last_pressed_note_key = 0xFF;
static uint32_t       led_cache[NUM_KEYS];
static spin_lock_t   *shared_lock           = NULL;

static uint32_t       record_start_ms       = 0;
static uint32_t       last_event_ms         = 0;
static uint8_t        record_target_layer   = 0;

// -------------------------------------------------------------------
// Forward declarations
// -------------------------------------------------------------------

static bool seesaw_write(uint8_t base, uint8_t func, const uint8_t *data, size_t len);
static bool seesaw_read(uint8_t base, uint8_t func, uint8_t *data, size_t len, uint32_t delay_us);
static void set_pixel(uint8_t key, uint32_t grb_color);
static void refresh_pixels(void);
static void update_leds(shared_state_t *state);
static void handle_key_press(uint8_t key, shared_state_t *state);
static void handle_key_release(uint8_t key, shared_state_t *state);
static void add_recorded_event(shared_state_t *state, uint8_t note);
static void start_recording(shared_state_t *state, uint8_t layer);
static void finalize_recording(shared_state_t *state);

// Short helpers for the shared-state spinlock.
static inline uint32_t lock_acquire(void) {
    return spin_lock_blocking(shared_lock);
}
static inline void lock_release(uint32_t save) {
    spin_unlock(shared_lock, save);
}

// -------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------

bool keypad_init(void) {
    i2c_init(I2C_PORT, I2C_BAUDRATE);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);

    shared_lock = spin_lock_init(SHARED_SPINLOCK_NUM);

    sleep_ms(10);

    // Software reset the Seesaw
    uint8_t reset_val = 0xFF;
    if (!seesaw_write(SEESAW_STATUS_BASE, SEESAW_STATUS_SWRST, &reset_val, 1)) {
        printf("Keypad: seesaw reset write failed (I2C no-ack?)\n");
        return false;
    }
    sleep_ms(500);

    // Configure the neopixel strip
    uint8_t buf_len[2] = {0x00, NUM_KEYS * 3};
    seesaw_write(SEESAW_NEOPIXEL_BASE, SEESAW_NEOPIXEL_BUF_LENGTH, buf_len, 2);
    uint8_t pix_pin = NEOTRELLIS_NEOPIXEL_PIN;
    seesaw_write(SEESAW_NEOPIXEL_BASE, SEESAW_NEOPIXEL_PIN, &pix_pin, 1);

    // Enable both rising (release) and falling (press) events for every key.
    for (int i = 0; i < NUM_KEYS; i++) {
        uint8_t s = NEO_KEY(i);
        uint8_t falling[2] = {s, 0x09};
        seesaw_write(SEESAW_KEYPAD_BASE, SEESAW_KEYPAD_EVENT, falling, 2);
        sleep_ms(1);
        uint8_t rising[2] = {s, 0x11};
        seesaw_write(SEESAW_KEYPAD_BASE, SEESAW_KEYPAD_EVENT, rising, 2);
        sleep_ms(1);
    }

    // Blank all LEDs
    for (int i = 0; i < NUM_KEYS; i++) {
        led_cache[i] = 0xFFFFFFFFu;
    }

    trellis_ok  = true;
    leds_dirty  = true;
    printf("Keypad: init OK\n");
    return true;
}

void keypad_poll(shared_state_t *state) {
    if (!trellis_ok) return;

    // how many events are waiting in FIFO?
    // if none, return
    uint8_t count = 0;
    if (!seesaw_read(SEESAW_KEYPAD_BASE, SEESAW_KEYPAD_COUNT, &count, 1, 500)) {
        return;  
    }

    if (count > 0) {
        if (count > NUM_KEYS) count = NUM_KEYS;  // cap to sane size
        uint8_t events[NUM_KEYS];
        if (!seesaw_read(SEESAW_KEYPAD_BASE, SEESAW_KEYPAD_FIFO, events, count, 1000)) {
            return;
        }

        for (int i = 0; i < count; i++) {
            uint8_t raw  = events[i];
            uint8_t edge = raw & 0x03;
            uint8_t s    = raw >> 2;
            uint8_t k    = seesaw_to_key(s);
            if (k >= NUM_KEYS) continue;

            if (edge == KEY_EDGE_FALLING) {
                handle_key_press(k, state);
            } else if (edge == KEY_EDGE_RISING) {
                handle_key_release(k, state);
            }
        }
    }

    if (leds_dirty) {
        update_leds(state);
        leds_dirty = false;
    }
}

// -------------------------------------------------------------------
// State machine
// -------------------------------------------------------------------

static void handle_key_press(uint8_t key, shared_state_t *state) {
    uint32_t save;

    save = lock_acquire();
    uint8_t mode = state->system_mode;
    bool    in_instr = state->in_instrument_select != 0;
    lock_release(save);

    // Instrument-select
    if (in_instr) {
        if (key <= 4) {
            // Keys 0..4 map to SINE, SQUARE, SAW, TRIANGLE, MIC
            static const uint8_t wf[5] = {
                WAVE_SINE, WAVE_SQUARE, WAVE_SAW, WAVE_TRIANGLE, WAVE_MIC
            };
            save = lock_acquire();
            state->current_waveform = wf[key];
            lock_release(save);
            printf("Keypad: waveform -> %d\n", wf[key]);
            leds_dirty = true;

        } else if (key == KEY_INSTRUMENT) {
            // Exit instrument select back to IDLE.
            save = lock_acquire();
            state->in_instrument_select = 0;
            lock_release(save);
            printf("Keypad: exit instrument select\n");
            leds_dirty = true;
        }
        return;
    }

    // Notes A, A#... G
    if (key < 12) {
        uint8_t note = key_to_note[key];

        save = lock_acquire();
        state->current_note = note;
        lock_release(save);
        printf("%d\n", note);

        last_pressed_note_key = key;

        if (mode == MODE_RECORDING || mode == MODE_OVERDUB) {
            add_recorded_event(state, note);
        }

        leds_dirty = true;
        return;
    }

    // Menu Record, Play, Stop, Instr select
    switch (key) {

    case KEY_RECORD: {
        if (mode == MODE_IDLE) {
            save = lock_acquire();
            uint8_t loop_idx = state->active_loop;
            for (int L = 0; L < MAX_LAYERS; L++) {
                state->loops[loop_idx].event_count[L] = 0;
            }
            state->loops[loop_idx].layer_count = 0;
            state->loops[loop_idx].duration_ms = 0;
            state->system_mode = MODE_RECORDING;
            lock_release(save);

            start_recording(state, 0);
            printf("Keypad: IDLE -> RECORDING (layer 0)\n");

        } else if (mode == MODE_PLAYING) {
            save = lock_acquire();
            uint8_t loop_idx = state->active_loop;
            uint8_t L = state->loops[loop_idx].layer_count;
            if (L >= MAX_LAYERS) L = MAX_LAYERS - 1;
            state->system_mode = MODE_OVERDUB;
            lock_release(save);

            start_recording(state, L);
            printf("Keypad: PLAYING -> OVERDUB (layer %u)\n", L);

        } else if (mode == MODE_OVERDUB) {
            finalize_recording(state);

            save = lock_acquire();
            uint8_t loop_idx = state->active_loop;
            uint8_t L = state->loops[loop_idx].layer_count;
            if (L >= MAX_LAYERS) L = MAX_LAYERS - 1;
            lock_release(save);

            start_recording(state, L);
            printf("Keypad: OVERDUB next layer (%u)\n", L);
        } else if (mode == MODE_RECORDING) {
            finalize_recording(state);

            save = lock_acquire();
            uint8_t loop_idx = state->active_loop;
            uint8_t L = state->loops[loop_idx].layer_count;
            if (L >= MAX_LAYERS) L = MAX_LAYERS - 1;
            lock_release(save);

            start_recording(state, L);
            printf("Keypad: OVERDUB next layer (%u)\n", L);
        }
        leds_dirty = true;
        break;
    }

    case KEY_PLAY: {
        if (mode == MODE_IDLE || mode == MODE_PLAYING) {
            save = lock_acquire();
            state->system_mode = MODE_PLAYING;
            lock_release(save);
            printf("Keypad: -> PLAYING\n");
        } else if (mode == MODE_OVERDUB) {
            finalize_recording(state);
            save = lock_acquire();
            state->system_mode = MODE_PLAYING;
            lock_release(save);
            printf("Keypad: OVERDUB -> PLAYING\n");
        }
        leds_dirty = true;
        break;
    }

    case KEY_STOP: {
        if (mode == MODE_RECORDING || mode == MODE_OVERDUB) {
            finalize_recording(state);
        }
        save = lock_acquire();
        state->system_mode         = MODE_IDLE;
        state->current_note        = NOTE_NONE;
        state->in_instrument_select = 0;
        lock_release(save);
        last_pressed_note_key = 0xFF;
        printf("Keypad: -> IDLE\n");
        leds_dirty = true;
        break;
    }

    case KEY_INSTRUMENT: {
        if (mode == MODE_IDLE) {
            save = lock_acquire();
            state->in_instrument_select = 1;
            lock_release(save);
            printf("Keypad: enter instrument select\n");
            leds_dirty = true;
        }
        break;
    }

    default: break;
    }
}

// AKA stop playing when note is released
static void handle_key_release(uint8_t key, shared_state_t *state) {
    if (key >= 12) return;

    if (key != last_pressed_note_key) return;

    uint32_t save;

    save = lock_acquire();
    state->current_note = NOTE_NONE;
    uint8_t mode = state->system_mode;
    lock_release(save);

    last_pressed_note_key = 0xFF;

    if (mode == MODE_RECORDING || mode == MODE_OVERDUB) {
        add_recorded_event(state, NOTE_NONE);
    }

    leds_dirty = true;
}

// -------------------------------------------------------------------
// Recording helpers
// -------------------------------------------------------------------

static void start_recording(shared_state_t *state, uint8_t layer) {
    record_start_ms = to_ms_since_boot(get_absolute_time());
    last_event_ms = record_start_ms;
    record_target_layer = layer;

    uint32_t save = lock_acquire();
    state->loops[state->active_loop].event_count[layer] = 0;
    lock_release(save);
}

static void add_recorded_event(shared_state_t *state, uint8_t note) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t delta = now - last_event_ms;
    last_event_ms = now;

    uint32_t save = lock_acquire();
    uint8_t  loop_idx = state->active_loop;
    uint8_t  L = record_target_layer;
    uint16_t n = state->loops[loop_idx].event_count[L];

    if (n < MAX_EVENTS) {
        state->loops[loop_idx].events[L][n].note = note;
        state->loops[loop_idx].events[L][n].delta_ms = delta;
        state->loops[loop_idx].event_count[L] = (uint16_t)(n + 1);
    }
    lock_release(save);
}

static void finalize_recording(shared_state_t *state) {
    uint32_t now      = to_ms_since_boot(get_absolute_time());
    uint32_t duration = now - record_start_ms;

    uint32_t save = lock_acquire();
    uint8_t  loop_idx = state->active_loop;

    // First layer
    if (record_target_layer == 0) {
        state->loops[loop_idx].duration_ms = duration;
    }
    // Only bump layer_count if we actually captured something
    if (state->loops[loop_idx].event_count[record_target_layer] > 0) {
        uint8_t needed = (uint8_t)(record_target_layer + 1);
        if (needed > state->loops[loop_idx].layer_count) {
            state->loops[loop_idx].layer_count = needed;
        }
    }
    lock_release(save);
}

// -------------------------------------------------------------------
// LED control
// -------------------------------------------------------------------

static void set_pixel(uint8_t key, uint32_t grb) {
    if (key >= NUM_KEYS) return;
    uint16_t offset = (uint16_t)(key * 3);
    uint8_t payload[5] = {
        (uint8_t)(offset >> 8),
        (uint8_t)(offset & 0xFF),
        (uint8_t)((grb >> 16) & 0xFF),  // G
        (uint8_t)((grb >> 8)  & 0xFF),  // R
        (uint8_t)( grb        & 0xFF),  // B
    };
    seesaw_write(SEESAW_NEOPIXEL_BASE, SEESAW_NEOPIXEL_BUF, payload, 5);
    led_cache[key] = grb;
}

static void refresh_pixels(void) {
    seesaw_write(SEESAW_NEOPIXEL_BASE, SEESAW_NEOPIXEL_SHOW, NULL, 0);
}

static uint32_t waveform_color(uint8_t wf) {
    switch (wf) {
    case WAVE_SINE:     return COLOR_BLUE;
    case WAVE_SQUARE:   return COLOR_RED;
    case WAVE_SAW:      return COLOR_YELLOW;
    case WAVE_TRIANGLE: return COLOR_GREEN;
    case WAVE_MIC:      return COLOR_MAGENTA;
    default:            return COLOR_WHITE;
    }
}

static void update_leds(shared_state_t *state) {
    uint32_t save = lock_acquire();
    uint8_t mode     = state->system_mode;
    uint8_t wf       = state->current_waveform;
    bool    in_instr = state->in_instrument_select != 0;
    lock_release(save);

    uint32_t target[NUM_KEYS];

    if (in_instr) {
        static const uint32_t wf_bright[5] = {
            COLOR_BLUE, COLOR_RED, COLOR_YELLOW, COLOR_GREEN, COLOR_MAGENTA
        };
        static const uint32_t wf_dim[5] = {
            COLOR_DIM_BLUE, COLOR_DIM_RED, COLOR_DIM_YELLOW,
            COLOR_DIM_GREEN_2, COLOR_DIM_MAGENTA
        };
        for (int k = 0; k < NUM_KEYS; k++) target[k] = COLOR_OFF;
        for (int k = 0; k < 5; k++) {
            target[k] = (wf == k) ? wf_bright[k] : wf_dim[k];
        }
        target[KEY_INSTRUMENT] = waveform_color(wf); 
    } else {
        // Normal mode.
        for (int k = 0; k < 12; k++) {
            if (k == last_pressed_note_key) {
                target[k] = COLOR_WHITE;
            } else {
                uint8_t n = key_to_note[k];
                // bool sharp = (n == NOTE_AS || n == NOTE_CS || n == NOTE_DS ||
                //               n == NOTE_FS || n == NOTE_GS);
                // target[k] = sharp ? COLOR_DARK_WHITE : COLOR_DIM_WHITE;
                target[k] = COLOR_DIM_WHITE;
            }
        }
        target[KEY_RECORD]     = (mode == MODE_RECORDING || mode == MODE_OVERDUB)
                                 ? COLOR_RED   : COLOR_DIM_RED;
        target[KEY_PLAY]       = (mode == MODE_PLAYING   || mode == MODE_OVERDUB)
                                 ? COLOR_GREEN : COLOR_DIM_GREEN;
        target[KEY_STOP]       = COLOR_DIM_WHITE;
        target[KEY_INSTRUMENT] = waveform_color(wf);
    }

    bool changed = false;
    for (int k = 0; k < NUM_KEYS; k++) {
        if (target[k] != led_cache[k]) {
            set_pixel((uint8_t)k, target[k]);
            changed = true;
        }
    }
    if (changed) refresh_pixels();
}

// -------------------------------------------------------------------
// Low-level Seesaw I2C helpers
// -------------------------------------------------------------------

static bool seesaw_write(uint8_t base, uint8_t func, const uint8_t *data, size_t len) {
    uint8_t buf[32];
    if (len + 2 > sizeof(buf)) return false;

    buf[0] = base;
    buf[1] = func;
    if (len > 0 && data != NULL) {
        memcpy(&buf[2], data, len);
    }
    int n = i2c_write_blocking(I2C_PORT, TRELLIS_ADDR, buf, (uint)(2 + len), false);
    return n == (int)(2 + len);
}

static bool seesaw_read(uint8_t base, uint8_t func, uint8_t *data, size_t len, uint32_t delay_us) {
    // Seesaw requires: write [base, func], wait, then restart-read the bytes.
    uint8_t cmd[2] = {base, func};
    int w = i2c_write_blocking(I2C_PORT, TRELLIS_ADDR, cmd, 2, false);
    if (w != 2) return false;

    // The chip needs time to prepare the response. 125us is the library
    // default; we give it more to be safe.
    sleep_us(delay_us);

    int r = i2c_read_blocking(I2C_PORT, TRELLIS_ADDR, data, (uint)len, false);
    return r == (int)len;
}