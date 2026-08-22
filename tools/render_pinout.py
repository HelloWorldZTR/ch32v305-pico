#!/usr/bin/env python3
"""Render the CH32V305 Pico-compatible 40-pin header as an SVG.

Usage:
    python3 tools/render_pinout.py
    python3 tools/render_pinout.py --output docs/assets/pinout.svg
    python3 tools/render_pinout.py --check

The pin data intentionally lives in this small script so the generated SVG has
no runtime dependencies.  Keep docs/pin-mode.md in sync when changing a pin's
primary board-level assignment.
"""

from __future__ import annotations

import argparse
import difflib
from dataclasses import dataclass
from html import escape
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "docs" / "assets" / "pinout.svg"


@dataclass(frozen=True)
class Pin:
    number: int
    signal: str
    mcu_pin: str
    functions: tuple[str, ...] = ()


PINS = (
    Pin(1, "PA2", "16", ("ADC2", "USART2_TX", "TIM2_CH3", "OPA2_OUT")),
    Pin(2, "PA3", "17", ("ADC3", "USART2_RX", "TIM2_CH4", "OPA1_OUT")),
    Pin(3, "GND", "—"),
    Pin(4, "PA0", "14", ("ADC0", "WKUP", "TIM2_CH1")),
    Pin(5, "PA1", "15", ("ADC1", "TIM2_CH2")),
    Pin(6, "PA6", "22", ("ADC6", "SPI1_MISO", "TIM3_CH1")),
    Pin(7, "PA4", "20", ("ADC4", "DAC0", "SPI1_NSS")),
    Pin(8, "GND", "—"),
    Pin(9, "PA5", "21", ("ADC5", "DAC1", "SPI1_SCK")),
    Pin(10, "PA7", "23", ("ADC7", "SPI1_MOSI", "TIM3_CH2")),
    Pin(11, "PC4", "24", ("ADC14", "OPA4_CH1P")),
    Pin(12, "PC5", "25", ("ADC15", "OPA3_CH1P")),
    Pin(13, "GND", "—"),
    Pin(14, "PB0", "26", ("ADC8", "TIM3_CH3")),
    Pin(15, "PB1", "27", ("ADC9", "TIM3_CH4")),
    Pin(16, "PC3", "11", ("ADC13", "TIM10_CH3")),
    Pin(17, "PB12", "33", ("SPI2_NSS", "I2S2_WS", "CAN2_RX")),
    Pin(18, "GND", "—"),
    Pin(19, "PB11", "30", ("I2C2_SDA", "USART3_RX")),
    Pin(20, "PB10", "29", ("I2C2_SCL", "USART3_TX")),
    Pin(21, "PB13", "34", ("SPI2_SCK", "I2S2_CK", "CAN2_TX")),
    Pin(22, "PB14", "35", ("SPI2_MISO", "SDIO_D0")),
    Pin(23, "GND", "—"),
    Pin(24, "PB15", "36", ("SPI2_MOSI", "I2S2_SD", "SDIO_D1")),
    Pin(25, "PC6", "37", ("TIM8_CH1", "TIM3_CH1_RM", "I2S2_MCK")),
    Pin(26, "PC7", "38", ("TIM8_CH2", "TIM3_CH2_RM", "I2S3_MCK")),
    Pin(27, "PC8", "39", ("TIM8_CH3", "SDIO_D0")),
    Pin(28, "GND", "—"),
    Pin(29, "PC9", "40", ("TIM8_CH4", "SDIO_D1")),
    Pin(30, "NRST", "7", ("RUN",)),
    Pin(31, "PC0", "8", ("ADC10",)),
    Pin(32, "PC1", "9", ("ADC11",)),
    Pin(33, "AGND", "12", ("VSSA",)),
    Pin(34, "PC2", "10", ("ADC12",)),
    Pin(35, "ADC_VREF", "13", ("VDDA",)),
    Pin(36, "3V3_OUT", "—", ("LDO_OUT",)),
    Pin(37, "3V3_EN", "—", ("LDO_EN",)),
    Pin(38, "GND", "—"),
    Pin(39, "VSYS", "—", ("LDO_IN",)),
    Pin(40, "VBUS", "—", ("USB_5V",)),
)


