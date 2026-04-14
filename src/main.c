#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "music_board.h"
#include "keypad.h"

static shared_state_t shared_state;

int main(void) {
    stdio_init_all();
    sleep_ms(1000);  // let USB serial connect
    printf("\nMusic Board Starting:\n");
 
    // Default
    memset(&shared_state, 0, sizeof(shared_state));
    shared_state.current_note = NOTE_NONE;
    shared_state.current_waveform = WAVE_SINE;
    shared_state.system_mode = MODE_IDLE;
    shared_state.active_loop = 0;
 
    // ---- Initialize peripherals on Core 0 ----
 
    // Sami: I2C keypad
    if (!keypad_init()) {
        printf("FATAL: Keypad init failed!\n");
        while (1) sleep_ms(1000);
    }
 
    // Chris: SPI display
    // display_init();
 
    // Geetika: ADC mic
    // mic_init();
 
    // ---- Main loop on Core 0 ----
    printf("Entering main loop.\n");
 
    while (true) {
        // Sami
        keypad_poll(&shared_state);
 
        // Chris
 
        // Geetika

        // Julia
 
        sleep_ms(10);  // ~100Hz poll rate
    }
 
    return 0;
}
