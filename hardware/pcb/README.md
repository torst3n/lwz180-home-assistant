# Stiebel Eltron LWZ 180 Home Assistant Bridge — Custom PCB

Dieses Verzeichnis enthält die vollständigen Produktionsdaten für das maßgefertigte 2-Layer PCB zur vibrationsfesten und langlebigen Verbindung von ESP32, Arduino Micro, galvanischem I2C-Trenner (ISO1540) und Pegelwandler.

---

## 1. Direkt bestellbares Gerber-Paket

📦 **[`lwz180_bridge_gerbers.zip`](lwz180_bridge_gerbers.zip)** *(Standard RS-274X + Excellon Drill)*

Du kannst dieses ZIP-Archiv direkt per Drag & Drop bei jedem Leiterplattenhersteller hochladen:
* **[JLCPCB.com](https://jlcpcb.com)**: 5 Stück ab ca. **2,00 $** (+ ca. 6 $ Versand, Lieferzeit ~7–10 Tage)
* **[AISLER.net](https://aisler.net)** *(Made in Germany/EU)*: 3 Stück ab ca. **18–22 €** (Lieferzeit ~2–4 Tage)
* **[PCBWay.com](https://pcbway.com)**: 5 Stück ab ca. **5,00 $**

---

## 2. Empfohlene Bestell-Parameter

Beim Hochladen der ZIP-Datei auf JLCPCB / AISLER wählst du folgende Standardwerte:

| Parameter | Empfohlener Wert | Bemerkung |
| :--- | :--- | :--- |
| **Abmessungen** | 90,0 mm × 60,0 mm | Wird aus `lwz180_bridge.GML` automatisch erkannt |
| **Layers** | **2 Layers** | Standard 2-lagig |
| **Material** | **FR-4** | Standard Industriequalität |
| **Dicke (Thickness)** | **1.6 mm** | Standard |
| **Kupfergewicht** | **1 oz** (35 µm) | Standard |
| **Oberflächenveredelung** | **HASL (lead-free)** oder **ENIG (Gold)** | Bleifrei für langlebige Kontakte |
| **Lötstopplack-Farbe** | Grün, Blau oder Mattschwarz | Frei wählbar nach Geschmack |
| **Siebdruck-Farbe** | Weiß (White) | Für beste Lesbarkeit der Pin-Beschriftungen |

---

## 3. Visuelle PCB-Vorschau

* **Oberseite (Bestückungsseite)**: [`pcb_top_render.svg`](pcb_top_render.svg)  
  *(Zeigt Sockel, Footprints, Klemmen und Modul-Beschriftungen)*
* **Unterseite (Lötseite)**: [`pcb_bottom_render.svg`](pcb_bottom_render.svg)  
  *(Zeigt die 5V-, GND- und Signalleitungen)*

---

## 4. Stückliste (Bill of Materials — BOM)

Alle Module werden in Buchsenleisten gesteckt (nichts fest verlötet):

| Designator | Anzahl | Bauteil | Footprint | Bezugsquelle / Empfehlung |
| :--- | :--- | :--- | :--- | :--- |
| **J1** | 1 | 4-Pin Print-Schraubklemme | Rastermaß 5.08 mm | KF301-4P oder Phoenix Contact (Amazon/Reichelt) |
| **U1** | 1 | ISO1540 I2C Isolator Breakout | 2× 4-Pin Buchsenleiste 2.54 mm | Adafruit 4754 / SparkFun Qwiic Isolator |
| **U2** | 1 | Arduino Micro (ATmega32U4) | 2× 17-Pin Buchsenleiste 2.54 mm | Arduino Micro / Pro Micro |
| **U3** | 1 | 4-Kanal Bi-direktionaler Level Shifter | 2× 6-Pin Buchsenleiste 2.54 mm | BSS138 Level Shifter (I2C/UART 3.3V <-> 5V) |
| **U4** | 1 | ESP32 DevKit V1 Modul | 2× 15-Pin Buchsenleiste 2.54 mm | NodeMCU-32S / ESP32 DevKit (30-polig) |
| **H1–H4** | 4 | M3 Montagebohrungen | 3,2 mm Bohrung (6 mm Pad) | Für Abstandsbolzen oder Gehäuse-Montage |

---

## 5. Merkmale des Schaltungsdesigns

1. **Galvanische Trennung (> 6 mm)**:
   * Die Klemme zur Stiebel Eltron LWZ 180 und die Seite 1 des ISO1540 sind durch einen breiten, kupferfreien Isolationsbereich physikalisch vom lokalen ESP32/Arduino-Bereich getrennt.
2. **Einzige Stromversorgung über USB**:
   * Ein USB-Netzteil am ESP32 versorgt über die 5V-Leiterbahn (`VIN` ➔ `5V` Micro ➔ `HV` Level Shifter ➔ `VCC2` ISO1540) die gesamte Platine.
3. **Integrierter Hardware-Reset**:
   * ESP32 GPIO 4 ist über den Level-Shifter-Kanal 3 fest mit dem `RESET`-Pin des Arduino Micro verbunden. Dadurch funktioniert die "HW Reset Bridge"-Taste im Web-Dashboard zuverlässig.
4. **Keine fliegenden Kabel**:
   * Keine Wackelkontakte, keine losen Drähte mehr.