COLORS = {
    "gpio": "#2f80ed",
    "adc": "#9b51e0",
    "spi": "#c96a10",
    "i2c": "#d9a514",
    "uart": "#e4572e",
    "timer": "#00a7a7",
    "other": "#718096",
    "ground": "#24292f",
    "power": "#d92d20",
    "control": "#e2b93b",
}

FUNCTION_COLUMNS = {
    "adc": 0,
    "timer": 1,
    "spi": 1,
    "i2c": 2,
    "uart": 3,
    "other": 2,
}
FUNCTION_WIDTH = 118
FUNCTION_GAP = 6


def function_kind(label: str) -> str:
    if label.startswith("ADC") or label in {"VDDA", "VSSA"}:
        return "adc"
    if label.startswith(("SPI", "I2S", "SDIO", "CAN")):
        return "spi"
    if label.startswith("I2C"):
        return "i2c"
    if label.startswith("USART"):
        return "uart"
    if label.startswith("TIM"):
        return "timer"
    return "other"


def signal_kind(signal: str) -> str:
    if signal in {"GND", "AGND"}:
        return "ground"
    if signal in {"3V3_OUT", "ADC_VREF", "VSYS", "VBUS"}:
        return "power"
    if signal in {"NRST", "3V3_EN"}:
        return "control"
    return "gpio"


def function_column(label: str) -> int:
    if label.startswith(("LDO_", "USB_")) or label == "RUN":
        return 0
    return FUNCTION_COLUMNS[function_kind(label)]


def pill(x: float, y: float, label: str, *, align_right: bool) -> str:
    width = FUNCTION_WIDTH
    left = x - width if align_right else x
    color = COLORS[function_kind(label)]
    skew = 10
    points = (
        f"{left + skew:.1f},{y - 15:.1f} {left + width:.1f},{y - 15:.1f} "
        f"{left + width - skew:.1f},{y + 15:.1f} {left:.1f},{y + 15:.1f}"
    )
    return (
        f'<polygon points="{points}" fill="{color}"/><text x="{left + width / 2:.1f}" y="{y + 6:.1f}" '
        f'class="function" text-anchor="middle">{escape(label)}</text>'
    )


def render_pin(pin: Pin, row: int, side: str) -> str:
    y = 150 + row * 54
    is_left = side == "left"
    pad_x = 650 if is_left else 1150
    label_x = 610 if is_left else 1190
    anchor = "end" if is_left else "start"
    number_x = 675 if is_left else 1125
    # Leave a clear gutter between the primary board signal and AF pills.
    function_x = 470 if is_left else 1330
    direction = -1 if is_left else 1
    all_text = ", ".join((f"{pin.signal} (MCU pin {pin.mcu_pin})", *pin.functions))

    parts = [f'<g class="pin"><title>Pin {pin.number}: {escape(all_text)}</title>']
    parts.append(
        f'<circle cx="{pad_x}" cy="{y}" r="16" fill="#d4aa34" stroke="#8a6812" stroke-width="4"/>'
        f'<circle cx="{pad_x}" cy="{y}" r="7" fill="#f7f8fa"/>'
    )
    kind = signal_kind(pin.signal)
    primary_left = label_x - 126 if is_left else label_x
    primary_right = primary_left + 126
    primary_points = (
        f"{primary_left + 11},{y - 18} {primary_right},{y - 18} "
        f"{primary_right - 11},{y + 18} {primary_left},{y + 18}"
    )
    parts.append(
        f'<polygon points="{primary_points}" fill="{COLORS[kind]}"/>'
        f'<text x="{label_x - 10 if is_left else label_x + 10}" y="{y + 7}" '
        f'class="signal" text-anchor="{anchor}">{escape(pin.signal)}</text>'
        f'<text x="{number_x}" y="{y + 6}" class="number" text-anchor="middle">{pin.number}</text>'
    )

    used_columns: set[int] = set()
    for label in pin.functions:
        column = function_column(label)
        while column in used_columns:
            column += 1
        used_columns.add(column)
        cursor = function_x + direction * column * (FUNCTION_WIDTH + FUNCTION_GAP)
        parts.append(pill(cursor, y, label, align_right=is_left))
    parts.append("</g>")
    return "".join(parts)


