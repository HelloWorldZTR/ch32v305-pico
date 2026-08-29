# ch32v305-pico

A compact CH32V305RBT6 development board with Raspberry Pi Pico-compatible
dimensions and castellated holes. It supports USB High Speed and provides ample
GPIO and ADC resources, making it well suited to projects such as keyboards
with an 8 kHz polling rate. All passive components are 0603 or larger for easy
hand soldering. The repository also includes a proven CherryUSB port and
ready-to-use demos.

The board can be reproduced for approximately ¥15/$2. A four-layer PCB with
castellated holes costs considerably more. To keep the prototype inexpensive,
use JLCPCB's free four-layer PCB offer and do not add a special castellated-hole
requirement to the order.

## Hardware

<p align="center">
  <img src="docs/images/3.jpg" alt="Assembled CH32V305 Pico board" width="31%">
  <img src="docs/images/2.jpg" alt="Close-up of the CH32V305RBT6 microcontroller" width="31%">
  <img src="docs/images/1.jpg" alt="CH32V305 Pico bare PCBs" width="31%">
</p>

See [the pin layout and alternate-function diagram](docs/pin-mode.md). Sorry for
the poor soldering job. 😂 Special thanks to JLCPCB for free prototyping.

## CherryUSB / USBHS

The repository includes a reusable CH32V30x CherryUSB port derived from the
upstream CH32V307 implementation. See
[CherryUSB USBHS configuration](docs/cherryusb-usbhs.md) for source attribution,
local fixes, and MounRiver project setup.

Known limitation: the CH32 USBHS device-controller port defaults to 16
bidirectional endpoint numbers and statically reserves a 512-byte OUT plus a
512-byte IN DMA buffer for every non-control endpoint. This consumes about
15 KiB of SRAM even when most endpoints are unused. Small-RAM projects should
define `USB_NUM_BIDIR_ENDPOINTS` to one more than the highest endpoint number
they actually use; see the linked configuration document for details.

## Examples

- [PA8 breathing LED](examples/blink/README.md): drives the onboard user LED
  with TIM1_CH1 hardware PWM and a perceptual brightness curve.
- [CH32V305 USBHS internal-Flash MSC disk](examples/usbhs_udisk/README.md):
  reserves the final 8 KiB of a CH32V305RBT6 as a persistent FAT12 test disk.
