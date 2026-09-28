/*
  LWZ180 I2C bridge firmware (Arduino/Genuino Micro compatible)
  - Receives LWZ180 packets on I2C general call as slave
  - Parses measurements + display status
  - Executes target control state machine via button packet broadcasts
  - Exposes telemetry and command interface over Serial (UART)

  Serial protocol (line-based, newline terminated):
    TX -> ESP32:
      READY
      MEAS,outside_temp,21.3
      MEAS,exhaust_humidity,45.1
      MEAS,inlet_flow,138
      MEAS,inlet_power,42.0
      STAT,power_vent=0,scheduled=1,level=2
      ACK,id=123,ok=1,msg=accepted
      ACK,id=123,ok=0,msg=timeout

    RX <- ESP32:
      CMD,id=123,level=0..3
      CMD,id=124,power_vent=0|1
      CMD,id=125,scheduled=0|1
*/

#include <Wire.h>

#define I2C_SLAVE_ADDR 42

#define MEASUREMENT_INTERVAL 60000
#define BUTTON_DELAY 700
#define WHEEL_DELAY 50

#define CHANGED_POWER_VENT 1
#define CHANGED_SCHEDULED 2
#define CHANGED_LEVEL 4

#define CRC_POLY 0x49

#define BUTTON_DOWN 0x01
#define BUTTON_UP 0x02
#define BUTTON_POWER_VENT 0x0c

#define CMD_TIMEOUT_MS 20000
#define CMD_MAX_ATTEMPTS 30

byte ledVal = 0;
uint32_t lastMeasurementFlush = 0;
uint32_t lastButton = 0;

byte button_packet[] = {0xe3, 0x30, 0x20, 0x00, 0x00, 0x00, 0x00};

struct Measurement {
  byte reg;
  const char* key;
  byte scale;
};

const byte MAX_REG = 0x5a + 1;
const byte N_MEAS = 14;

uint16_t changed = 0;
Measurement measurements[N_MEAS] = {
  {0x00, "dew_point_extract", 10},
  {0x01, "dew_point_outdoor", 10},
  {0x06, "outside_temp", 10},
  {0x07, "supply_temp", 10},
  {0x08, "extract_temp", 10},
  {0x09, "exhaust_temp", 10},
  {0x0e, "outdoor_humidity", 10},
  {0x0f, "exhaust_humidity", 10},
  {0x10, "inlet_flow", 1},
  {0x11, "exhaust_flow", 1},
  {0x14, "inlet_power", 10},
  {0x15, "exhaust_power", 10},
  {0x16, "preheater_power", 1},
  {0x5a, "filter_life", 1}
};
byte registers[MAX_REG];
int16_t values[N_MEAS] = {};

struct Status {
  int8_t power_vent;
  int8_t scheduled;
  int8_t level;
};

byte stat_changed = 0;
Status stat = {-1, -1, -1};

const byte DIGIT_1 = 0x06;
const byte DIGIT_2 = 0x5b;
const byte DIGIT_3 = 0x4f;

int8_t target_level = -1;
bool set_scheduled = false;

uint32_t totalPackets = 0;
bool snifferActive = false;
uint32_t snifferUntil = 0;
volatile bool newPacketAvailable = false;
volatile byte lastPacketLen = 0;
volatile byte lastPacketData[28];

int freeMemory() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

struct PendingCommand {
  bool active;
  long id;
  uint32_t start;
  uint8_t attempts;
};

PendingCommand pending = {false, 0, 0, 0};

String serialLine;

byte crc8_push_byte(byte crc, byte data) {
  crc ^= data;
  for (int i = 0; i < 8; i++) {
    if ((crc & 0x80) != 0) {
      crc = (byte)((crc << 1) ^ CRC_POLY);
    } else {
      crc <<= 1;
    }
  }
  return crc;
}

void sendCmd(byte* cmd) {
  Wire.beginTransmission(0);
  Wire.write(cmd, 7);
  Wire.endTransmission();
}

void sendButton(byte code, byte count) {
  digitalWrite(LED_BUILTIN, ledVal ^= 1);
  button_packet[5] = code;
  button_packet[6] = crc8_push_byte(0, code);
  for (byte i = 0; i < count; i++) {
    if (i != 0) {
      delay(WHEEL_DELAY);
    }
    sendCmd(button_packet);
  }
  digitalWrite(LED_BUILTIN, ledVal ^= 1);
}

