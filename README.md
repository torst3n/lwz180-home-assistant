# Stiebel Eltron LWZ 180 / 280 Home Assistant Integration

This repository contains the firmware and wiring guides to integrate a **Stiebel Eltron LWZ 180 (or LWZ 280)** heat recovery ventilation unit with **Home Assistant** via MQTT and Home Assistant Auto-Discovery.

The integration exposes **14 detailed sensor metrics** and **3 active controls** (Ventilation Level, Scheduled Mode, and Power Venting) to your smart home.

---

## 🏗️ Architecture Overview

The hardware is designed as a **dual-MCU architecture** for absolute timing and protocol reliability:

```text
 [ Stiebel Eltron LWZ 180 ]
          │
          │ (I2C Bus @ 31.25 kHz, General-Call Broadcasts)
          ▼
   [ ISO1540 Galvanic Isolator ]  <─── High-power HVAC protection
          │
          ▼
   [ Arduino Micro (Bridge) ]     <─── Handles TWAR registers & I2C slave timing
          │
          │ (UART Serial Link @ 115200 Baud)
          ▼
   [ 4-Ch Logic Level Shifter ]   <─── Bridges 5V and 3.3V domains
          │
          ▼
   [ ESP32 WROOM DevKit ]         <─── Manages Wi-Fi, MQTT & HA Auto-Discovery
          │
          ▼
    [ Home Assistant ]
```

### Why two microcontrollers?
The LWZ 180 uses a non-standard I2C bus running at **31.25 kHz**, and transmits all telemetry as **general-call I2C broadcasts (address `0x00`)**. 
The ESP32's hardware I2C peripheral does not natively support or process general-call slave broadcasts. The **ATmega32U4 (Arduino Micro)** handles this natively via low-level hardware register configuration (`TWAR`). The Arduino parses these broadcasts and bridges the clean telemetry to the **ESP32** over hardware UART.

---

## ⚡ Galvanic Isolation (Safety First!)

> [!CAUTION]
> **NEVER connect the Stiebel Eltron LWZ 180 motherboard directly to your microcontrollers.**
> Potential ground loops or voltage spikes between your HVAC unit's power supply and your microcontrollers can permanently damage the LWZ main controller board.
>
> You **MUST** use a bidirectional I2C galvanic isolator (like the **DollaTek / Adafruit ISO1540 STEMMA QT board**) to physically isolate the two electrical systems.

* **Side 1 (LWZ Side):** Receives 5V and GND strictly from the LWZ 180 terminal block.
* **Side 2 (Local Side):** Receives 5V and GND from your microcontrollers.
* **DO NOT connect the LWZ Ground to the Arduino/ESP32 Ground.** If you connect them, you will bypass the isolator and lose all protection!

---

## 🔌 Wiring & Pinout Guide

### 1. LWZ 180 Terminal Block ("externe Bedieneinheit")
Locate the terminal block under the main cover of the LWZ 180. The terminals labeled **1 through 8** are dedicated to the external programming unit (FES/FEB) and carry the I2C signals.

Connect the **LWZ terminals** to **Side 1** of your **ISO1540** isolator:

| LWZ Terminal Block Pin | Signal | ISO1540 Board Pin (Side 1) | Wire Purpose |
| :--- | :--- | :--- | :--- |
| **Klemme 1** *(or 2)* | **SCL** | **SCL1** *(or C1)* | I2C Clock Line |
| **Klemme 3** *(or 4)* | **GND** | **GND1** *(or G1)* | Isolated Ground Reference |
| **Klemme 5** *(or 6)* | **+5V DC**| **VCC1** *(or V1)* | Isolated 5V Bus Power |
| **Klemme 7** *(or 8)* | **SDA** | **SDA1** *(or D1)* | I2C Data Line |

---

### 2. Complete Local Wiring Diagram

