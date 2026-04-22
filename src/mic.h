// #include <stdio.h>
// #include "pico/stdlib.h"
// #include "music_board.h"

// #ifndef __PICO_REC_PLAY_AUDIO_H
// #define __PICO_REC_PLAY_AUDIO_H


// #define BUFF_SIZE                      128

// // Audio buffer 2 channels for 3 seconds @ 22050 Hz Samples/Second.
// #define AUDIO_CHANNELS                 2
// #define AUDIO_PERIOD                   3
// #define WAV_SAMPLE_RATE                22050 // comment out after adding pwm .h file??
// #define WAV_PWM_COUNT                  (125000000 / WAV_SAMPLE_RATE) // comment out after adding pwm .h file??
// #define AUDIO_BUFF_SIZE                (AUDIO_CHANNELS * WAV_SAMPLE_RATE * AUDIO_PERIOD)

// #define ADC_PORT_AUDIO_IN              0

// #define GPIO_KEY_RECORD                7
// #define GPIO_KEY_PLAY                  11
// #define GPIO_KEY_DUMP                  15
// #define GPIO_AUDIO_OUT                 16
// #define GPIO_ADC_AUDIO_IN              40



// void mic_init();
// void mic_capture();
// void AudioCapture(unsigned short AudioBuffer[], shared_state_t *state);


// #endif

// #ifndef __WAV_PWM_AUDIO_H
// #define __WAV_PWM_AUDIO_H


// void WavPwmInit(unsigned char GpioPinChannelA);
// unsigned char WavPwmIsPlaying();
// void WavPwmStopAudio();
// unsigned char WavPwmPlayAudio(const unsigned short WavPwmData[]);


// #endif