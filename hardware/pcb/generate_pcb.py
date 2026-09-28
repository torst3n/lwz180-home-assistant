#!/usr/bin/env python3
"""
Custom PCB & Gerber Generator for Stiebel Eltron LWZ 180 Home Assistant Bridge
Generates:
1. RS-274X Gerber Layers (GTL, GBL, GTS, GBS, GTO, GBO, GML)
2. Excellon Drill file (.DRL)
3. Ready-to-order ZIP package (lwz180_bridge_gerbers.zip)
4. High-resolution PCB Top & Bottom Render SVGs (pcb_top_render.svg, pcb_bottom_render.svg)
5. Bill of Materials (BOM.csv)
"""

import os
import zipfile
import math

OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))

# Board Dimensions (in mm)
BOARD_W = 90.0
BOARD_H = 60.0
CORNER_R = 3.0

# Colors for SVGs
COLOR_PCB_GREEN = "#1b4d2e"
COLOR_COPPER_GOLD = "#d4af37"
COLOR_SILK_WHITE = "#ffffff"
COLOR_MASK_MATTE = "#163d24"
COLOR_HOLE = "#12151a"

class GerberWriter:
    def __init__(self, filename, comment=""):
        self.filename = filename
        self.lines = [
            f"G04 {comment}*",
            "%FSLAX46Y46*%",
            "%MOMM*%",
            "%LPD*%"
        ]
        self.apertures = {}
        self.next_aperture = 10
        self.current_aperture = None

    def add_aperture(self, shape, *dims):
        key = (shape, dims)
        if key not in self.apertures:
            d_code = self.next_aperture
            self.next_aperture += 1
            if shape == "C": # Circle (diameter)
                self.lines.append(f"%ADD{d_code}C,{dims[0]:.4f}*%")
            elif shape == "R": # Rectangle (width, height)
                self.lines.append(f"%ADD{d_code}R,{dims[0]:.4f}X{dims[1]:.4f}*%")
            elif shape == "O": # Oval
                self.lines.append(f"%ADD{d_code}O,{dims[0]:.4f}X{dims[1]:.4f}*%")
            self.apertures[key] = d_code
        return self.apertures[key]

    def set_aperture(self, d_code):
        if self.current_aperture != d_code:
            self.lines.append(f"D{d_code}*")
            self.current_aperture = d_code

    def format_coord(self, val):
        # 4.6 format (in mm)
        int_val = int(round(val * 1000000))
        return f"{int_val:010d}"

    def flash(self, x, y, d_code):
        self.set_aperture(d_code)
        self.lines.append(f"X{self.format_coord(x)}Y{self.format_coord(y)}D03*")

    def line(self, x1, y1, x2, y2, d_code):
        self.set_aperture(d_code)
        self.lines.append(f"X{self.format_coord(x1)}Y{self.format_coord(y1)}D02*")
        self.lines.append(f"X{self.format_coord(x2)}Y{self.format_coord(y2)}D01*")

    def poly_rect(self, x1, y1, x2, y2):
        self.lines.append("G36*")
        self.lines.append(f"X{self.format_coord(x1)}Y{self.format_coord(y1)}D02*")
        self.lines.append(f"X{self.format_coord(x2)}Y{self.format_coord(y1)}D01*")
        self.lines.append(f"X{self.format_coord(x2)}Y{self.format_coord(y2)}D01*")
        self.lines.append(f"X{self.format_coord(x1)}Y{self.format_coord(y2)}D01*")
        self.lines.append(f"X{self.format_coord(x1)}Y{self.format_coord(y1)}D01*")
        self.lines.append("G37*")

    def save(self):
        self.lines.append("M02*")
        filepath = os.path.join(OUTPUT_DIR, self.filename)
        with open(filepath, "w") as f:
            f.write("\n".join(self.lines) + "\n")
        return filepath


