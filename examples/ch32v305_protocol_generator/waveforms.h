/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DEMO_WAVEFORMS_H
#define DEMO_WAVEFORMS_H

#include <stdint.h>

enum demo_line { DEMO_SDA, DEMO_SCL, DEMO_CS, DEMO_SCK, DEMO_MOSI };

/* Implemented by the board, or by the host waveform test. */
static void demo_write(enum demo_line line, uint8_t high);
static void delay_us(uint16_t us);

static void spi_demo_byte(uint8_t value)
{
    unsigned bit;
    for (bit = 0; bit < 8; bit++) {
        demo_write(DEMO_MOSI, (value & 0x80U) != 0U);
        delay_us(5);
        demo_write(DEMO_SCK, 1);
        delay_us(5);
        demo_write(DEMO_SCK, 0);
        value <<= 1;
    }
}

static void spi_demo_frame(uint8_t counter)
{
    /* Mode 0, MSB first, nominal 100 kHz; GPIO/software overhead adds time. */
    demo_write(DEMO_CS, 0);
    spi_demo_byte(counter);
    spi_demo_byte((uint8_t)~counter);
    delay_us(5);
    demo_write(DEMO_CS, 1);
}

static void i2c_demo_byte(uint8_t value)
{
    unsigned bit;
    for (bit = 0; bit < 8; bit++) {
        demo_write(DEMO_SDA, (value & 0x80U) != 0U);
        delay_us(5);
        demo_write(DEMO_SCL, 1);
        delay_us(5);
        demo_write(DEMO_SCL, 0);
        value <<= 1;
    }
    /* Synthetic slave ACK: this is a decoder stimulus, not an I2C master.
     * Connect only analyzer inputs, never an actual I2C slave or master. */
    demo_write(DEMO_SDA, 0);
    delay_us(5);
    demo_write(DEMO_SCL, 1);
    delay_us(5);
    demo_write(DEMO_SCL, 0);
}

static void i2c_demo_frame(uint8_t counter)
{
    /* Idle SDA/SCL are released high through external 3.3 V pull-ups. */
    demo_write(DEMO_SDA, 0); /* START */
    delay_us(5);
    demo_write(DEMO_SCL, 0);
    i2c_demo_byte(0x42U << 1); /* Seven-bit address 0x42, write. */
    i2c_demo_byte(counter);
    demo_write(DEMO_SDA, 0);
    delay_us(5);
    demo_write(DEMO_SCL, 1);
    delay_us(5);
    demo_write(DEMO_SDA, 1); /* STOP */
    delay_us(5);
}

#endif
