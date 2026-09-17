# CH32V305 Pico protocol generator

A native CH32V305RBT6 protocol generator for Pico Rev 1 and Rev 2. Outputs are 3.3 V. It uses the onboard 8 MHz crystal, a 72 MHz
system clock, and a 128 KiB Flash / 32 KiB RAM linker layout.

Every 1 ms it starts UART transmission, emits an SPI frame and an I2C demo
frame, then outputs a variable-width pulse. The counter increments modulo
256 after each burst. PWM runs continuously and the PA8 user LED blinks.
The USB-C port supplies power and supports ISP; the running generator does
not enumerate a USB device.

| Signal | GPIO | Header pin | Analyzer channel | Output |
|---|---|---:|---|---|
| I2C SDA | PA0 | 4 | D0 | Address 0x42 write, then counter; synthetic ACKs |
| I2C SCL | PA1 | 5 | D1 | Nominal 100 kHz |
| UART TX | PA2 | 1 | D2 | 115200 baud, 8N1, raw counter byte |
| Variable pulse | PA3 | 2 | D3 | Nominal 2–33 µs high |
| SPI CS | PA4 | 7 | D4 | Active low for two bytes |
| SPI SCK | PA5 | 9 | D5 | Nominal 100 kHz, mode 0 |
| PWM | PA6 | 6 | D6 | TIM3_CH1, 1 kHz, 37.5% duty |
| SPI MOSI | PA7 | 10 | D7 | Counter then bitwise complement, MSB first |

UART and PWM use hardware peripherals. SPI and I2C waveforms use GPIO with
TIM2-based delays; their nominal 5 µs half-periods and pulse widths include
additional software overhead. The nominal 100 kHz SPI clock makes all eight demo channels easy to inspect
at a 1 MHz capture rate.

**I2C is a synthetic decoder stimulus, not a bus master.** The generator
creates both the data and the low ACK bits, allowing a complete decoded
transaction with only two boards. Connect only analyzer inputs to PA0/PA1;
do not attach a real I2C device. Both outputs are open drain: add a separate
**4.7 kΩ pull-up from each of PA0 and PA1 to the generator's 3V3**. GPIO high
releases the line. No third board or 5 V level translator is required.

## Build and flash

From the repository root, using the official
[WCH EVT SDK](https://github.com/openwch/ch32v307):

```sh
make -C examples/ch32v305_protocol_generator WCH_EVT_ROOT=/path/to/ch32v307/EVT
wchisp flash examples/ch32v305_protocol_generator/build/ch32v305_protocol_generator.elf
```

Connect only the board being flashed in USB ISP mode. See the
[two-board demo](../../docs/two-board-demo.md) for complete wiring, flashing,
and PulseView decoder settings. The same eight signal wires work for the
logic-pattern generator.

## Host waveform check

From the repository root:

```sh
cc -std=c99 -Wall -Wextra -Werror tests/generator_waveforms_test.c \
  -o /tmp/generator-waveforms-test
/tmp/generator-waveforms-test
```

This exercises the actual SPI/I2C waveform functions for all 256 counter
values, checking bit order, chip select, START/STOP, ACK slots and requested
clock delays. It does not measure physical GPIO timing or signal integrity.