void reportStat() {
  if (stat.power_vent >= 0 && stat.scheduled >= 0 && stat.level >= 0) {
    Serial1.print("STAT,power_vent=");
    Serial1.print(stat.power_vent);
    Serial1.print(",scheduled=");
    Serial1.print(stat.scheduled);
    Serial1.print(",level=");
    Serial1.println(stat.level);
  }
}

void ackPending(bool ok, const char* msg) {
  if (!pending.active) {
    return;
  }
  Serial1.print("ACK,id=");
  Serial1.print(pending.id);
  Serial1.print(",ok=");
  Serial1.print(ok ? 1 : 0);
  Serial1.print(",msg=");
  Serial1.println(msg);
  pending.active = false;
}

void beginPending(long id) {
  pending.active = true;
  pending.id = id;
  pending.start = millis();
  pending.attempts = 0;
}

void handleCommandLine(const String& line) {
  if (!line.startsWith("CMD,")) {
    return;
  }

  int idStart = line.indexOf("id=");
  int idEnd = line.indexOf(',', idStart);
  if (idStart < 0 || idEnd < 0) {
    return;
  }

  long id = line.substring(idStart + 3, idEnd).toInt();

  if (line.indexOf("level=") > -1) {
    int val = line.substring(line.indexOf("level=") + 6).toInt();
    if (val >= 0 && val <= 3) {
      // If we are already at the target level and manual mode is active, apply instantly
      if (stat.scheduled == 0 && stat.level == val) {
        Serial1.print("ACK,id=");
        Serial1.print(id);
        Serial1.println(",ok=1,msg=applied");
        return;
      }
      target_level = val;
      set_scheduled = false;
      beginPending(id);
      Serial1.print("ACK,id=");
      Serial1.print(id);
      Serial1.println(",ok=1,msg=accepted");
      return;
    }
  } else if (line.indexOf("power_vent=") > -1) {
    int val = line.substring(line.indexOf("power_vent=") + 11).toInt();
    if ((val == 0 || val == 1) && stat.power_vent >= 0) {
      if (val != stat.power_vent) {
        sendButton(BUTTON_POWER_VENT, 1);
      }
      // power_vent is fire-and-forget (toggle), no convergence loop needed
      Serial1.print("ACK,id=");
      Serial1.print(id);
      Serial1.println(",ok=1,msg=applied");
      return;
    }
  } else if (line.indexOf("scheduled=") > -1) {
    int val = line.substring(line.indexOf("scheduled=") + 10).toInt();
    if ((val == 0 || val == 1) && stat.scheduled >= 0) {
      // If we are already in the target scheduled state, apply instantly
      if ((bool)val == (bool)stat.scheduled) {
        Serial1.print("ACK,id=");
        Serial1.print(id);
        Serial1.println(",ok=1,msg=applied");
        return;
      }
      if (val == 1) {
        set_scheduled = true;
      } else {
        set_scheduled = false;
        target_level = stat.level;
      }
      beginPending(id);
      Serial1.print("ACK,id=");
      Serial1.print(id);
      Serial1.println(",ok=1,msg=accepted");
      return;
    }
  } else if (line.indexOf("query=") > -1) {
    reportStat();
    Serial1.print("ACK,id=");
    Serial1.print(id);
    Serial1.println(",ok=1,msg=status_reported");
    return;
  } else if (line.indexOf("diag=") > -1) {
    Serial1.print("DIAG,uptime=");
    Serial1.print(millis() / 1000);
    Serial1.print(",packets=");
    Serial1.print(totalPackets);
    Serial1.print(",level=");
    Serial1.print(stat.level);
    Serial1.print(",sched=");
    Serial1.print(stat.scheduled);
    Serial1.print(",pv=");
    Serial1.print(stat.power_vent);
    Serial1.print(",target=");
    Serial1.print(target_level);
    Serial1.print(",attempts=");
    Serial1.print(pending.attempts);
    Serial1.print(",free_ram=");
    Serial1.println(freeMemory());
    Serial1.print("ACK,id=");
    Serial1.print(id);
    Serial1.println(",ok=1,msg=diag_reported");
    return;
  } else if (line.indexOf("sniff=") > -1) {
    int val = line.substring(line.indexOf("sniff=") + 6).toInt();
    snifferActive = (val != 0);
    if (snifferActive) {
      snifferUntil = millis() + 60000UL; // Sniff for 60 seconds
    }
    Serial1.print("ACK,id=");
    Serial1.print(id);
    Serial1.print(",ok=1,msg=sniffer_");
    Serial1.println(snifferActive ? "on" : "off");
    return;
  } else if (line.indexOf("btn=") > -1) {
    String btn = line.substring(line.indexOf("btn=") + 4);
    btn.trim();
    if (btn == "up") {
      sendButton(BUTTON_UP, 2);
    } else if (btn == "down") {
      sendButton(BUTTON_DOWN, 2);
    } else if (btn == "pv") {
      sendButton(BUTTON_POWER_VENT, 1);
    } else if (btn == "wake") {
      sendButton(BUTTON_UP, 8);
    }
    Serial1.print("ACK,id=");
    Serial1.print(id);
    Serial1.println(",ok=1,msg=button_sent");
    return;
  } else if (line.indexOf("reboot=1") > -1 || line.indexOf("reset=1") > -1) {
    Serial1.print("ACK,id=");
    Serial1.print(id);
    Serial1.println(",ok=1,msg=rebooting");
    Serial1.flush();
    delay(50);
    asm volatile ("jmp 0");
    return;
  }

  Serial1.print("ACK,id=");
  Serial1.print(id);
  Serial1.println(",ok=0,msg=invalid");
}

