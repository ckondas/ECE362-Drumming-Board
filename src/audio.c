#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "audio.h"
#include "music_board.h"

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

void set_freq(int chan, float f) {
    if (chan == 0) {
        if (f == 0.0) {
            step0 = 0;
            offset0 = 0;
        } else
            step0 = (f * N / RATE) * (1<<16);
    }
    if (chan == 1) {
        if (f == 0.0) {
            step1 = 0;
            offset1 = 0;
        } else
            step1 = (f * N / RATE) * (1<<16);
    }
}

void pwm_audio_handler(){
    uint slice = pwm_gpio_to_slice_num(PIN_SPEAKER);

    // Acknowledge interrupt
    pwm_clear_irq(slice);

    offset0 += step0;
    offset1 += step1;

    if(offset0 >= N << 16)
        offset0 -= N << 16;
    if(offset1 >= N << 16)
        offset1 -= N << 16;

    short int *current_table;

    switch (shared_state.current_waveform) {
        case WAVE_SINE:     current_table = wavetable_sine; break;
        case WAVE_SQUARE:   current_table = wavetable_square; break;
        case WAVE_SAW:      current_table = wavetable_saw; break;
        case WAVE_TRIANGLE: current_table = wavetable_triangle; break;
        default:            current_table = wavetable_sine; break;
    }

    int samp = current_table[offset0 >> 16];
    
    // Ensure no audio clipping when two samples added
    samp /= 2;
    
    // Scale to range of PWM duty cycle
    samp *= pwm_hw->slice[slice].top;
    samp /= 1 << 16;

    // Write to slice's duty cycle register
    pwm_hw->slice[slice].cc = samp;
}

void audio_init(){
    // Configure pin 36 as PWM output
    gpio_set_function(PIN_SPEAKER, GPIO_FUNC_PWM);

    // Set slice's clock divider value
    uint slice = pwm_gpio_to_slice_num(PIN_SPEAKER);
    pwm_set_clkdiv(slice, 150);
    
    // Set period of PWM signal
    pwm_set_wrap(slice, (1000000 / RATE) - 1);
    
    // Initialize duty cycle to 0
    pwm_set_gpio_level(PIN_SPEAKER, 0);

    init_wavetable();

    // Enable IRQ
    pwm_set_irq_enabled(slice, true);

    // Set handler
    irq_set_exclusive_handler(PWM_IRQ_WRAP_0, pwm_audio_handler);
    
    // Enable interrupt
    irq_set_enabled(PWM_IRQ_WRAP_0, true);
    
    // Enable slice
    pwm_set_enabled(slice, true);
}