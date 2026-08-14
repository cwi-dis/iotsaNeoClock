# Hardware reference: Elecrow CrowPanel ESP32 1.28" round display

Reference notes for the board being investigated for #4 ("multiple renderers"). Compiled from:

- The vendor's public repo, [Elecrow-RD/CrowPanel--ESP32-Display-1.28-R-inch-240240](https://github.com/Elecrow-RD/CrowPanel--ESP32-Display-1.28-R-inch-240240) (factory firmware source, other example sketches, Eagle schematic `CrowPanel ESP32 Display 1.28(R) Inch_V1.0_240507.sch`).
- Direct queries against a physical unit (`iotsa dfu info`, `espefuse summary`, USB descriptor inspection).

Confidence varies by item — noted per section. Where a claim comes only from the vendor's source/schematic and hasn't been independently exercised, it's marked accordingly.

**Board variants:** the single Eagle schematic appears to cover more than one Elecrow SKU sharing the same PCB, with some parts populated only on certain variants. Confirmed so far: the "extra flash" footprint (`U4`, below) really is silkscreened `U4` on the physical unit, matching the schematic — but that unit doesn't actually have it populated. Separately, this specific unit (the round-clock demo one) doesn't appear to have the rotary encoder populated or used either; Jack has a second variant of this board with a physical pushbutton + rotary encoder that most likely does populate and use it — and quite possibly `U4` too. Treat the encoder section below as "what the shared schematic supports," not as confirmed-present on this particular unit.

## MCU

- **ESP32-C3FH4** (schematic ref `U3`): single-core RISC-V @ 160MHz, 4MB flash embedded in-package (vendor `XMC`), WiFi + BT5 LE.
- Confirmed on the physical unit via `iotsa dfu info` / `espefuse summary`: silicon revision v0.4, 40MHz crystal, MAC `94:a9:90:54:e6:70`.
- Shipped with secure boot off, flash encryption off, no eFuse keys burned, download mode not locked out — open for reflashing.

## Power

- USB-C connector (`J1`, 16-pin) for power and programming.
- `RT9013-30PB` (`U6`): 5V → 3.3V LDO.
- `TP4054` (`U2`, DFN8): single-cell Li-ion/LiPo linear charger, with a `CHRG` status output.
- `B3`, a 1.0mm-pitch 4-pin battery connector (`BAT-1.0MM-W`) — only 2 of the 4 pins are used (`B+`/`B-`); the other two are tied straight to GND. This is almost certainly the 2-pin-looking JST-style connector you spotted — confirms the board is designed to take a LiPo cell and charge it over USB.
- Separately, a **CR927 coin-cell holder** (`B2`) diode-ORed (`D5`, dual-diode) onto the RTC's own supply rail (`VDD_RTC`) — an independent backup power source just for the BM8563 RTC chip and its 32.768kHz crystal, so timekeeping survives even with both USB and the main LiPo disconnected. Don't confuse this with the main LiPo connector — different purpose, different battery.

## Display + touch (round LCD module)

The GC9A01-driven 240×240 round IPS panel and its capacitive touch controller live on their own FPC module, connected to the mainboard via connector `J2`. Not independently verified — this is what the vendor's LovyanGFX init code claims, and the display visibly works, but the pin wiring itself hasn't been cross-checked any other way:

- SPI: `sclk`=GPIO6, `mosi`=GPIO7, `dc`=GPIO2, `cs`=GPIO10 (no `miso`, no dedicated hardware reset pin).
- Touch (CST816D): I2C, shared bus (`SDA`=GPIO4, `SCL`=GPIO5), `INT`=GPIO0. Never exercised in our testing (no touch events observed).
- Both **LCD reset** and **touch reset** are *not* direct GPIOs — both are routed through the IO expander (below). Same for **backlight enable**.

## IO expander — PI4IOE5V6408ZTAEX (`U1`)

I2C, address pin tied to GND (fixed address). This one chip fans out to several things that look at first glance like they'd be direct GPIOs:

| Expander pin | Net name | Drives |
|---|---|---|
| P0 | `MOTOR_P0` | Vibration motor (via transistor `Q3`) |
| P2 | `LED_P2` | LCD backlight enable (via MOSFET `Q1`, pulls `LEDK` low) |
| P3 | `TP_RESET` | Touch controller reset |
| P4 | `LCD_RESET` | LCD reset |
| P1, P5–P7 | — | Unused — factory firmware only configures P0–P4 as outputs |

The expander's `INT` pin exists on the chip but **is not wired to any ESP32-C3 GPIO** in this design — despite the schematic net being labeled `PI4-INT_IO3`, which looks like a GPIO3 reference but isn't connected to the MCU at all (verified from the net's wire list — it only touches the expander's own INT pin). The real GPIO3 net is separately named `BEEP_IO3` (see Buzzer, below) — an unfortunate naming collision in the schematic, not an actual shared pin. Practical upshot: expander state has to be polled over I2C, there's no interrupt-driven path.

