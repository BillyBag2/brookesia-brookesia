# brookesia-brookesia

## Introduction

An attempt to create ESP-Brookesia, for a number of boards that I have.

## Target hardware (wish list)

- ESP32-P4
  - [ ] M5 Stack TAB5
  - [ ] Waveshare ESP32-P4 86 box. (I have the relay version)
  - [ ] M5 Stack ESP32-P4 POE. (No screen as standard)

## ESP3-Brookesia reference

- [ESP3-Brookesia](https://github.com/espressif/esp-brookesia) Expressive component source.
- [Waveshare ESP32-P4 Example](https://github.com/waveshareteam/ESP32-P4-Platform/tree/main/examples/esp-idf/18_esp_brookesia_phone)

## Board pages

- [M5 Stack TAB5](./docs/boards/m5_stack_tab5.md)
- [Waveshare ESP32-P4 86 box](./docs/boards/waveshare_esp32_p4_86_box.md)
- [M5 Stack ESP32-P4 POE](./docs/boards/m5_stack_esp32_p4_poe.md)

## Specification table

Manufacturer specifications checked on 2026-09-07. The Waveshare column covers the relay version, **ESP32-P4-86-Panel-ETH-2RO**. The M5 Stack POE board is treated as **Unit PoE-P4 (U213)**. See the linked board pages for sources, pinouts, and schematics.

`?` means unknown or not verified from the sources; it does not mean unsupported. Features describe hardware, not tested ESP-Brookesia support in this project.

| Feature | [M5 Stack TAB5](./docs/boards/m5_stack_tab5.md#references) | [Waveshare ESP32-P4 86 box](./docs/boards/waveshare_esp32_p4_86_box.md#references) | [M5 Stack ESP32-P4 POE](./docs/boards/m5_stack_esp32_p4_poe.md#references) |
|---------|---------------|---------------------------|-----------------------|
| Main processor | ESP32-P4NRW32 | ESP32-P4NRW32 | ESP32-P4NRW32 |
| CPU | Dual-core RISC-V, 360 MHz; LP core, 40 MHz | Dual-core RISC-V, up to 360 MHz; LP core, up to 40 MHz | Dual-core RISC-V, 360 MHz; LP core, 40 MHz |
| Flash | 16 MB | 32 MB NOR | 16 MB |
| PSRAM | 32 MB | 32 MB | 32 MB |
| Screen | 5-inch IPS, 1280 × 720 | 4-inch IPS, 720 × 720 | No screen fitted |
| Display interface | MIPI DSI | 2-lane MIPI DSI | 2-lane MIPI DSI, 24-pin FPC |
| Touch | Yes; fitted controller ? (see board page) | GT911, capacitive, 5-point | External touch interface on display FPC |
| Wireless coprocessor | ESP32-C6-MINI-1U | ESP32-C6-MINI-1U-H8 | ? |
| Wi-Fi | 2.4 GHz Wi-Fi 6 | Wi-Fi 6 | ? |
| Ethernet | No, as standard | RJ45, 10/100 Mbps, IP101 PHY | RJ45, 10/100 Mbps, IP101GRI PHY |
| PoE input | No, as standard | ? | Yes; manufacturer lists IEEE 802.3at, 6 W maximum |
| USB | Type-A host; Type-C OTG | Type-C USB 2.0 HS OTG; Type-C USB-to-UART | Type-C host; Type-C download/OTG |
| Camera | SC2356, 2 MP, MIPI CSI | No camera connector fitted on relay variant | 2-lane MIPI CSI, 24-pin FPC; external camera |
| Audio codec | ES8388 | ES8311 | ? |
| Microphones | Dual; ES7210 | Dual; ES7210 | ? |
| Speaker / audio output | 1 W speaker; 3.5 mm jack | Header for 8-ohm, 2 W speaker | ? |
| microSD slot | Yes | Yes, SDIO 3.0 | ? (SDIO expansion bus provided) |
| RS485 | Yes, SIT3088 | Yes, isolated, automatic direction | ? |
| Relays | ? | 2, optocoupler-isolated | ? |
| Motion sensor | BMI270, 6-axis | ? | ? |
| RTC / backup | RX8130CE; supercapacitor | Rechargeable RTC battery header; RTC implementation ? | ? |
| Power input | USB-C; 6–24 V external; NP-F550 battery | USB-C; 6–30 V DC terminal | USB-C 5 V; PoE |
| Battery | Removable NP-F550; included with Kit only | RTC backup header; main battery ? | ? |
| Expansion | Grove, M5-Bus, GPIO_EXT, Stamp pads | 2.0 mm headers; bottom-board connector | Grove, Hat2-Bus, SDIO-Bus, ISP-Bus |
