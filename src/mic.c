#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "mic.h"
#include "music_board.h"

// MAX9814 Pins
// AR: unconnected
// Out: ADC channel
// Gain: GND
// VDD: 3v3
// GND: GND


void mic_init()
{
   stdio_init_all();

 
   // Configure Pico on board LED

   gpio_init(PICO_DEFAULT_LED_PIN);
   gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    // Configure ADC
   adc_init(); // IDENTIFY WHICH CHANNEL (Out on max9814)
   adc_gpio_init(GPIO_ADC_AUDIO_IN); 

}



void mic_capture(shared_state_t *state)
{

 // Ensure buffer size data is present

   AudioBuffer[0] = (AUDIO_BUFF_SIZE & 0xFFFF);
   AudioBuffer[1] = (AUDIO_BUFF_SIZE >> 16);
   AudioCapture(AudioBuffer, &state);
   if (state->system_mode != MODE_IDLE || state->system_mode != MODE_OVERDUB)
   {
      state->system_mode = MODE_IDLE;
   };
}

void AudioCapture(unsigned short AudioBuffer[], shared_state_t *state)
{
   unsigned long SampleCount = 0;
   unsigned long long SamplePeriod;

  /*********************************************/
 /* First two 16bit values are the data size. */
/*********************************************/
   SampleCount = 0;
   AudioBuffer[SampleCount++] = (AUDIO_BUFF_SIZE & 0xFFFF);
   AudioBuffer[SampleCount++] = (AUDIO_BUFF_SIZE >> 16);
  /**************************************************************/
 /* Fill audio buffer with values read from the A/D converter. */
/**************************************************************/
   SamplePeriod = time_us_64() + (1000000 / WAV_SAMPLE_RATE);
   while (SampleCount < AUDIO_BUFF_SIZE && state->system_mode != MODE_IDLE && state->system_mode != MODE_OVERDUB)
   {
  /***************************/
 /* Read values @ 22050 Hz. */
/***************************/
      while(time_us_64() < SamplePeriod);
      SamplePeriod = time_us_64() + (1000000 / WAV_SAMPLE_RATE);
  /****************************************************/
 /* Read the current audio level from A/D converter. */
/****************************************************/
      adc_select_input(ADC_PORT_AUDIO_IN);
      AudioBuffer[SampleCount++] = WAV_PWM_COUNT * adc_read() / 4096;
   };
}
