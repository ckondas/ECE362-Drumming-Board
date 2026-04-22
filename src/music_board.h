#ifndef MUSIC_BOARD_H
#define MUSIC_BOARD_H

#include <stdint.h>
#include "pico/stdlib.h"
#include "mic.h"



// PINS

// Neotrellis (Sami)
#define PIN_I2C_SDA       2
#define PIN_I2C_SCL       3
#define TRELLIS_ADDR      0x2E

// TFT LCD (Chris)
#define CS_NUM  29
#define DC_NUM 27
#define RESET_NUM 28
#define PIN_SDI    31
#define PIN_CS     29
#define PIN_SCK    30
#define PIN_DC     27
#define PIN_nRESET 28



// Speaker (Julia)
#define PIN_SPEAKER 36



// NOTE DEFINITIONS
typedef enum {
    NOTE_A  = 0,
    NOTE_AS = 1,
    NOTE_B  = 2,
    NOTE_C  = 3,
    NOTE_CS = 4,
    NOTE_D  = 5,
    NOTE_DS = 6,
    NOTE_E  = 7,
    NOTE_F  = 8,
    NOTE_FS = 9,
    NOTE_G  = 10,
    NOTE_GS = 11,
    NOTE_NONE = 0xFF
} note_id_t;

// A4 = 440Hz, octave 4 frequencies in Hz
static const float note_freq[12] = {
    440.00,  // A
    466.16,  // A#
    493.88,  // B
    523.25,  // C
    554.37,  // C#
    587.33,  // D
    622.25,  // D#
    659.25,  // E
    698.46,  // F
    739.99,  // F#
    783.99,  // G
    830.61   // G#
};

// INSTRUMENTS

typedef enum {
  WAVE_SINE     = 0,
  WAVE_SQUARE   = 1,
  WAVE_SAW      = 2,
  WAVE_TRIANGLE = 3,
  WAVE_MIC      = 4,
  WAVE_COUNT    = 5
} waveform_t;

// SYSTEM STATE
typedef enum {
  MODE_IDLE      = 0,
  MODE_RECORDING = 1,
  MODE_PLAYING   = 2,
  MODE_OVERDUB   = 3
} system_mode_t;

// LOOPING
#define MAX_EVENTS 512
#define MAX_LAYERS 4
#define NUM_LOOPS 2

typedef struct {
    uint8_t  note; // Store 12 notes + none = 13 < 255
    uint32_t delta_ms; // Time value in milliseconds
} note_event_t;

typedef struct {
    note_event_t events[MAX_LAYERS][MAX_EVENTS];
    uint16_t     event_count[MAX_LAYERS]; // Store 512 <65,535
    uint8_t      layer_count; // 4 < 255
    uint32_t     duration_ms; // Time value in milliseconds
} loop_t;

// SHARED STATE

typedef struct {
    volatile uint8_t  current_note; // note_id_t
    volatile uint8_t  current_waveform; // waveform_t

    volatile uint8_t  system_mode; // system_mode_t
    volatile uint8_t  prev_mode;
    volatile uint8_t  active_loop; // 0 or 1
    volatile uint8_t  in_instrument_select;
    volatile uint8_t  prev_instrument;

    // Loop data
    loop_t loops[NUM_LOOPS];
} shared_state_t;

#define SHARED_SPINLOCK_NUM  0

// KEYPAD LAYOUT

static const uint8_t key_to_note[12] = {
    NOTE_A, NOTE_AS, NOTE_B,  NOTE_C,
    NOTE_CS, NOTE_D, NOTE_DS, NOTE_E,
    NOTE_F, NOTE_FS, NOTE_G,  NOTE_GS
};

#define KEY_RECORD     12
#define KEY_PLAY       13
#define KEY_STOP       14
#define KEY_INSTRUMENT 15

// Microphone (Geetika)

#define BUFF_SIZE                      128

// Audio buffer 2 channels for 3 seconds @ 22050 Hz Samples/Second.
#define AUDIO_CHANNELS                 2
#define AUDIO_PERIOD                   3
#define WAV_SAMPLE_RATE                22050 // comment out after adding pwm .h file??
#define WAV_PWM_COUNT                  (125000000 / WAV_SAMPLE_RATE) // comment out after adding pwm .h file??
#define AUDIO_BUFF_SIZE                (AUDIO_CHANNELS * WAV_SAMPLE_RATE * AUDIO_PERIOD)

#define ADC_PORT_AUDIO_IN              0

#define GPIO_KEY_RECORD                7
#define GPIO_KEY_PLAY                  11
#define GPIO_KEY_DUMP                  15
#define GPIO_AUDIO_OUT                 16
#define GPIO_ADC_AUDIO_IN              40



void mic_init();
void mic_capture();
void AudioCapture(unsigned short AudioBuffer[], shared_state_t *state);



void WavPwmInit(unsigned char GpioPinChannelA);
unsigned char WavPwmIsPlaying();
void WavPwmStopAudio();
unsigned char WavPwmPlayAudio(const unsigned short WavPwmData[]);

#define PIN_MIC_ADC 40
unsigned short AudioBuffer[AUDIO_BUFF_SIZE];




#endif