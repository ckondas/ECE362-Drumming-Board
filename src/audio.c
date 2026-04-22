#include <stdio.h>

#include <math.h>

#include <stdlib.h>

#include "pico/stdlib.h"

#include "hardware/pwm.h"

#include "hardware/irq.h"

#include "hardware/dma.h"

#include "audio.h"

#include "music_board.h"

#include "mic.h"

 

extern shared_state_t shared_state;

 

#define M_PI 3.14159265358979323846

#define PIN_SPEAKER 36

 

short int wavetable_sine[N];

short int wavetable_square[N];

short int wavetable_saw[N];

short int wavetable_triangle[N];

 

int step0 = 0;

int offset0 = 0;

int step1 = 0;

int offset1 = 0;

int volume = 2400;

 

static uint16_t play_event_index[MAX_LAYERS];

static uint32_t play_loop_start_ms;

 

void init_wavetable(void) {

    for(int i=0; i < N; i++){

        wavetable_sine[i] = (16383 * sin(2 * M_PI * i / N)) + 16384;

        wavetable_square[i] = (i < N/2) ? 32767 : 0;

        wavetable_saw[i] = (i * 32767) / N;

        if (i < N/2)

            wavetable_triangle[i] = (i * 2 * 32767) / N;

        else

            wavetable_triangle[i] = ((N - i) * 2 * 32767) / N;

    }

}

 

void set_freq(int chan, float f){

    if (chan == 0) {

        if (f == 0.0) {

            step0 = 0;

            offset0 = 0;

        } else

            step0 = (f * N / RATE) * (1 << 16);

    }

    if (chan == 1) {

        if (f == 0.0) {

            step1 = 0;

            offset1 = 0;

        } else

            step1 = (f * N / RATE) * (1 << 16);

    }

}

 

void playback_reset(void){

    memset(play_event_index, 0, sizeof(play_event_index));

    play_loop_start_ms = to_ms_since_boot(get_absolute_time());

}

 

void playback_tick(void){

    if (shared_state.system_mode != MODE_PLAYING) {

        set_freq(0, 0.0f);

        set_freq(1, 0.0f);

        return;

    }

 

    loop_t *loop = &shared_state.loops[shared_state.active_loop];

 

    if (loop->duration_ms == 0) return;

 

    uint32_t now = to_ms_since_boot(get_absolute_time());

    uint32_t elapsed = now - play_loop_start_ms;

 

    // Restart loop when duration exceeded

    if (elapsed >= loop->duration_ms) {

        playback_reset();

        elapsed = 0;

    }

 

    for (uint8_t layer = 0; layer < loop->layer_count; layer++) {

        uint16_t count = loop->event_count[layer];

        uint16_t *idx = &play_event_index[layer];

 

        while (*idx < count) {

            note_event_t *ev = &loop->events[layer][*idx];

            if (elapsed >= ev->delta_ms) {

                if (ev->note == NOTE_NONE)

                    set_freq(layer, 0.0f);

                else

                    set_freq(layer, note_freq[ev->note]);

                (*idx)++;

            } else {

                break; // events stored in ascending delta_ms order

            }

        }

    }

}

 

void pwm_audio_handler(void){

    uint slice = pwm_gpio_to_slice_num(PIN_SPEAKER);

    uint chan = pwm_gpio_to_channel(PIN_SPEAKER);

 

    // Acknowledge interrupt

    pwm_clear_irq(slice);

 

    offset0 += step0;

    offset1 += step1;

 

    if(offset0 >= N << 16)

        offset0 -= N << 16;

    if(offset1 >= N << 16)

        offset1 -= N << 16;

 

    uint16_t top = pwm_hw->slice[slice].top;

    uint32_t duty;

 

    uint8_t waveform = shared_state.current_waveform;

 

    if (waveform == WAVE_MIC) {

        // Index through mic buffer; offset0 drives pitch just like wavetables

        // AudioBuffer values are 12-bit (0-4095), scale to 0-top

        uint16_t mic_idx = (offset0 >> 16) % AUDIO_BUFF_SIZE;

        duty = ((uint32_t)AudioBuffer[mic_idx] * top) / 4096;

        pwm_set_chan_level(slice, chan, duty);

        return;

    }

 

    short int *current_table;

 

    switch (waveform) {

        case WAVE_SINE: current_table = wavetable_sine; break;

        case WAVE_SQUARE: current_table = wavetable_square; break;

        case WAVE_SAW: current_table = wavetable_saw; break;

        case WAVE_TRIANGLE: current_table = wavetable_triangle; break;

        default: current_table = wavetable_sine; break;

    }

 

    int samp = (current_table[offset0 >> 16] + current_table[offset1 >> 16]) / 2;

    duty = ((uint32_t)samp * top) >> 15;

    pwm_set_chan_level(slice, chan, duty);

}

 

void audio_init(void) {

    gpio_set_function(PIN_SPEAKER, GPIO_FUNC_PWM);

 

    uint slice = pwm_gpio_to_slice_num(PIN_SPEAKER);

    uint chan = pwm_gpio_to_channel(PIN_SPEAKER);

 

    pwm_set_clkdiv(slice, 6.25f);

    pwm_set_wrap(slice, 1000 - 1);

 

    init_wavetable();

 

    pwm_set_irq_enabled(slice, true);

    irq_set_exclusive_handler(PWM_IRQ_WRAP_0, pwm_audio_handler);

    irq_set_enabled(PWM_IRQ_WRAP_0, true);

    pwm_set_enabled(slice, true);

}