This matches the factory firmware's own `set_pin_io()` calls in `setup()` exactly: pin 3 (`TP_RESET`) and pin 4 (`LCD_RESET`) are driven high first, and pin 2 (`LED_P2`, backlight) only after LVGL has finished initializing — avoids flashing garbage on screen before the UI is ready.

## RTC — BM8563EMA (`U7`)

I2C, shared bus, fixed address. 32.768kHz crystal (`Y1`). Backed up independently by the CR927 coin cell described above. Confirmed working — the factory firmware reads it once a minute and we saw the expected serial output.

## Rotary encoder + buttons

- Rotary encoder, part `E5A5-23-12-8` (`U5`): A=GPIO19, B=GPIO18, integrated pushbutton=GPIO8 (`ENCODER_A_PIN`/`ENCODER_B_PIN`/`SWITCH_PIN` in the factory source). Not independently exercised, and per Jack's physical inspection, likely **not populated on this particular unit** — see "Board variants" above. Probably populated and used on the sibling pushbutton+encoder variant instead.
- RESET (`K1`) and BOOT/download (`K2`, GPIO9) buttons — the standard pair for manual bootloader entry, though on this board it's normally unnecessary since the USB bridge chip auto-resets into the bootloader (see below).
- A third, side-mounted "custom" button (`K3`) → GPIO1 (`Custom_PIN`). Confirmed directly wired (not through the expander) from both the factory source and the standalone `Button.ino` vendor example.

## Buzzer

Passive piezo buzzer (`BP1`, part `BEEP_7525`), driven through transistor `Q2`, directly on GPIO3 (`tone()` in the factory firmware). Never triggered in our testing (alarm never fired).

## USB/serial bridge

`CH340X`/`CH340E` (`U9`) — an external WCH USB-to-UART bridge chip, **not** the ESP32-C3's own native USB-Serial-JTAG peripheral. RXD/TXD go to GPIO20/21; DTR/RTS are wired to RESET and BOOT(GPIO9) for automatic bootloader entry (no manual button-holding needed for `iotsa dfu`/`esptool`/`pio upload`).

Independently confirmed from the host side: macOS enumerates the connected device as USB VID `0x1A86` (WCH) — matches the schematic exactly.

**Build-flag consequence:** because this is an external UART bridge and not native USB-CDC, firmware for this board should *not* need `ARDUINO_USB_MODE`/`ARDUINO_USB_CDC_ON_BOOT`-style flags the way a native-USB ESP32-C3/S3 board would — `Serial` here is a plain UART, same as any classic Arduino board.

## Extra flash footprint — confirmed: `U4`, unpopulated on this unit

The schematic includes a **second, discrete 8MB SPI NOR flash chip**, `W25Q64JVXGIQ` (`U4`) — completely separate from the ESP32-C3's own embedded 4MB. It's fully wired in the schematic (all of `DO`/`DI`/`CLK`/`CS`/`WP`/`HOLD` connect to real nets), so this isn't a stub or a leftover — it's a real, designed-in part on an 8-pin package footprint.

No SRAM or PSRAM part appears anywhere in the design, so it was never a SRAM/PSRAM candidate. **Confirmed by physical inspection:** the unpopulated 2×4-pad footprint is silkscreened `U4`, matching this part exactly. Not populated on this unit — per the "Board variants" note above, likely populated on the sibling pushbutton+encoder variant instead, alongside the encoder.

One caveat that still applies even now it's confirmed unpopulated here: this wouldn't show up either way in `iotsa dfu info`'s flash-size report. That only detects the chip's *boot* flash (the embedded 4MB) via the standard bootloader flash-probe protocol — it has no visibility into a second chip sitting on what would be a separate, general-purpose SPI bus.

## Partition table (as shipped)

Read directly off the physical unit via `iotsa dfu info` — not present anywhere in the vendor's repo (no `partitions.csv` committed; this was purely an Arduino IDE build-time choice):

```
nvs        0x9000    0x5000   (20KB)
otadata    0xe000    0x2000   (8KB)
app0       0x10000   0x3c0000 (~3.75MB, single ota_0 slot — no ota_1)
spiffs     0x3d0000  0x20000  (128KB, littlefs)
coredump   0x3f0000  0x10000  (64KB)
```

Single OTA slot only — the factory firmware can be updated in place, but there's no second slot for A/B rollback the way `iotsa`'s own OTA mechanism expects. Whatever partition scheme we pick for an iotsa-based build on this board will need real `ota_0`+`ota_1` slots, and given how much of the 4MB the factory image already claims, actual binary size should be measured early rather than assumed to fit.
