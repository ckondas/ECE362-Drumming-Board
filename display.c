#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"

uint16_t __attribute__((aligned(16))) msg[8] = {
    (0 << 8) | 0x3F, // seven-segment value of 0
    (1 << 8) | 0x06, // seven-segment value of 1
    (2 << 8) | 0x5B, // seven-segment value of 2
    (3 << 8) | 0x4F, // seven-segment value of 3
    (4 << 8) | 0x66, // seven-segment value of 4
    (5 << 8) | 0x6D, // seven-segment value of 5
    (6 << 8) | 0x7D, // seven-segment value of 6
    (7 << 8) | 0x07, // seven-segment value of 7
};

extern char font[]; // Font mapping for 7-segment display
extern const int SPI_LCD_SCK; extern const int SPI_LCD_CSn; extern const int SPI_LCD_TX;

void display_init_spi() {
    sio_hw->gpio_oe_set = (1u << (SPI_LCD_CSn & 0x1fu));
    sio_hw->gpio_oe_set = (1u << (SPI_LCD_SCK & 0x1fu));
    sio_hw->gpio_oe_set = (1u << (SPI_LCD_TX & 0x1fu));
    sio_hw->gpio_set = (1u << SPI_LCD_SCK) | (1u << SPI_LCD_TX);
    sio_hw->gpio_clr = (1u << SPI_LCD_CSn);
    hw_write_masked(&pads_bank0_hw->io[SPI_LCD_CSn],
                   PADS_BANK0_GPIO0_IE_BITS,
                   PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS
    );
    hw_write_masked(&pads_bank0_hw->io[SPI_LCD_SCK],
                   PADS_BANK0_GPIO0_IE_BITS,
                   PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS
    );
    hw_write_masked(&pads_bank0_hw->io[SPI_LCD_TX],
                   PADS_BANK0_GPIO0_IE_BITS,
                   PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS
    );
    io_bank0_hw->io[SPI_LCD_CSn].ctrl = GPIO_FUNC_SPI;
    io_bank0_hw->io[SPI_LCD_SCK].ctrl = GPIO_FUNC_SPI;
    io_bank0_hw->io[SPI_LCD_TX].ctrl = GPIO_FUNC_SPI;
    hw_clear_bits(&pads_bank0_hw->io[SPI_LCD_CSn], PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits(&pads_bank0_hw->io[SPI_LCD_SCK], PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits(&pads_bank0_hw->io[SPI_LCD_TX], PADS_BANK0_GPIO0_ISO_BITS);
    spi_init(spi1, 125000);
    spi_set_format(spi1, 8, 0, 0, SPI_MSB_FIRST);
}