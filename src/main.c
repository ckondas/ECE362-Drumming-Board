// Core 0: Keypad (Sami), LCD (Chris), Mic (Geetika)
// Core 1: PWM audio output (Julia) (audio can't tolerate jitter, so this way it won't have any interruptions)
 
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "music_board.h"
#include "keypad.h"
// #include "display.h"   // Chris
#include "audio.h"     // Julia
// #include "mic.h"       // Geetika
 
static shared_state_t shared_state;
 
// CORE 1 FOR JULIA
 
void core1_audio_main(void) {
    // - Initialize PWM on PIN_SPEAKER
    // - In a loop:
    // 1. Read shared_state.current_note + current_waveform
    // 2. If a note is active, generate the waveform at the right frequency
    // 3. If system_mode == MODE_PLAYING, iterate through loop_data
    // 4. Mix multiple layers if overdub
    //
    audio_init();
    while (true) {
        if (shared_state.current_note == NOTE_NONE) {
            set_freq(0, 0.0f);
        } 
        else {
            set_freq(0, note_freq[shared_state.current_note]);
        }

        sleep_ms(1);
    }
}
 
// CORE 0 FOR EVERYONE ELSE
 
int main(void) {
    stdio_init_all();
    sleep_ms(1000);  // let USB serial connect
    printf("\nMusic Board Starting: \n");
 
    // Default state on startup
    memset(&shared_state, 0, sizeof(shared_state));
    shared_state.current_note = NOTE_NONE;
    shared_state.current_waveform = WAVE_SINE;
    shared_state.system_mode = MODE_IDLE;
    shared_state.active_loop = 0;
    shared_state.in_instrument_select = 0;
 
    // INITS
 
    // Sami: Keypad
    if (!keypad_init()) {
        printf("FATAL: Keypad init failed!\n");
        while (1) sleep_ms(1000);
    }
 
    // Chris: LCD
    // display_init();
 
    // Geetika: Mic
    // mic_init();
 
    multicore_launch_core1(core1_audio_main);
 
    while (true) {
        // Sami: poll keypad (~10ms intervals)
        keypad_poll(&shared_state);
 
        // Chris: update display based on shared_state
        // display_update(&shared_state);
 
        // Geetika: if recording mic, sample ADC
        // if (shared_state.system_mode == MODE_RECORDING) {
        //     mic_sample(&shared_state);
        // }
 
        sleep_ms(10);  // ~100Hz poll rate
    }
 
    return 0;
}