# M5 Stack TAB5

[Back to the README](../../README.md#board-pages)

## Overview

The M5 Stack TAB5 has no ethernet as standard.

Specifications in the [README comparison](../../README.md#specification-table) use M5Stack's Tab5 documentation (C145 / K145), checked on 2026-09-07. The standard C145 excludes the battery; K145 is the battery kit. [Source: M5Stack](https://docs.m5stack.com/en/core/Tab5).

The fitted display/touch controller is **?** until the unit's label is checked. M5Stack lists ILI9881C + GT911 and ST7123 / ST7121 variants; this affects driver selection. [Source: pin map and screen-driver change note](https://docs.m5stack.com/en/core/Tab5).

## Pinout

![M5Stack Tab5 pin map](images/m5_stack_tab5_pinmap.png)

Image: M5Stack, [original pin map](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/C145_Pinmap_Overview.png). See the [official PinMap tables](https://docs.m5stack.com/en/core/Tab5#pinmap) for peripheral assignments.

## Schematic

- [Tab5 schematics (PDF)](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf)
- [Overall design block diagram (PDF)](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Overall_Design_Block_Diagram.pdf)

## Battery management

The native board target publishes the TAB5 battery through ESP-Brookesia's
`BatteryIface`. It reads the two-cell pack voltage and signed current from the
INA226 monitor and uses the second GPIO expander for charger status and control.
Positive current means the pack is charging; negative current means it is
discharging.

Supported controls are charging on/off and the hardware's 0.5 A or 1 A charging
rate. The reported percentage is linearly interpolated from a fixed 6.0-8.4 V
two-cell Li-ion voltage curve. It needs no learning or saved history, but it is an
estimate: load, charging, temperature, cell ageing, and the pack protection and
balancing circuit can all shift the voltage. Hardware behaviour and current
polarity must be confirmed on a TAB5 battery kit before relying on the readings
for protection or charging decisions.

## References

- [M5Stack Tab5 documentation](https://docs.m5stack.com/en/core/Tab5) — specifications, power, hardware variants, pin map, and software links.
- [Official ESP-IDF board support package](https://components.espressif.com/components/espressif/m5stack_tab5) — starting point for board integration; project compatibility is not yet verified.
