# CH32V305 Pico logic-pattern generator

A native CH32V305RBT6 logic-pattern generator for Pico Rev 1 and Rev 2. It uses the onboard 8 MHz crystal and a 72 MHz system clock, with a
128 KiB Flash / 32 KiB RAM linker layout and a reserved 2 KiB stack.

TIM2 update requests drive DMA1 channel 2. Each 32-bit DMA write to GPIOA BSHR
atomically sets/resets PA0–PA7 without changing the other GPIOA outputs.
The circular buffer holds 4096 samples (16 KiB). PB0 (header pin 14) toggles
once per buffer cycle; it stays high and output stops after a DMA error.
PB0 is a software interrupt marker, not a synchronous sample clock.

## Build and flash

From the repository root, using the official
[WCH EVT SDK](https://github.com/openwch/ch32v307):

```sh
make -C examples/ch32v305_logic_generator WCH_EVT_ROOT=/path/to/ch32v307/EVT \
  GENERATOR_RATE_HZ=100000 GENERATOR_PATTERN=0
wchisp flash examples/ch32v305_logic_generator/build/ch32v305_logic_generator.elf
```

The build also produces `.bin` and `.map` files. Connect only the board being
flashed in USB ISP mode; see the [two-board demo](../../docs/two-board-demo.md).

| GENERATOR_PATTERN | Output |
|---:|---|
| 0 (default) | Eight-bit incrementing counter, 0–255 |
| 1 | Walking one, 01 / 02 / 04 / … / 80 |
| 2 | Alternating 55 / AA |
| 3 | Eight-bit LFSR, seed A5; restarts at each 4096-sample buffer boundary |
| 4 | All bits high for 16 samples, low for 240 samples |

`GENERATOR_RATE_HZ` accepts 1–8000000, default 100000 samples/s. The timer
rounds to integer prescaler/period values; actual rate is
`72000000 / ((PSC + 1) * (ARR + 1))`. The 100 kHz default is exact relative
to the crystal. Higher rates require hardware validation and short wiring.
Re-running `make` rebuilds the image when these options change.

## Wiring and capture

Connect PA0–PA7 to the analyzer's PA0–PA7 respectively, plus GND. All signals
are 3.3 V; start with 33–100 ohm series resistors at the generator. The GPIO
order differs from the physical header order: see the full
[wiring table](../../docs/two-board-demo.md#shared-wiring).

For the default 100 kHz generator, capture at 1 MHz with 16384 samples.
Expect about ten repeated samples per counter value. Independent generator
and analyzer clocks are not phase-locked, so do not require exactly one
increment per raw analyzer sample. This is a waveform demo, not proof of a
lossless maximum-rate acquisition.