```text
       [ LWZ 180 SIDE ]                                        [ LOCAL MICROCONTROLLER SIDE ]
    (Galvanically Isolated)                                       (Common Local Ground)
    
     Terminal Block (1-17)
     +-------------------+                                      +------------------------+
     |   LWZ 180 BUS     |                                      |     ARDUINO MICRO      |
     |                   |                                      |                        |
     | Klemme 5 (+5V) ---+----+                            +----+-- Pin 2 (SDA)          |
     | Klemme 7 (SDA) ---+--+ |                            | +--+-- Pin 3 (SCL)          |
     | Klemme 1 (SCL) ---+--|-+--+                      +--|-+--+-- Pin 1 (TX)           |
     | Klemme 3 (GND) ---+--|---|--+                  +-|--|----+-- Pin 0 (RX)           |
     +-------------------+  |   |  |                  | |  |  +-+-- 5V VCC               |
     (Alt: 2=SCL, 4=GND,    |   |  |                  | |  |  +-+-- GND (Local)          |
           6=+5V, 8=SDA)    |   |  |                  | |  |  | +------------------------+
                            |   |  |                  | |  |  |
                            v   v  v                  v v  v  v
                     +----------------------------------------------+
                     |    STEMMA QT / QWIIC ISO1540 BREAKOUT BOARD  |
                     |                                              |
                     |  [Side 1: LWZ Side]     [Side 2: Local Side] |
                     |   Pin/Pad: VCC1          Pin/Pad: VCC2   <---+-- Arduino 5V
                     |   Pin/Pad: SDA1          Pin/Pad: SDA2   <---+-- Arduino SDA (Pin 2)
                     |   Pin/Pad: SCL1          Pin/Pad: SCL2   <---+-- Arduino SCL (Pin 3)
                     |   Pin/Pad: GND1          Pin/Pad: GND2   <---+-- Arduino GND
                     +----------------------------------------------+
                                                                  |
                                       +--------------------------+
                                       |
                                       |     +-----------------------------------+
                                       |     |      4-CHANNEL LEVEL SHIFTER      |
                                       |     |                                   |
                                       |     |     [HIGH SIDE]      [LOW SIDE]   |
                                       +-----+-->  HV  <==========>  LV   <------+--- ESP32 3V3
                                       +-----+-->  GND <==========>  GND  <------+--- ESP32 GND
                                       |     |                                   |
                                       |     |     [UART SHIFTING]               |
         Arduino TX (Pin 1) -----------+-----+-->  HV1 <==========>  LV1  -------+---> ESP32 RX2 (Pin 16)
         Arduino RX (Pin 0) <----------+-----+--<  HV2 <==========>  LV2  <------+--- ESP32 TX2 (Pin 17)
                                             +-----------------------------------+
```

### 💡 Single USB Power Configuration
To power the entire local side with a single micro-USB or USB-C cable:
1. Plug the 5V USB power supply into the **ESP32** USB port.
2. Connect a wire from the ESP32 **`VIN`** pin to the Arduino Micro **`5V`** pin (this supplies 5V to the Arduino, the ISO1540 Side 2, and the Level Shifter High-Side).
3. The common ground is established via the Level Shifter GND lines.

---

## 📊 Home Assistant Entity Reference

All sensors are fully compatible with Home Assistant's long-term historical statistics and **Energy Dashboard** thanks to `state_class` and `device_class` integration.

### Telemetry Sensors (Read-only)
* 🌡️ **Outside Temperature** (`°C`) - *AUL*
* 🌡️ **Supply Air Temperature** (`°C`) - *ZUL*
* 🌡️ **Extract Air Temperature** (`°C`) - *ABL*
* 🌡️ **Exhaust Air Temperature** (`°C`) - *FOL*
* 🌡️ **Dew Point Extract Air** (`°C`)
* 🌡️ **Dew Point Outdoor Air** (`°C`)
* 💧 **Outdoor Humidity** (`%`)
* 💧 **Exhaust Humidity** (`%`)
* 🌀 **Inlet Flow** (`m³/h`)
* 🌀 **Exhaust Flow** (`m³/h`)
* ⚡ **Inlet Fan Power** (`W`)
* ⚡ **Exhaust Fan Power** (`W`)
* ⚡ **Pre-heater Power** (`W`)
* ⏳ **Filter Life Remaining** (`h`)

### System Controls (Read/Write)
* ⚙️ **Ventilation Level** (`0`, `1`, `2`): Number selector to control fan speeds.
* ⚡ **Power Venting** (`0` / `1`): Toggle switch to force temporary maximum venting.
* 📅 **Scheduled Mode** (`0` / `1`): Toggle switch to activate/deactivate internal unit timer programs.

---

## 🤖 Smart Summer / Winter Automations

Paste these automations directly into Home Assistant's Automation Editor (**Three Dots ➔ Edit in YAML**).

### ☀️ 1. Summer Heat Protection (Reduce to 0/1)
*Reduces ventilation to Level `0` (or `1`) when the outside temperature climbs above indoor temperatures, preventing your home from overheating.*

