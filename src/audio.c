#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "audio.h"

#define M_PI 3.14159265358979323846
#define PIN_SPEAKER 36

uint16_t wavetable[N];

int step0 = 0;
int offset0 = 0;
int step1 = 0;
int offset1 = 0;
int volume = 2400;

void init_wavetable(void) {
    for(int i=0; i < N; i++)
        wavetable[i] = (16383 * sin(2 * M_PI * i / N)) + 16384;
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

    int samp = wavetable[offset0 >> 16] + wavetable[offset1 >> 16];
    
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
    uint slice = pwm_gpio_to_slice_num(36);
    pwm_set_clkdiv(slice, 150);
    
    // Set period of PWM signal
    pwm_set_wrap(slice, (1000000 / RATE) - 1);
    
    // Initialize duty cycle to 0
    pwm_set_gpio_level(36, 0);

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