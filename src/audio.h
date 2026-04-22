#ifndef AUDIO_H

#define AUDIO_H

 

#include <math.h>

#include <stdint.h>

#include "music_board.h"

 

// Defined this in main.c

#define N 1000 // Size of the wavetable

extern short int wavetable_sine[N];

extern short int wavetable_square[N];

extern short int wavetable_saw[N];

extern short int wavetable_triangle[N];

 

#define RATE 20000

 

// Defined as extern to share between audio.c and main.c

extern int step0;

extern int offset0;

extern int step1;

extern int offset1;

 

// Analog-to-digital conversion for a volume level

extern int volume;

 

void init_wavetable(void);

void set_freq(int chan, float f);

void audio_init(void);

void playback_reset(void);

void playback_tick(void);

 

#endif