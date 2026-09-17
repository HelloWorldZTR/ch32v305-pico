# CH32V305 USBHS logic analyzer

This example implements an eight-channel SUMP/OLS-compatible logic analyzer
for PulseView and sigrok. Digital inputs `D0..D7` are the low byte of GPIOA.

| Logic channel | MCU GPIO | Pico header pin |
|---:|---|---:|
| D0 | PA0 | 4 |
| D1 | PA1 | 5 |
| D2 | PA2 | 1 |
| D3 | PA3 | 2 |
| D4 | PA4 | 7 |
| D5 | PA5 | 9 |
| D6 | PA6 | 6 |
| D7 | PA7 | 10 |

Header pins 3 and 8 are nearby digital grounds. Inputs must remain between 0 V
and 3.3 V. The board has no input buffer, clamp network, or 5 V conversion.

## Build

Install the MounRiver RISC-V toolchain and obtain the official
[`openwch/ch32v307`](https://github.com/openwch/ch32v307) EVT tree, then run:

```sh
make WCH_EVT_ROOT=/path/to/ch32v307/EVT
```

The result is `build/usbhs_logic_analyzer.bin`. The link map must show less
than 30 KiB of RAM including the 2 KiB stack. The firmware uses USB VID:PID
`1a86:fe14`; this project-local PID has not been allocated by WCH for a
commercial product.

## PulseView

1. Flash the firmware and reconnect the USB-C cable.
2. In PulseView, add **Openbench Logic Sniffer & SUMP compatibles**.
3. Select the serial port exposed by `CH32V305 Pico Logic Analyzer`, using the
   default 115200/8n1 serial settings. CDC line coding does not limit USBHS
   bulk throughput.
4. Start at 1 MHz and 1024 samples. Verify the CH32V305 Pico counter pattern before
   trying higher rates or triggers.

The device reports 16 KiB of usable sample memory and an experimental maximum
of 10 MHz. The firmware runs the core and TIM2 sampling clock from the 120 MHz
HSI PLL so standard SUMP rates such as 10, 5, 2, and 1 MHz have integer timer
dividers. These are bring-up values, not measured performance claims. Only
rates that pass the hardware procedure below should be documented as stable.

## Capture behavior and limitations

- Immediate captures use TIM2 update requests to DMA the low byte of
  `GPIOA->INDR` into SRAM.
- A non-zero stage-0 trigger mask enables a single level/mask trigger. Trigger
  stages 1–3, RLE, demux, filtering, and an external sample clock are ignored.
- Triggered capture uses a 20 KiB physical ring while advertising only 16 KiB.
  The 4 KiB guard absorbs the delay between trigger detection and stopping DMA.
- OLS sends its sample memory newest-first and libsigrok reverses the complete
  response. Consequently, standards-compatible SUMP cannot provide an
  unlimited chronological stream. This implementation favors correct
  PulseView captures over a misleading pseudo-streaming extension.
- On DMA overrun the onboard PA8 LED remains on and the host acquisition times
  out. Send SUMP reset or restart acquisition; no reflashing is needed.

## Hardware validation

For a two-Pico demonstration, use the
[CH32V305 generator and wiring guide](../../docs/two-board-demo.md): start
with a 100 kHz pattern and 1 MHz acquisition.

The strict one-increment-per-sample procedure below requires a controlled,
phase-aligned test source. Two free-running boards at the same nominal rate
can duplicate or miss counter values due to clock drift; those captures do
not isolate analyzer failures. For every candidate rate in such a controlled
setup:

1. Capture at least 16 KiB repeatedly and reverse the SUMP wire order when
   checking raw data.
2. Require every decoded sample to increment modulo 256; any duplicate or jump
   is a failure.
3. Run repeated acquisitions for at least ten minutes.
4. Repeat with walking-one, alternating, LFSR, and pulse patterns.
5. Record generator rate, actual timer divider, wire length, series resistance,
   USB host/controller, and failure count.

For a raw one-byte-per-sample SUMP response, validate counter continuity with:

```sh
python3 tools/validate_counter_capture.py --sump-wire-order capture.bin
```

Do not raise `CAPTURE_MAX_RATE_HZ` or the SUMP metadata until this test passes.
ADC/MSO support is intentionally out of scope for this version.