void processSerialInput() {
  while (Serial1.available() > 0) {
    char ch = (char)Serial1.read();
    if (ch == '\n') {
      handleCommandLine(serialLine);
      serialLine = "";
    } else if (ch != '\r') {
      serialLine += ch;
      if (serialLine.length() > 160) {
        serialLine = "";
      }
    }
  }
}

void receiveEvent(int howMany) {
  byte data[28];
  if (howMany > 28) {
    while (Wire.available()) {
      Wire.read();
    }
    return;
  }

  Wire.readBytes(data, howMany);
  totalPackets++;
  if (snifferActive && !newPacketAvailable && howMany <= 28) {
    lastPacketLen = howMany;
    memcpy((void*)lastPacketData, data, howMany);
    newPacketAvailable = true;
  }

  if (howMany == 7 && data[0] == 0xe3 && data[1] == 0x20) {
    byte reg = data[3];
    // Direct fan stage actual-value registers broadcast by HVAC motherboard
    if (reg == 0x02 || reg == 0x05 || reg == 0x69 || reg == 0x6d) {
      int16_t fanStage = (int16_t)((data[4] << 8) + data[5]);
      if (fanStage >= 0 && fanStage <= 3) {
        if (fanStage != stat.level) {
          stat.level = (int8_t)fanStage;
          stat_changed |= CHANGED_LEVEL;
        }
      }
    }
    if (reg < MAX_REG) {
      byte idx = registers[reg];
      if (idx) {
        idx--;
        values[idx] = (int16_t)((data[4] << 8) + data[5]);
        changed |= (1 << idx);
      }
    }
  }

  if (howMany == 28 && data[0] == 0xf7 && data[1] == 0x20) {
    int8_t power_vent = (bool)(data[26] & 0x01);
    int8_t scheduled = (bool)(data[15] & 0x01);
    if (power_vent != stat.power_vent) {
      stat.power_vent = power_vent;
      stat_changed |= CHANGED_POWER_VENT;
    }
    if (scheduled != stat.scheduled) {
      stat.scheduled = scheduled;
      stat_changed |= CHANGED_SCHEDULED;
    }

    int8_t level = stat.level;
    if ((data[20] & 0x04) || (bool)(data[19] & 0x10)) {
      // Home screen active with fan digit
      byte digit = data[8];
      if (digit == DIGIT_1) {
        level = 1;
      } else if (digit == DIGIT_2) {
        level = 2;
      } else if (digit == DIGIT_3) {
        level = 3;
      }
      // If digit is unrecognised or transition frame, preserve stat.level
    } else if ((data[12] & 0x04) && !(data[16] & 0x10)) {
      // Confirmed Standby state: Standby power icon is ON (data[12] & 0x04)
      // and Fan icon is OFF (!(data[16] & 0x10))
      level = 0;
    }
    // Intermediate transition/menu/blank frames preserve current stat.level
    // to avoid false Level 0 overwrites during adjustments

    if (level != stat.level && level >= 0) {
      stat.level = level;
      stat_changed |= CHANGED_LEVEL;
    }
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);   // USB debug
  Serial1.begin(115200);  // UART to ESP32 (pins 0/1)
  Serial1.setTimeout(5);

  for (byte i = 0; i < MAX_REG; i++) {
    registers[i] = 0x00;
  }
  for (byte i = 0; i < N_MEAS; i++) {
    byte idx = measurements[i].reg;
    registers[idx] = i + 1;
  }

  Wire.begin(I2C_SLAVE_ADDR);
  TWAR = (I2C_SLAVE_ADDR << 1) | 1;
  Wire.onReceive(receiveEvent);

  Serial.println("READY (USB debug)");
  Serial1.println("READY");
}