class ExcellonWriter:
    def __init__(self, filename):
        self.filename = filename
        self.tools = {} # diameter -> tool_id
        self.hits = {}  # tool_id -> [(x, y)]
        self.next_tool = 1

    def add_hit(self, x, y, diameter):
        if diameter not in self.tools:
            t_id = self.next_tool
            self.next_tool += 1
            self.tools[diameter] = t_id
            self.hits[t_id] = []
        t_id = self.tools[diameter]
        self.hits[t_id].append((x, y))

    def save(self):
        lines = [
            "M48",
            "METRIC,TZ",
        ]
        for dia, t_id in sorted(self.tools.items(), key=lambda x: x[1]):
            lines.append(f"T{t_id:02d}C{dia:.3f}")
        lines.append("%")
        lines.append("G90")
        lines.append("G05")

        for dia, t_id in sorted(self.tools.items(), key=lambda x: x[1]):
            lines.append(f"T{t_id:02d}")
            for x, y in self.hits[t_id]:
                # Format 4.3 mm or 3.3
                x_val = int(round(x * 1000))
                y_val = int(round(y * 1000))
                lines.append(f"X{x_val:06d}Y{y_val:06d}")
        lines.append("M30")

        filepath = os.path.join(OUTPUT_DIR, self.filename)
        with open(filepath, "w") as f:
            f.write("\n".join(lines) + "\n")
        return filepath


