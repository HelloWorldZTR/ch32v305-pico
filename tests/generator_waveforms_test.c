/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../examples/ch32v305_protocol_generator/waveforms.h"

static uint8_t levels[5];
static uint8_t bits[32];
static unsigned count, starts, stops, selects, deselects;
static uint32_t clock_us, last_rise, last_fall;
static int testing_i2c;

static void delay_us(uint16_t us)
{
    clock_us += us;
}

static void demo_write(enum demo_line line, uint8_t high)
{
    assert(high <= 1);
    if (levels[line] == high) {
        return;
    }
    if (testing_i2c && line == DEMO_SDA && levels[DEMO_SCL]) {
        if (high) stops++; else starts++;
    }
    if (!testing_i2c && line == DEMO_CS) {
        if (high) deselects++; else selects++;
    }
    if (line == (testing_i2c ? DEMO_SCL : DEMO_SCK)) {
        if (high) {
            assert(clock_us - last_fall >= 5);
            assert(count < sizeof(bits));
            if (!testing_i2c) assert(levels[DEMO_CS] == 0);
            bits[count++] = levels[testing_i2c ? DEMO_SDA : DEMO_MOSI];
            last_rise = clock_us;
        } else {
            if (count) assert(clock_us - last_rise >= 5);
            last_fall = clock_us;
        }
    }
    levels[line] = high;
}

static uint8_t byte_at(unsigned offset)
{
    uint8_t value = 0;
    unsigned i;
    for (i = 0; i < 8; i++) value = (uint8_t)((value << 1) | bits[offset + i]);
    return value;
}

static void reset_trace(int i2c)
{
    testing_i2c = i2c;
    levels[DEMO_SDA] = levels[DEMO_SCL] = levels[DEMO_CS] = 1;
    levels[DEMO_SCK] = levels[DEMO_MOSI] = 0;
    count = starts = stops = selects = deselects = 0;
    clock_us = last_rise = last_fall = 0;
}

int main(void)
{
    unsigned value;
    for (value = 0; value < 256; value++) {
        reset_trace(0);
        spi_demo_frame((uint8_t)value);
        assert(count == 16 && selects == 1 && deselects == 1);
        assert(byte_at(0) == value && byte_at(8) == (uint8_t)(value ^ 0xffU));
        assert(levels[DEMO_CS] == 1 && levels[DEMO_SCK] == 0);
        assert(clock_us == 165);

        reset_trace(1);
        i2c_demo_frame((uint8_t)value);
        /* 18 data/ACK clocks plus the rise used to produce STOP. */
        assert(count == 19 && starts == 1 && stops == 1);
        assert(byte_at(0) == 0x84 && byte_at(9) == value);
        assert(bits[8] == 0 && bits[17] == 0);
        assert(levels[DEMO_SDA] == 1 && levels[DEMO_SCL] == 1);
        assert(clock_us == 200);
    }
    puts("Generator waveforms: all 256 payloads passed SPI and I2C checks.");
    return 0;
}