```yaml
alias: "LWZ180: Summer Heat Protection"
description: "Drops ventilation to Level 0 when it is hot outside and hotter than indoors"
trigger:
  - platform: numeric_state
    entity_id: sensor.lwz180_outside_temperature
    above: 24.0
condition:
  # Only run between May and September
  - condition: template
    value_template: "{{ now().month in [5, 6, 7, 8, 9] }}"
  # Verify that outside is indeed hotter than inside (by at least 1.0°C)
  - condition: template
    value_template: >-
      {{ (states('sensor.lwz180_outside_temperature') | float) >
      (states('sensor.lwz180_extract_temperature') | float + 1.0) }}
action:
  - service: number.set_value
    target:
      entity_id: number.lwz180_ventilation_level
    data:
      value: "0"
mode: single
```

### 🌙 2. Summer Night Cooling (Increase to 2)
*Flushes the house with cool night air once the outdoor temperature drops below the indoor temperature.*

```yaml
alias: "LWZ180: Summer Night Cooling"
description: "Increases ventilation to Level 2 when it cools down outside"
trigger:
  - platform: numeric_state
    entity_id: sensor.lwz180_outside_temperature
    below: 23.0
condition:
  # Only run between May and September
  - condition: template
    value_template: "{{ now().month in [5, 6, 7, 8, 9] }}"
  # Only run if the house is actually warm and needs cooling
  - condition: numeric_state
    entity_id: sensor.lwz180_extract_temperature
    above: 21.0
  # Verify outside is cooler than inside (by at least 1.0°C)
  - condition: template
    value_template: >-
      {{ (states('sensor.lwz180_outside_temperature') | float) <
      (states('sensor.lwz180_extract_temperature') | float - 1.0) }}
action:
  - service: number.set_value
    target:
      entity_id: number.lwz180_ventilation_level
    data:
      value: "2"
mode: single
```

### ❄️ 3. Winter Cold Protection (Reduce to 1)
*Protects your indoor humidity from drying out and decreases electric resistance pre-heater bills during cold snaps by reducing to Level `1`.*

```yaml
alias: "LWZ180: Winter Cold Protection"
description: "Reduces ventilation to Level 1 during sustained freezing weather to save energy and retain humidity"
trigger:
  - platform: numeric_state
    entity_id: sensor.lwz180_outside_temperature
    below: -2.0
    for:
      minutes: 15
condition:
  # Only run between October and April
  - condition: template
    value_template: "{{ now().month in [10, 11, 12, 1, 2, 3, 4] }}"
  # Only run if we aren't already on Level 1 (or 0)
  - condition: numeric_state
    entity_id: number.lwz180_ventilation_level
    above: 1
action:
  - service: number.set_value
    target:
      entity_id: number.lwz180_ventilation_level
    data:
      value: "1"
mode: single
```

### 🟢 4. Winter Normal Recovery (Increase to 2)
*Restores standard ventilation when freezing weather subsides.*

```yaml
alias: "LWZ180: Winter Normal Recovery"
description: "Restores ventilation to Level 2 when freezing weather subsides"
trigger:
  - platform: numeric_state
    entity_id: sensor.lwz180_outside_temperature
    above: 0.0
    for:
      minutes: 15
condition:
  # Only run between October and April
  - condition: template
    value_template: "{{ now().month in [10, 11, 12, 1, 2, 3, 4] }}"
  # Only run if we are currently on Level 1 (Reduced)
  - condition: state
    entity_id: number.lwz180_ventilation_level
    state: "1"
action:
  - service: number.set_value
    target:
      entity_id: number.lwz180_ventilation_level
    data:
      value: "2"
mode: single
```

---

## 🚀 Quick Setup Instructions

1. **Configure Credentials:**
   Copy `esp32-lwz180-ha/secrets.h.example` to `esp32-lwz180-ha/secrets.h` and fill in your Wi-Fi SSID, Password, and MQTT broker details.
2. **Flash the Firmware:**
   * Flash `lwz180-bridge/lwz180-bridge.ino` to your Arduino Micro.
   * Flash `esp32-lwz180-ha/esp32-lwz180-ha.ino` to your ESP32.
3. **Confirm Auto-Discovery:**
   Power on your devices. Check your Home Assistant MQTT Integration dashboard—the **LWZ180 Ventilation** device will be automatically discovered with all 14 sensors and 3 controls fully functional!
