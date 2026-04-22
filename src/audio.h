#ifndef AUDIO_H
#define AUDIO_H

#include <math.h>
#include <stdint.h>
#include "music_board.h"

#define N 1000 
extern short int wavetable_sine[N];
extern short int wavetable_square[N];
extern short int wavetable_saw[N];
extern short int wavetable_triangle[N];

#define RATE 20000

extern int step0;
extern int offset0;
extern int step1;
extern int offset1;

extern int volume;
extern int mic_dma_ch;

void init_wavetable(void);
void set_freq(int chan, float f);
void audio_init(void);
void playback_reset(void);
void playback_tick(void);
void mic_playback_start(void);
void mic_playback_stop(void);

#endif