def render_svg() -> str:
    left = {pin.number: pin for pin in PINS if pin.number <= 20}
    right = {pin.number: pin for pin in PINS if pin.number > 20}
    body = []
    for row in range(20):
        body.append(render_pin(left[row + 1], row, "left"))
        body.append(render_pin(right[40 - row], row, "right"))

    legend = []
    legend_items = (
        ("gpio", "GPIO"), ("adc", "ADC"), ("spi", "SPI / I²S / SDIO / CAN"),
        ("i2c", "I²C"), ("uart", "UART"), ("timer", "Timer"),
        ("power", "Power"), ("ground", "Ground"), ("control", "Control"),
    )
    x = 225
    for key, label in legend_items:
        width = 38 + len(label) * 10
        legend.append(
            f'<rect x="{x}" y="1450" width="24" height="24" rx="6" fill="{COLORS[key]}"/>'
            f'<text x="{x + 32}" y="1468" class="legend">{escape(label)}</text>'
        )
        x += width + 24

    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="2200" height="1500" viewBox="0 0 2200 1500">
<title>CH32V305RBT6 Pico-compatible pinout</title>
<desc>USB is at the top. Left header is numbered 1 to 20 downward; right header is numbered 40 to 21 downward.</desc>
<style>
  text {{ font-family: Inter, "Noto Sans", Arial, sans-serif; }}
  .title {{ font-size: 30px; font-weight: 700; fill: #17202a; }}
  .subtitle {{ font-size: 17px; fill: #52606d; }}
  .signal, .function {{ font-size: 15px; font-weight: 700; fill: white; }}
  .number {{ font-size: 15px; font-weight: 700; fill: #17202a; }}
  .legend {{ font-size: 14px; fill: #263238; }}
</style>
<defs>
  <linearGradient id="usb-shell" x1="0" y1="0" x2="1" y2="0">
    <stop offset="0" stop-color="#a9aaad"/>
    <stop offset="0.13" stop-color="#dadbdd"/>
    <stop offset="0.52" stop-color="#eeeeef"/>
    <stop offset="0.86" stop-color="#d7d8da"/>
    <stop offset="1" stop-color="#96989c"/>
  </linearGradient>
</defs>
<rect width="2200" height="1500" fill="#f7f8fa"/>
<text x="1100" y="42" class="title" text-anchor="middle">CH32V305RBT6 Pico Pinout</text>
<text x="1100" y="70" class="subtitle" text-anchor="middle">USB ↑ · common alternate functions · physical pin view</text>
<g transform="translate(200 65)">
<rect x="640" y="100" width="520" height="1128" rx="42" fill="#126e65" stroke="#0b4944" stroke-width="5"/>
<!-- USB-C receptacle, PCB top view.  The opening faces off-board and is hidden by the metal top shell. -->
<g aria-label="USB-C receptacle">
  <!-- SMT contacts visible below the rear edge of the shell. -->
  <g fill="#d5a62c" stroke="#8b6810" stroke-width="1">
    <rect x="834" y="154" width="9" height="22"/><rect x="847" y="154" width="9" height="22"/>
    <rect x="860" y="154" width="9" height="22"/><rect x="873" y="154" width="9" height="22"/>
    <rect x="886" y="154" width="9" height="22"/><rect x="899" y="154" width="9" height="22"/>
    <rect x="912" y="154" width="9" height="22"/><rect x="925" y="154" width="9" height="22"/>
    <rect x="938" y="154" width="9" height="22"/><rect x="951" y="154" width="9" height="22"/>
    <rect x="964" y="154" width="9" height="22"/>
  </g>
  <!-- Side mounting feet. -->
  <rect x="778" y="126" width="28" height="42" fill="#8d8f93"/>
  <rect x="994" y="126" width="28" height="42" fill="#8d8f93"/>
  <!-- Long rectangular metal can seen from above. -->
  <path d="M802 24 C796 24 792 27 792 33 V147 C792 158 798 164 809 164 H991
           C1002 164 1008 158 1008 147 V33 C1008 27 1004 24 998 24 Z"
        fill="url(#usb-shell)" stroke="#85878b" stroke-width="3"/>
  <!-- Rolled side walls: shadow on the left, highlight and shadow on the right. -->
  <path d="M792 34 C803 49 805 132 793 151 C796 159 801 163 809 164
           C820 139 819 50 808 25 H802 C797 25 793 28 792 34 Z"
        fill="#96989c" opacity="0.48"/>
  <path d="M996 25 C985 52 985 138 995 160 C1003 157 1008 153 1008 147
           V33 C1008 28 1004 25 998 24 Z" fill="#ffffff" opacity="0.62"/>
  <path d="M1002 29 C993 56 993 135 1001 156" fill="none" stroke="#85878b" stroke-width="5" opacity="0.5"/>
  <!-- Rear retention tabs and black shell clips. -->
  <path d="M826 143 L864 139 L865 151 L828 156 Z" fill="#4b4d50"/>
  <path d="M936 139 L974 143 L972 156 L935 151 Z" fill="#4b4d50"/>
  <path d="M790 148 V165 C790 174 799 179 808 176 L820 168" fill="none" stroke="#202225" stroke-width="7"/>
  <path d="M1010 148 V165 C1010 174 1001 179 992 176 L980 168" fill="none" stroke="#202225" stroke-width="7"/>
</g>
<text x="900" y="194" class="signal" text-anchor="middle">USB-C</text>
<!-- Board-level LED connected to PA8. -->
<circle cx="742" cy="258" r="24" fill="#f4d20b" stroke="#c49a00" stroke-width="5"/>
<circle cx="742" cy="258" r="11" fill="#fff7a8"/>
<path d="M742 230 V214" stroke="#43a047" stroke-width="3"/>
<text x="775" y="253" class="signal">USER LED</text>
<text x="775" y="274" class="function">PA8</text>
<rect x="750" y="430" width="300" height="300" rx="24" fill="#263238" transform="rotate(45 900 580)"/>
<text x="900" y="568" class="signal" text-anchor="middle" font-size="20">CH32V305RBT6</text>
<text x="900" y="594" class="function" text-anchor="middle">LQFP64M</text>
<text x="900" y="1168" class="function" text-anchor="middle">51 × 21 mm · 2.54 mm pitch</text>
{''.join(body)}
<!-- Three-pad WCH-Link/SWD header, left to right: SWCLK, GND, SWDIO. -->
<g aria-label="Debug connector: SWCLK, GND, SWDIO">
  <text x="900" y="1200" class="function" text-anchor="middle">DEBUG</text>
  <rect x="842" y="1210" width="28" height="28" rx="4" fill="#d5a62c" stroke="#8b6810" stroke-width="3"/>
  <rect x="886" y="1210" width="28" height="28" rx="4" fill="#d5a62c" stroke="#8b6810" stroke-width="3"/>
  <rect x="930" y="1210" width="28" height="28" rx="4" fill="#d5a62c" stroke="#8b6810" stroke-width="3"/>
  <path d="M856 1238 V1262 M900 1238 V1262 M944 1238 V1262" stroke="#737b87" stroke-width="4"/>
  <g transform="translate(856 1307) rotate(-90)">
    <polygon points="10,-15 112,-15 102,15 0,15" fill="#e57b22"/>
    <text x="56" y="6" class="function" text-anchor="middle">SWCLK</text>
  </g>
  <g transform="translate(900 1307) rotate(-90)">
    <polygon points="10,-15 112,-15 102,15 0,15" fill="#24292f"/>
    <text x="56" y="6" class="function" text-anchor="middle">GND</text>
  </g>
  <g transform="translate(944 1307) rotate(-90)">
    <polygon points="10,-15 112,-15 102,15 0,15" fill="#e57b22"/>
    <text x="56" y="6" class="function" text-anchor="middle">SWDIO</text>
  </g>
</g>
</g>
{''.join(legend)}
</svg>
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true", help="fail if the SVG is stale")
    args = parser.parse_args()
    output = args.output.resolve()
    generated = render_svg()

    if args.check:
        current = output.read_text(encoding="utf-8") if output.exists() else ""
        if current == generated:
            print(f"up to date: {output.relative_to(ROOT)}")
            return 0
        diff = difflib.unified_diff(
            current.splitlines(), generated.splitlines(),
            fromfile=str(output), tofile="generated", lineterm="",
        )
        print("\n".join(diff), file=sys.stderr)
        return 1

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(generated, encoding="utf-8")
    print(f"wrote {output.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