def generate_pcb():
    print(f"Generating Custom PCB files in {OUTPUT_DIR}...")

    # Data structures for SVG render
    pads_data = []      # (x, y, pad_dia, hole_dia, net, label)
    traces_top = []     # (x1, y1, x2, y2, width, net)
    traces_bottom = []  # (x1, y1, x2, y2, width, net)
    silkscreen_lines = []
    silkscreen_texts = []

    # 1. Initialize Gerber Writers
    gtl = GerberWriter("lwz180_bridge.GTL", "Top Copper")
    gbl = GerberWriter("lwz180_bridge.GBL", "Bottom Copper")
    gts = GerberWriter("lwz180_bridge.GTS", "Top Solder Mask")
    gbs = GerberWriter("lwz180_bridge.GBS", "Bottom Solder Mask")
    gto = GerberWriter("lwz180_bridge.GTO", "Top Silkscreen")
    gbo = GerberWriter("lwz180_bridge.GBO", "Bottom Silkscreen")
    gml = GerberWriter("lwz180_bridge.GML", "Board Outline")
    drl = ExcellonWriter("lwz180_bridge.DRL")

    # Apertures
    ap_outline = gml.add_aperture("C", 0.15)
    ap_trace_pwr = gbl.add_aperture("C", 0.8) # 0.8mm for power
    ap_trace_sig_b = gbl.add_aperture("C", 0.4) # 0.4mm bottom signal
    ap_trace_sig_t = gtl.add_aperture("C", 0.4) # 0.4mm top signal
    ap_silk = gto.add_aperture("C", 0.18)

    # Standard Pads
    pad_header = gtl.add_aperture("C", 1.7)
    gbl.add_aperture("C", 1.7)
    mask_header = gts.add_aperture("C", 1.85)
    gbs.add_aperture("C", 1.85)

    # Screw Terminal Pads
    pad_term = gtl.add_aperture("C", 2.8)
    gbl.add_aperture("C", 2.8)
    mask_term = gts.add_aperture("C", 3.0)
    gbs.add_aperture("C", 3.0)

    # Mounting Holes (M3)
    pad_m3 = gtl.add_aperture("C", 6.0)
    gbl.add_aperture("C", 6.0)
    mask_m3 = gts.add_aperture("C", 6.2)
    gbs.add_aperture("C", 6.2)

    # --- BOARD OUTLINE (GML) ---
    gml.line(CORNER_R, 0, BOARD_W - CORNER_R, 0, ap_outline)
    gml.line(BOARD_W, CORNER_R, BOARD_W, BOARD_H - CORNER_R, ap_outline)
    gml.line(BOARD_W - CORNER_R, BOARD_H, CORNER_R, BOARD_H, ap_outline)
    gml.line(0, BOARD_H - CORNER_R, 0, CORNER_R, ap_outline)

    # --- MOUNTING HOLES ---
    m_holes = [(4.0, 4.0), (BOARD_W - 4.0, 4.0), (4.0, BOARD_H - 4.0), (BOARD_W - 4.0, BOARD_H - 4.0)]
    for x, y in m_holes:
        drl.add_hit(x, y, 3.2)
        gtl.flash(x, y, pad_m3)
        gbl.flash(x, y, pad_m3)
        gts.flash(x, y, mask_m3)
        gbs.flash(x, y, mask_m3)
        pads_data.append((x, y, 6.0, 3.2, "MOUNT", "M3"))

    # --- COMPONENT 1: SCREW TERMINAL BLOCK (J1 - 4 Pin 5.08mm) ---
    term_x = 8.0
    term_start_y = 22.38
    j1_pins = {}
    for i, label in enumerate(["1:SCL", "2:GND", "3:5V", "4:SDA"]):
        py = term_start_y + (3 - i) * 5.08 # Pin 1 at top, Pin 4 at bottom
        drl.add_hit(term_x, py, 1.4)
        gtl.flash(term_x, py, pad_term)
        gbl.flash(term_x, py, pad_term)
        gts.flash(term_x, py, mask_term)
        gbs.flash(term_x, py, mask_term)
        j1_pins[i + 1] = (term_x, py)
        pads_data.append((term_x, py, 2.8, 1.4, f"J1_{i+1}", label))

    # Silkscreen for J1
    gto.line(term_x - 4.0, term_start_y - 3.5, term_x + 4.0, term_start_y - 3.5, ap_silk)
    gto.line(term_x + 4.0, term_start_y - 3.5, term_x + 4.0, term_start_y + 18.8, ap_silk)
    gto.line(term_x + 4.0, term_start_y + 18.8, term_x - 4.0, term_start_y + 18.8, ap_silk)
    gto.line(term_x - 4.0, term_start_y + 18.8, term_x - 4.0, term_start_y - 3.5, ap_silk)

    # --- COMPONENT 2: ISO1540 BREAKOUT (U1) ---
    # Adafruit/Standard ISO1540 breakout (2x4 pins, 15.24mm row spacing)
    iso_left_x = 20.0
    iso_right_x = 35.24
    iso_start_y = 26.19
    u1_side1 = {} # 1: VCC1, 2: GND1, 3: SDA1, 4: SCL1
    u1_side2 = {} # 1: VCC2, 2: GND2, 3: SDA2, 4: SCL2

    for i, label in enumerate(["VCC1", "GND1", "SDA1", "SCL1"]):
        py = iso_start_y + (3 - i) * 2.54
        drl.add_hit(iso_left_x, py, 1.0)
        gtl.flash(iso_left_x, py, pad_header)
        gbl.flash(iso_left_x, py, pad_header)
        gts.flash(iso_left_x, py, mask_header)
        gbs.flash(iso_left_x, py, mask_header)
        u1_side1[label] = (iso_left_x, py)
        pads_data.append((iso_left_x, py, 1.7, 1.0, f"U1_{label}", label))

    for i, label in enumerate(["VCC2", "GND2", "SDA2", "SCL2"]):
        py = iso_start_y + (3 - i) * 2.54
        drl.add_hit(iso_right_x, py, 1.0)
        gtl.flash(iso_right_x, py, pad_header)
        gbl.flash(iso_right_x, py, pad_header)
        gts.flash(iso_right_x, py, mask_header)
        gbs.flash(iso_right_x, py, mask_header)
        u1_side2[label] = (iso_right_x, py)
        pads_data.append((iso_right_x, py, 1.7, 1.0, f"U1_{label}", label))

    # --- GALVANIC ISOLATION BARRIER SILK & KEEPOUT ---
    iso_barrier_x = 27.62
    gto.line(iso_barrier_x, 6.0, iso_barrier_x, BOARD_H - 6.0, ap_silk)

    # --- COMPONENT 3: ARDUINO MICRO (U2) ---
    # 2 rows of 17 pins (pitch 2.54mm, row spacing 15.24mm)
    am_start_x = 46.0
    am_top_y = 12.0
    am_bot_y = 27.24
    am_pins = {}

    top_labels = ["MOSI", "SS", "TX1", "RX0", "RST", "GND", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10", "D11", "D12"]
    for i, label in enumerate(top_labels):
        px = am_start_x + i * 2.54
        drl.add_hit(px, am_top_y, 1.0)
        gtl.flash(px, am_top_y, pad_header)
        gbl.flash(px, am_top_y, pad_header)
        gts.flash(px, am_top_y, mask_header)
        gbs.flash(px, am_top_y, mask_header)
        am_pins[label] = (px, am_top_y)
        pads_data.append((px, am_top_y, 1.7, 1.0, f"U2_{label}", label))

    bot_labels = ["VIN", "NC1", "NC2", "5V", "NC3", "NC4", "AREF", "3V3", "D13", "A0", "A1", "A2", "A3", "A4", "A5", "NC5", "NC6"]
    for i, label in enumerate(bot_labels):
        px = am_start_x + i * 2.54
        drl.add_hit(px, am_bot_y, 1.0)
        gtl.flash(px, am_bot_y, pad_header)
        gbl.flash(px, am_bot_y, pad_header)
        gts.flash(px, am_bot_y, mask_header)
        gbs.flash(px, am_bot_y, mask_header)
        am_pins[label] = (px, am_bot_y)
        pads_data.append((px, am_bot_y, 1.7, 1.0, f"U2_{label}", label))

    # --- COMPONENT 4: 4-CHANNEL LEVEL SHIFTER (U3) ---
    # 2 rows of 6 pins (pitch 2.54mm, row spacing 12.7mm)
    ls_start_x = 51.08
    ls_top_y = 35.0  # High side (5V)
    ls_bot_y = 47.7  # Low side (3.3V)
    ls_pins = {}

    ls_top_labels = ["HV", "GND_H", "HV1", "HV2", "HV3", "HV4"]
    for i, label in enumerate(ls_top_labels):
        px = ls_start_x + i * 2.54
        drl.add_hit(px, ls_top_y, 1.0)
        gtl.flash(px, ls_top_y, pad_header)
        gbl.flash(px, ls_top_y, pad_header)
        gts.flash(px, ls_top_y, mask_header)
        gbs.flash(px, ls_top_y, mask_header)
        ls_pins[label] = (px, ls_top_y)
        pads_data.append((px, ls_top_y, 1.7, 1.0, f"U3_{label}", label))

    ls_bot_labels = ["LV", "GND_L", "LV1", "LV2", "LV3", "LV4"]
    for i, label in enumerate(ls_bot_labels):
        px = ls_start_x + i * 2.54
        drl.add_hit(px, ls_bot_y, 1.0)
        gtl.flash(px, ls_bot_y, pad_header)
        gbl.flash(px, ls_bot_y, pad_header)
        gts.flash(px, ls_bot_y, mask_header)
        gbs.flash(px, ls_bot_y, mask_header)
        ls_pins[label] = (px, ls_bot_y)
        pads_data.append((px, ls_bot_y, 1.7, 1.0, f"U3_{label}", label))

    # --- COMPONENT 5: ESP32 DEVKIT (U4) ---
    # 2 rows of 15 pins (pitch 2.54mm, row spacing 25.4mm / 1.0 inch)
    esp_start_x = 46.0
    esp_top_y = 33.0   # Upper row
    esp_bot_y = 54.0   # Lower row
    # Notice: Let's shift ESP32 to bottom right:
    # Top row: D23... TX2(17), RX2(16), GPIO4, GND, 3V3
    # We place ESP32 so its pins match cleanly:
    esp_pins = {}
    esp_top_labels = ["D23", "D22", "TX0", "RX0", "D21", "D19", "D18", "D5", "TX2", "RX2", "D4", "D2", "D15", "GND_E1", "3V3"]
    for i, label in enumerate(esp_top_labels):
        px = esp_start_x + i * 2.54
        drl.add_hit(px, esp_top_y + 12.0, 1.0)
        gtl.flash(px, esp_top_y + 12.0, pad_header)
        gbl.flash(px, esp_top_y + 12.0, pad_header)
        gts.flash(px, esp_top_y + 12.0, mask_header)
        gbs.flash(px, esp_top_y + 12.0, mask_header)
        esp_pins[label] = (px, esp_top_y + 12.0)
        pads_data.append((px, esp_top_y + 12.0, 1.7, 1.0, f"U4_{label}", label))

    esp_bot_labels = ["EN", "VP", "VN", "D34", "D35", "D32", "D33", "D25", "D26", "D27", "D14", "D12", "D13", "GND_E2", "VIN"]
    for i, label in enumerate(esp_bot_labels):
        px = esp_start_x + i * 2.54
        drl.add_hit(px, esp_bot_y, 1.0)
        gtl.flash(px, esp_bot_y, pad_header)
        gbl.flash(px, esp_bot_y, pad_header)
        gts.flash(px, esp_bot_y, mask_header)
        gbs.flash(px, esp_bot_y, mask_header)
        esp_pins[label] = (px, esp_bot_y)
        pads_data.append((px, esp_bot_y, 1.7, 1.0, f"U4_{label}", label))

    # --- ROUTING (TRACES) ---
    def route_bottom(p1, p2, width_ap, width_mm, net):
        # Orthogonal route on bottom layer (GBL)
        mid_x = p1[0]
        mid_y = p2[1]
        gbl.line(p1[0], p1[1], mid_x, mid_y, width_ap)
        gbl.line(mid_x, mid_y, p2[0], p2[1], width_ap)
        traces_bottom.append((p1[0], p1[1], mid_x, mid_y, width_mm, net))
        traces_bottom.append((mid_x, mid_y, p2[0], p2[1], width_mm, net))

    def route_top(p1, p2, width_ap, width_mm, net):
        # Orthogonal route on top layer (GTL)
        mid_x = p2[0]
        mid_y = p1[1]
        gtl.line(p1[0], p1[1], mid_x, mid_y, width_ap)
        gtl.line(mid_x, mid_y, p2[0], p2[1], width_ap)
        traces_top.append((p1[0], p1[1], mid_x, mid_y, width_mm, net))
        traces_top.append((mid_x, mid_y, p2[0], p2[1], width_mm, net))

    # 1. Isolated Side (J1 -> ISO1540 Side 1)
    route_bottom(j1_pins[1], u1_side1["SCL1"], ap_trace_sig_b, 0.4, "LWZ_SCL")
    route_bottom(j1_pins[2], u1_side1["GND1"], ap_trace_sig_b, 0.4, "LWZ_GND")
    route_bottom(j1_pins[3], u1_side1["VCC1"], ap_trace_sig_b, 0.4, "LWZ_5V")
    route_bottom(j1_pins[4], u1_side1["SDA1"], ap_trace_sig_b, 0.4, "LWZ_SDA")

    # 2. Local I2C Bus (ISO1540 Side 2 -> Arduino D2/D3)
    route_bottom(u1_side2["SDA2"], am_pins["D2"], ap_trace_sig_b, 0.4, "I2C_SDA")
    route_top(u1_side2["SCL2"], am_pins["D3"], ap_trace_sig_t, 0.4, "I2C_SCL")

    # 3. 5V Power Distribution (ESP32 VIN -> Arduino 5V -> ISO1540 VCC2 -> Level Shifter HV)
    route_bottom(esp_pins["VIN"], am_pins["5V"], ap_trace_pwr, 0.8, "5V_MAIN")
    route_bottom(am_pins["5V"], ls_pins["HV"], ap_trace_pwr, 0.8, "5V_MAIN")
    route_top(ls_pins["HV"], u1_side2["VCC2"], ap_trace_pwr, 0.8, "5V_MAIN")

    # 4. 3.3V Power Distribution (ESP32 3V3 -> Level Shifter LV)
    route_bottom(esp_pins["3V3"], ls_pins["LV"], ap_trace_pwr, 0.8, "3V3_MAIN")

    # 5. Local GND Distribution
    route_bottom(esp_pins["GND_E1"], ls_pins["GND_L"], ap_trace_pwr, 0.8, "GND")
    route_bottom(ls_pins["GND_H"], am_pins["GND"], ap_trace_pwr, 0.8, "GND")
    route_top(am_pins["GND"], u1_side2["GND2"], ap_trace_pwr, 0.8, "GND")

    # 6. UART Signals (via Level Shifter)
    # Arduino TX1 -> Level Shifter HV1
    route_top(am_pins["TX1"], ls_pins["HV1"], ap_trace_sig_t, 0.4, "UART_TX1")
    # Level Shifter LV1 -> ESP32 RX2
    route_bottom(ls_pins["LV1"], esp_pins["RX2"], ap_trace_sig_b, 0.4, "UART_RX2")
    # ESP32 TX2 -> Level Shifter LV2
    route_bottom(esp_pins["TX2"], ls_pins["LV2"], ap_trace_sig_b, 0.4, "UART_TX2")
    # Level Shifter HV2 -> Arduino RX0
    route_top(ls_pins["HV2"], am_pins["RX0"], ap_trace_sig_t, 0.4, "UART_RX0")

    # 7. Hardware Reset Signal (ESP32 D4 -> Level Shifter LV3 -> HV3 -> Arduino RST)
    route_bottom(esp_pins["D4"], ls_pins["LV3"], ap_trace_sig_b, 0.4, "RST_LV")
    route_top(ls_pins["HV3"], am_pins["RST"], ap_trace_sig_t, 0.4, "RST_HV")

    # Save all Gerber & Drill files
    files = [
        gtl.save(),
        gbl.save(),
        gts.save(),
        gbs.save(),
        gto.save(),
        gbo.save(),
        gml.save(),
        drl.save(),
    ]

    # --- CREATE ZIP PACKAGE FOR ORDERING (JLCPCB / AISLER / PCBWAY) ---
    zip_path = os.path.join(OUTPUT_DIR, "lwz180_bridge_gerbers.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zipf:
        for f in files:
            zipf.write(f, os.path.basename(f))
    print(f"Generated Gerber ZIP Package: {zip_path}")

    # --- GENERATE BOM CSV ---
    bom_path = os.path.join(OUTPUT_DIR, "BOM.csv")
    with open(bom_path, "w") as f:
        f.write("Designator,Qty,Component,Footprint,Description,Manufacturer / Link\n")
        f.write("J1,1,4-Pin Screw Terminal,Pitch 5.08mm Printklemme,Stiebel Eltron LWZ 180 Bus Connector,KF301-4P / Phoenix Contact\n")
        f.write("U1,1,ISO1540 Breakout,2x4 Pin Header Female 2.54mm,Galvanischer I2C Isolator,Adafruit 4754 / SparkFun Qwiic\n")
        f.write("U2,1,Arduino Micro,2x17 Pin Header Female 2.54mm,ATmega32U4 Stiebel Bus Master/Decoder,Original Arduino Micro / Pro Micro compatible\n")
        f.write("U3,1,4-Ch Logic Level Shifter,2x6 Pin Header Female 2.54mm,BSS138 3.3V <-> 5V Bi-directional,Standard 4-Ch I2C/UART Shifter\n")
        f.write("U4,1,ESP32 DevKit V1,2x15 Pin Header Female 2.54mm,ESP-WROOM-32 WiFi MQTT Web OTA,NodeMCU-32S / ESP32 DevKit\n")
        f.write("H1-H4,4,M3 Mounting Hole,3.2mm Drill 6.0mm Pad,Corner Mounting Holes for Enclosure,-\n")
    print(f"Generated BOM: {bom_path}")

    # --- GENERATE HIGH RESOLUTION SVG RENDERS ---
    generate_svg_render(pads_data, traces_top, traces_bottom, "pcb_top_render.svg", is_top=True)
    generate_svg_render(pads_data, traces_top, traces_bottom, "pcb_bottom_render.svg", is_top=False)


def generate_svg_render(pads, traces_top, traces_bottom, filename, is_top=True):
    scale = 12.0  # 12 px per mm -> 90mm * 12 = 1080px width
    svg_w = int(BOARD_W * scale)
    svg_h = int(BOARD_H * scale)

    layer_title = "TOP LAYER (Bestückungsseite)" if is_top else "BOTTOM LAYER (Lötseite)"

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {svg_w + 100} {svg_h + 120}" width="100%" height="100%" style="background-color: #0d1117; font-family: -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif;">',
        '  <defs>',
        '    <filter id="pcb-shadow" x="-5%" y="-5%" width="110%" height="110%">',
        '      <feDropShadow dx="3" dy="5" stdDeviation="6" flood-color="#000" flood-opacity="0.7"/>',
        '    </filter>',
        '  </defs>',
        f'  <text x="50" y="40" font-size="20" font-weight="700" fill="#ffffff">Stiebel Eltron LWZ 180 Custom PCB — {layer_title}</text>',
        f'  <text x="50" y="60" font-size="12" fill="#8b949e">Abmessungen: 90.0 × 60.0 mm · 2-Layer FR4 · 1.6mm · Industriestandard JLCPCB / AISLER ready</text>',
        f'  <g transform="translate(50, 75)" filter="url(#pcb-shadow)">',
        f'    <!-- PCB Board FR4 Base -->',
        f'    <rect x="0" y="0" width="{svg_w}" height="{svg_h}" rx="{CORNER_R * scale}" fill="{COLOR_PCB_GREEN}" stroke="#2e7d32" stroke-width="2"/>',
    ]

    # Isolation Cutout / Slot indicator
    iso_x = 27.62 * scale
    svg.append(f'    <line x1="{iso_x}" y1="0" x2="{iso_x}" y2="{svg_h}" stroke="#e53935" stroke-width="3" stroke-dasharray="10,6"/>')
    svg.append(f'    <text x="{iso_x - 10}" y="{svg_h - 20}" font-size="12" font-weight="700" fill="#ef5350" text-anchor="end" transform="rotate(-90 {iso_x - 10} {svg_h - 20})">GALVANISCHE TRENNUNG (&gt;6mm)</text>')

    # Render Traces
    traces = traces_top if is_top else traces_bottom
    trace_color = "#e6c35c" if is_top else "#4fc3f7"
    for x1, y1, x2, y2, w, net in traces:
        px1, py1 = x1 * scale, y1 * scale
        px2, py2 = x2 * scale, y2 * scale
        pw = max(2.5, w * scale)
        svg.append(f'    <line x1="{px1}" y1="{py1}" x2="{px2}" y2="{py2}" stroke="{trace_color}" stroke-width="{pw}" stroke-linecap="round" opacity="0.85"/>')

    # Render Pads
    for x, y, pad_dia, hole_dia, net, label in pads:
        px, py = x * scale, y * scale
        pr = (pad_dia / 2.0) * scale
        hr = (hole_dia / 2.0) * scale
        # Copper Ring
        svg.append(f'    <circle cx="{px}" cy="{py}" r="{pr}" fill="{COLOR_COPPER_GOLD}" stroke="#b89428" stroke-width="1"/>')
        # Drill Hole
        svg.append(f'    <circle cx="{px}" cy="{py}" r="{hr}" fill="{COLOR_HOLE}"/>')
        # Silkscreen text for key pads
        if is_top and pad_dia < 4.0:
            svg.append(f'    <text x="{px}" y="{py - pr - 3}" font-size="8" font-weight="600" fill="{COLOR_SILK_WHITE}" text-anchor="middle">{label}</text>')

    # Silkscreen Box Outlines for Modules
    if is_top:
        svg.append('    <!-- Module Outlines & Labels -->')
        # Screw Terminal Outline
        svg.append(f'    <rect x="{4.0 * scale}" y="{18.0 * scale}" width="{8.0 * scale}" height="{22.0 * scale}" fill="none" stroke="{COLOR_SILK_WHITE}" stroke-width="1.5"/>')
        svg.append(f'    <text x="{8.0 * scale}" y="{15.0 * scale}" font-size="11" font-weight="700" fill="{COLOR_SILK_WHITE}" text-anchor="middle">J1: LWZ 180</text>')

        # ISO1540 Outline
        svg.append(f'    <rect x="{17.0 * scale}" y="{21.0 * scale}" width="{21.5 * scale}" height="{14.0 * scale}" fill="none" stroke="{COLOR_SILK_WHITE}" stroke-width="1.5"/>')
        svg.append(f'    <text x="{27.7 * scale}" y="{19.0 * scale}" font-size="11" font-weight="700" fill="{COLOR_SILK_WHITE}" text-anchor="middle">U1: ISO1540</text>')

        # Arduino Micro Outline
        svg.append(f'    <rect x="{43.0 * scale}" y="{8.0 * scale}" width="{44.0 * scale}" height="{23.0 * scale}" rx="4" fill="none" stroke="{COLOR_SILK_WHITE}" stroke-width="1.5"/>')
        svg.append(f'    <text x="{65.0 * scale}" y="{6.0 * scale}" font-size="12" font-weight="700" fill="{COLOR_SILK_WHITE}" text-anchor="middle">U2: ARDUINO MICRO</text>')

        # Level Shifter Outline
        svg.append(f'    <rect x="{48.0 * scale}" y="{32.0 * scale}" width="{19.0 * scale}" height="{19.0 * scale}" rx="3" fill="none" stroke="{COLOR_SILK_WHITE}" stroke-width="1.5"/>')
        svg.append(f'    <text x="{57.5 * scale}" y="{30.5 * scale}" font-size="10" font-weight="700" fill="{COLOR_SILK_WHITE}" text-anchor="middle">U3: LEVEL SHIFTER</text>')

        # ESP32 Outline
        svg.append(f'    <rect x="{43.0 * scale}" y="{42.0 * scale}" width="{44.0 * scale}" height="{15.0 * scale}" rx="4" fill="none" stroke="{COLOR_SILK_WHITE}" stroke-width="1.5"/>')
        svg.append(f'    <text x="{65.0 * scale}" y="{40.0 * scale}" font-size="12" font-weight="700" fill="{COLOR_SILK_WHITE}" text-anchor="middle">U4: ESP32 DEVKIT</text>')

    svg.append('  </g>')
    svg.append('</svg>')

    filepath = os.path.join(OUTPUT_DIR, filename)
    with open(filepath, "w") as f:
        f.write("\n".join(svg) + "\n")
    print(f"Generated SVG Render: {filepath}")


if __name__ == "__main__":
    generate_pcb()