void flushMeasurements() {
  long mask = changed;
  changed = 0;
  for (byte i = 0; i < N_MEAS; i++) {
    if (mask & 1) {
      float scaled = (float)values[i] / measurements[i].scale;
      Serial1.print("MEAS,");
      Serial1.print(measurements[i].key);
      Serial1.print(",");
      if (measurements[i].scale == 1) {
        Serial1.println((int)scaled);
      } else {
        Serial1.println(scaled, 1);
      }
    }
    mask >>= 1;
  }
}

bool controlConverged() {
  if (set_scheduled) {
    return (stat.scheduled == 1);
  }
  if (target_level > -1) {
    return (stat.scheduled == 0 && stat.level == target_level);
  }
  return true;
}

void loop() {
  processSerialInput();
  uint32_t now = millis();

  if (now - lastMeasurementFlush >= MEASUREMENT_INTERVAL) {
    lastMeasurementFlush = now;
    flushMeasurements();
    reportStat(); // Send current status periodically as a heartbeat
  }

  if (stat_changed) {
    stat_changed = 0;
    reportStat();
  }

  if (snifferActive && now > snifferUntil) {
    snifferActive = false;
    Serial1.println("SNIFF,expired");
  }

  if (newPacketAvailable) {
    newPacketAvailable = false;
    Serial1.print("SNIFF,len=");
    Serial1.print(lastPacketLen);
    if (lastPacketLen == 28 && lastPacketData[0] == 0xf7) {
      Serial1.print(",type=display,digit=0x");
      Serial1.print(lastPacketData[8], HEX);
      Serial1.print(",sched=");
      Serial1.print((bool)(lastPacketData[15] & 0x01));
      Serial1.print(",pv=");
      Serial1.print((bool)(lastPacketData[26] & 0x01));
      Serial1.print(",fan_icon=");
      Serial1.print((bool)(lastPacketData[16] & 0x10));
      Serial1.print(",pwr_icon=");
      Serial1.print((bool)(lastPacketData[12] & 0x04));
    } else if (lastPacketLen == 7 && lastPacketData[0] == 0xe3) {
      Serial1.print(",type=meas,reg=0x");
      Serial1.print(lastPacketData[3], HEX);
      int16_t v = (int16_t)((lastPacketData[4] << 8) + lastPacketData[5]);
      Serial1.print(",val=");
      Serial1.print(v);
    }
    Serial1.print(",raw=");
    for (byte i = 0; i < lastPacketLen; i++) {
      if (lastPacketData[i] < 0x10) Serial1.print("0");
      Serial1.print(lastPacketData[i], HEX);
    }
    Serial1.println();
  }

  if (pending.active) {
    if (controlConverged()) {
      target_level = -1;
      set_scheduled = false;
      ackPending(true, "applied");
    } else if ((now >= pending.start && (now - pending.start) > CMD_TIMEOUT_MS) || pending.attempts >= CMD_MAX_ATTEMPTS) {
      target_level = -1;
      set_scheduled = false;
      ackPending(false, "timeout");
    }
  }

  if (now - lastButton >= BUTTON_DELAY) {
    if ((target_level > -1) || set_scheduled) {
      byte code = BUTTON_UP;
      if (set_scheduled) {
        code = BUTTON_UP;
      } else if ((stat.scheduled == 1) || (target_level < stat.level)) {
        code = BUTTON_DOWN;
      } else if (target_level > stat.level) {
        code = BUTTON_UP;
      }
      byte count = 2;
      if (stat.level == 0 && target_level > 0) {
        count = 8; // Wake-up pulse burst for capacitive touch controller
      }
      sendButton(code, count);
      if (pending.active) {
        pending.attempts++;
      }
    }
    lastButton = now;
  }
}
