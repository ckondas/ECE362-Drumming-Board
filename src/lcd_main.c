#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "lcd.h"
#include <stdio.h>
#include <string.h>
#include <math.h>   

/****************************************** */
#define PIN_SDI    31
#define PIN_CS     29
#define PIN_SCK    30
#define PIN_DC     27
#define PIN_nRESET 28

// Uncomment the following #define when 
// you are ready to run Step 3.

// WARNING: The process will take a VERY 
// long time as it compiles and uploads 
// all the image frames into the uploaded 
// binary!  Expect to wait 5 minutes.
// #define ANIMATION

/****************************************** */
#ifdef ANIMATION
#include "images.h"
#endif
/****************************************** */

void init_spi_lcd() {
    gpio_set_function(PIN_CS, GPIO_FUNC_SIO);
    gpio_set_function(PIN_DC, GPIO_FUNC_SIO);
    gpio_set_function(PIN_nRESET, GPIO_FUNC_SIO);

    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_nRESET, GPIO_OUT);

    gpio_put(PIN_CS, 1); // CS high
    gpio_put(PIN_DC, 0); // DC low
    gpio_put(PIN_nRESET, 1); // nRESET high

    // initialize SPI0 with 12 MHz clock
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SDI, GPIO_FUNC_SPI);
    spi_init(spi1, 12 * 1000 * 1000);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

Picture* load_image(const char* image_data);
void free_image(Picture* pic);

int main() {
    stdio_init_all();
    init_spi_lcd();
    LCD_Setup();
    LCD_Clear(0x0000); // Clear the screen to black

    // Clear everything so we start from scratch
    LCD_Clear(BLACK);

    const char* state1_0 = "From Left to Right";
    const char* state1_1 = "Recording";
    const char* state1_2 = "Playback";
    const char* state1_3 = "Insutrment Select";

    const char* state2_0 = "From Left to Right";
    const char* state2_1 = "Microphone";
    const char* state2_2 = "Sine";
    const char* state2_3 = "Square";
    const char* state2_4 = "Other Options";
    const char* state2_5 = "Saw";
    const char* state2_6 = "Triangle";
    const char* state2_7 = "Back";
    const char* state2_8 = "Return to Menu";

    int select = 2;
    // 0: Idle Menu
    // 1: Instrument Select 1
    // 2: Instrument Select 2

    while(1){
        u8 size = 16;
        u8 scale = 2;
        if(select == 0){
            LCD_DrawString(10, 10, WHITE, BLACK, state1_0, size, 0, scale);
            LCD_DrawString(10, 50, LIGHTBLUE, BLACK, state1_1, size, 0, scale);
            LCD_DrawString(10, 90, LIGHTBLUE, BLACK, state1_2, size, 0, scale);
            LCD_DrawString(10, 130, LIGHTBLUE, BLACK, state1_3, size, 0, scale);
        }
        else if(select == 1){
            LCD_DrawString(10, 10, WHITE, BLACK, state2_0, size, 0, scale);
            LCD_DrawString(10, 50, LIGHTBLUE, BLACK, state2_1, size, 0, scale);
            LCD_DrawString(10, 90, LIGHTBLUE, BLACK, state2_2, size, 0, scale);
            LCD_DrawString(10, 130, LIGHTBLUE, BLACK, state2_3, size, 0, scale);
            LCD_DrawString(10, 170, LIGHTBLUE, BLACK, state2_4, size, 0, scale);
        } 
        else if(select == 2){
            LCD_DrawString(10, 10, WHITE, BLACK, state2_0, size, 0, scale);
            LCD_DrawString(10, 50, LIGHTBLUE, BLACK, state2_5, size, 0, scale);
            LCD_DrawString(10, 90, LIGHTBLUE, BLACK, state2_6, size, 0, scale);
            LCD_DrawString(10, 130, LIGHTBLUE, BLACK, state2_7, size, 0, scale);
            LCD_DrawString(10, 170, LIGHTBLUE, BLACK, state2_8, size, 0, scale);
        }
    }
}
