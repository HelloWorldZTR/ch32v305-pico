# Two CH32V305 Pico boards: analyzer + generator

Use board A as the USBHS logic analyzer and board B as the signal generator.
Both Rev 1 and Rev 2 use the same header pinout. Flash either the pattern
or protocol generator onto board B; these are separate firmware images.

## Build and flash

Run these commands from the repository root with the MounRiver RISC-V
compiler and the official [WCH EVT SDK](https://github.com/openwch/ch32v307):

```sh
make -C examples/usbhs_logic_analyzer WCH_EVT_ROOT=/path/to/ch32v307/EVT
make -C examples/ch32v305_logic_generator WCH_EVT_ROOT=/path/to/ch32v307/EVT
make -C examples/ch32v305_protocol_generator WCH_EVT_ROOT=/path/to/ch32v307/EVT
```

1. Connect only board A. Hold BOOT while plugging in USB, or hold BOOT and
   RESET, release RESET, then release BOOT. Flash:

   ```sh
   wchisp flash examples/usbhs_logic_analyzer/build/usbhs_logic_analyzer.elf
   ```

2. Disconnect A, connect only B in the same ISP mode, and select one image:

   ```sh
   # Pattern demo:
   wchisp flash examples/ch32v305_logic_generator/build/ch32v305_logic_generator.elf
   # Or protocol demo:
   wchisp flash examples/ch32v305_protocol_generator/build/ch32v305_protocol_generator.elf
   ```

3. Unplug both boards, wire them as below, then power both through USB-C.
   A enumerates as `CH32V305 Pico Logic Analyzer`; B only needs USB power.
   If a board stays in ISP after flashing, reset it without holding BOOT.

## Shared wiring

View the component side with USB-C at the top. Header numbering is shown in
[the pinout](pin-mode.md). Connect the matching GPIO names, not eight
consecutive header positions.

| Board B output | B header pin | Board A input | A header pin | Protocol demo signal |
|---|---:|---|---:|---|
| PA0 | 4 | PA0 / D0 | 4 | I2C SDA |
| PA1 | 5 | PA1 / D1 | 5 | I2C SCL |
| PA2 | 1 | PA2 / D2 | 1 | UART TX |
| PA3 | 2 | PA3 / D3 | 2 | Variable pulse |
| PA4 | 7 | PA4 / D4 | 7 | SPI CS |
| PA5 | 9 | PA5 / D5 | 9 | SPI SCK |
| PA6 | 6 | PA6 / D6 | 6 | PWM |
| PA7 | 10 | PA7 / D7 | 10 | SPI MOSI |
| GND | 3 or 8 | GND | 3 or 8 | Common ground |

Use short wires, with 33–100 Ω series resistors near B on the signal lines.
For the protocol demo, add 4.7 kΩ from B PA0 to B 3V3 (pin 36), and another
4.7 kΩ from B PA1 to B 3V3. These pull-ups may remain for the pattern demo.
When both boards use USB power, leave their 3V3, VSYS and VBUS pins separate.
B's I2C signals include generated ACKs and are for analyzer inputs only.

## Pattern demo

In PulseView, select **Openbench Logic Sniffer & SUMP compatibles** and A's
serial port. Start with **1 MHz, 16384 samples**, without a trigger.
The default generator emits a new counter value at 100 kHz, so each value
lasts about 10 µs (about ten analyzer samples). D0 is the least significant
bit; D7 is the most significant bit.

Select another pattern when building B, for example:

```sh
make -C examples/ch32v305_logic_generator WCH_EVT_ROOT=/path/to/ch32v307/EVT \
  GENERATOR_PATTERN=1 GENERATOR_RATE_HZ=100000
```

Reflash B after rebuilding. Values 0–4 select counter, walking one,
alternating 55/AA, LFSR, and periodic pulses. See the
[pattern README](../examples/ch32v305_logic_generator/README.md).

## Protocol demo

Keep the same signal wiring and flash the protocol generator onto B.
Capture at **1 MHz, 16384 samples** for about 16 ms of data, then add:

| Decoder / measurement | Settings | Expected result |
|---|---|---|
| UART | RX = D2; 115200, 8 data bits, no parity, 1 stop bit; idle high | Incrementing raw bytes |
| SPI | CS = D4, CLK = D5, MOSI = D7; no MISO; mode 0; MSB first; 8 bits | Counter, then complement; e.g. 12 ED |
| I2C | SDA = D0, SCL = D1 | Address 0x42 write, ACK, counter, ACK, STOP |
| PWM | D6 | 1 kHz, 375 µs high / 625 µs low |
| Pulse | D3 | High width cycles through approximately 2–33 µs |

UART, SPI and I2C carry the same counter for each 1 ms burst. Capture may
begin partway through a frame; inspect subsequent complete frames. At
1 MHz the shortest pulses have few samples; try 5 MHz for more detail once
basic acquisition works. The analyzer advertises 16 KiB, giving about
3.28 ms at 5 MHz, enough for multiple protocol bursts.

These settings are bring-up/demo defaults. Host tests and successful builds
do not replace on-board timing and signal-integrity checks. The independent
clocks are not phase-locked: same-rate generator/analyzer captures are not
a reliable one-increment-per-sample validation setup.
