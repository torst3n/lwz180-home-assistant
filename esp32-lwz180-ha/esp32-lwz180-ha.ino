/*
  ESP32 MQTT + Home Assistant bridge frontend for LWZ180
  - Communicates with lwz180-bridge.ino over UART
  - Publishes telemetry/state to MQTT
  - Accepts MQTT commands and forwards to bridge
  - Publishes Home Assistant MQTT discovery entities
*/

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ------------------ WIFI / MQTT CONFIG ----------------
#include "secrets.h"

// ------------------ UART BRIDGE CONFIG ----------------
#define BRIDGE_SERIAL_BAUD 115200
#define BRIDGE_UART_RX_PIN 16
#define BRIDGE_UART_TX_PIN 17
#define BRIDGE_UART_PORT 2

// ------------------ MQTT TOPICS -----------------------
const char* TOPIC_AVAILABILITY = "ventilation/bridge/status";
const char* TOPIC_ACK = "ventilation/command/ack";

const char* TOPIC_TEMP_OUTSIDE = "ventilation/temperature/outside";
const char* TOPIC_HUMIDITY_EXHAUST = "ventilation/humidity/exhaust";
const char* TOPIC_FLOW_INLET = "ventilation/flow/inlet";
const char* TOPIC_POWER_INLET = "ventilation/power/inlet";

const char* TOPIC_DEWPOINT_EXTRACT = "ventilation/dewpoint/extract";
const char* TOPIC_DEWPOINT_OUTDOOR = "ventilation/dewpoint/outdoor";
const char* TOPIC_TEMP_SUPPLY = "ventilation/temperature/supply";
const char* TOPIC_TEMP_EXTRACT = "ventilation/temperature/extract";
const char* TOPIC_TEMP_EXHAUST = "ventilation/temperature/exhaust";
const char* TOPIC_HUMIDITY_OUTDOOR = "ventilation/humidity/outdoor";
const char* TOPIC_FLOW_EXHAUST = "ventilation/flow/exhaust";
const char* TOPIC_POWER_EXHAUST = "ventilation/power/exhaust";
const char* TOPIC_POWER_PREHEATER = "ventilation/power/preheater";
const char* TOPIC_FILTER_LIFE = "ventilation/filter/life";

const char* TOPIC_LEVEL_SET = "ventilation/level/set";
const char* TOPIC_LEVEL_STATE = "ventilation/level/state";
const char* TOPIC_POWER_SET = "ventilation/power/set";
const char* TOPIC_POWER_STATE = "ventilation/power/state";
const char* TOPIC_SCHEDULED_SET = "ventilation/scheduled/set";
const char* TOPIC_SCHEDULED_STATE = "ventilation/scheduled/state";

const char* DISCOVERY_PREFIX = "homeassistant";
const char* DEVICE_ID = "lwz180_bridge";
const char* DEVICE_NAME = "LWZ180 Ventilation";
const char* DEVICE_MANUFACTURER = "Stiebel Eltron";
const char* DEVICE_MODEL = "LWZ180";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
HardwareSerial BridgeSerial(BRIDGE_UART_PORT);

String serialLine;
uint32_t lastAvailabilityPublish = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastWifiAttempt = 0;
long nextCommandId = 1;

struct State {
  int8_t level = -1;
  int8_t powerVent = -1;
  int8_t scheduled = -1;
} state;

void publishRetained(const char* topic, const char* payload) {
  mqttClient.publish(topic, payload, true);
}

void publishAvailability(bool online) {
  publishRetained(TOPIC_AVAILABILITY, online ? "online" : "offline");
}

void publishDiscovery(const char* component, const char* objectId, const char* payload) {
  char topic[220];
  snprintf(topic, sizeof(topic), "%s/%s/%s/%s/config", DISCOVERY_PREFIX, component, DEVICE_ID, objectId);
  publishRetained(topic, payload);
}

void publishDiscoveryConfig() {
  char payload[1024];

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Outside Temperature\",\"uniq_id\":\"%s_outside_temp\",\"stat_t\":\"%s\",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_TEMP_OUTSIDE, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "outside_temperature", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Exhaust Humidity\",\"uniq_id\":\"%s_exhaust_humidity\",\"stat_t\":\"%s\",\"unit_of_meas\":\"%%\",\"dev_cla\":\"humidity\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_HUMIDITY_EXHAUST, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "exhaust_humidity", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Inlet Flow\",\"uniq_id\":\"%s_inlet_flow\",\"stat_t\":\"%s\",\"unit_of_meas\":\"m³/h\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_FLOW_INLET, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "inlet_flow", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Inlet Power\",\"uniq_id\":\"%s_inlet_power\",\"stat_t\":\"%s\",\"unit_of_meas\":\"W\",\"dev_cla\":\"power\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_POWER_INLET, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "inlet_power", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Dew Point Extract\",\"uniq_id\":\"%s_dew_point_extract\",\"stat_t\":\"%s\",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_DEWPOINT_EXTRACT, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "dew_point_extract", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Dew Point Outdoor\",\"uniq_id\":\"%s_dew_point_outdoor\",\"stat_t\":\"%s\",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_DEWPOINT_OUTDOOR, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "dew_point_outdoor", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Supply Air Temperature\",\"uniq_id\":\"%s_supply_temp\",\"stat_t\":\"%s\",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_TEMP_SUPPLY, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "supply_temperature", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Extract Air Temperature\",\"uniq_id\":\"%s_extract_temp\",\"stat_t\":\"%s\",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_TEMP_EXTRACT, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "extract_temperature", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Exhaust Air Temperature\",\"uniq_id\":\"%s_exhaust_temp\",\"stat_t\":\"%s\",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_TEMP_EXHAUST, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "exhaust_temperature", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Outdoor Humidity\",\"uniq_id\":\"%s_outdoor_humidity\",\"stat_t\":\"%s\",\"unit_of_meas\":\"%%\",\"dev_cla\":\"humidity\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_HUMIDITY_OUTDOOR, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "outdoor_humidity", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Exhaust Flow\",\"uniq_id\":\"%s_exhaust_flow\",\"stat_t\":\"%s\",\"unit_of_meas\":\"m³/h\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_FLOW_EXHAUST, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "exhaust_flow", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Exhaust Power\",\"uniq_id\":\"%s_exhaust_power\",\"stat_t\":\"%s\",\"unit_of_meas\":\"W\",\"dev_cla\":\"power\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_POWER_EXHAUST, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "exhaust_power", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Pre-heater Power\",\"uniq_id\":\"%s_preheater_power\",\"stat_t\":\"%s\",\"unit_of_meas\":\"W\",\"dev_cla\":\"power\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_POWER_PREHEATER, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "preheater_power", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Filter Life Remaining\",\"uniq_id\":\"%s_filter_life\",\"stat_t\":\"%s\",\"unit_of_meas\":\"h\",\"stat_cla\":\"measurement\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_FILTER_LIFE, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("sensor", "filter_life", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Ventilation Level\",\"uniq_id\":\"%s_level\",\"cmd_t\":\"%s\",\"stat_t\":\"%s\",\"min\":0,\"max\":3,\"step\":1,\"mode\":\"box\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_LEVEL_SET, TOPIC_LEVEL_STATE, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("number", "ventilation_level", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Power Vent\",\"uniq_id\":\"%s_power_vent\",\"cmd_t\":\"%s\",\"stat_t\":\"%s\",\"pl_on\":\"1\",\"pl_off\":\"0\",\"stat_on\":\"1\",\"stat_off\":\"0\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_POWER_SET, TOPIC_POWER_STATE, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("switch", "power_vent", payload);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"LWZ180 Scheduled Mode\",\"uniq_id\":\"%s_scheduled\",\"cmd_t\":\"%s\",\"stat_t\":\"%s\",\"pl_on\":\"1\",\"pl_off\":\"0\",\"stat_on\":\"1\",\"stat_off\":\"0\",\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"%s\",\"mdl\":\"%s\"}}",
           DEVICE_ID, TOPIC_SCHEDULED_SET, TOPIC_SCHEDULED_STATE, TOPIC_AVAILABILITY, DEVICE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_MODEL);
  publishDiscovery("switch", "scheduled_mode", payload);
}

void connectWifi() {
  Serial.printf("[WIFI] Connecting to %s...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true); // Enable background auto-reconnect
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  // Wait up to 15 seconds for initial connection, then proceed offline if down
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
    if (millis() - start > 15000) {
      Serial.println("\n[WIFI] Timeout. Will continue attempting connection in background.");
      break;
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
  }
}

void sendBridgeCommand(const char* field, const char* value) {
  char line[80];
  long commandId = nextCommandId++;
  snprintf(line, sizeof(line), "CMD,id=%ld,%s=%s", commandId, field, value);
  BridgeSerial.println(line);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[32];
  if (length >= sizeof(msg)) {
    return;
  }
  memcpy(msg, payload, length);
  msg[length] = '\0';

  Serial.printf("[MQTT] Received %s: %s\n", topic, msg);

  if (strcmp(topic, TOPIC_LEVEL_SET) == 0) {
    int level = atoi(msg);
    if (level == 3) {
      if (state.powerVent != 1) {
        sendBridgeCommand("power_vent", "1");
      }
    } else if (level >= 0 && level <= 2) {
      if (state.powerVent == 1) {
        sendBridgeCommand("power_vent", "0");
      }
      char value[4];
      itoa(level, value, 10);
      sendBridgeCommand("level", value);
    }
  } else if (strcmp(topic, TOPIC_POWER_SET) == 0) {
    if (strcmp(msg, "0") == 0 || strcmp(msg, "1") == 0) {
      sendBridgeCommand("power_vent", msg);
    }
  } else if (strcmp(topic, TOPIC_SCHEDULED_SET) == 0) {
    if (strcmp(msg, "0") == 0 || strcmp(msg, "1") == 0) {
      sendBridgeCommand("scheduled", msg);
    }
  }
}

void ensureMqttConnected() {
  if (mqttClient.connected()) {
    return;
  }

  uint32_t now = millis();
  if (now - lastMqttAttempt < 5000) {
    return; // Limit connection attempts to once every 5 seconds
  }
  lastMqttAttempt = now;

  Serial.println("[MQTT] Attempting connection...");
  String clientId = String("esp32-lwz180-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  mqttClient.setKeepAlive(30);
  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD, TOPIC_AVAILABILITY, 1, true, "offline")) {
    Serial.println("[MQTT] Connected");
    publishAvailability(true);
    publishDiscoveryConfig();
    mqttClient.subscribe(TOPIC_LEVEL_SET);
    mqttClient.subscribe(TOPIC_POWER_SET);
    mqttClient.subscribe(TOPIC_SCHEDULED_SET);
    // Request current status from bridge to synchronize state immediately
    BridgeSerial.println("CMD,id=0,query=1");
  } else {
    Serial.printf("[MQTT] Connection failed, rc=%d. Will retry.\n", mqttClient.state());
  }
}

void publishMeasurement(const char* key, const char* value) {
  if (strcmp(key, "outside_temp") == 0) {
    publishRetained(TOPIC_TEMP_OUTSIDE, value);
  } else if (strcmp(key, "exhaust_humidity") == 0) {
    publishRetained(TOPIC_HUMIDITY_EXHAUST, value);
  } else if (strcmp(key, "inlet_flow") == 0) {
    publishRetained(TOPIC_FLOW_INLET, value);
  } else if (strcmp(key, "inlet_power") == 0) {
    publishRetained(TOPIC_POWER_INLET, value);
  } else if (strcmp(key, "dew_point_extract") == 0) {
    publishRetained(TOPIC_DEWPOINT_EXTRACT, value);
  } else if (strcmp(key, "dew_point_outdoor") == 0) {
    publishRetained(TOPIC_DEWPOINT_OUTDOOR, value);
  } else if (strcmp(key, "supply_temp") == 0) {
    publishRetained(TOPIC_TEMP_SUPPLY, value);
  } else if (strcmp(key, "extract_temp") == 0) {
    publishRetained(TOPIC_TEMP_EXTRACT, value);
  } else if (strcmp(key, "exhaust_temp") == 0) {
    publishRetained(TOPIC_TEMP_EXHAUST, value);
  } else if (strcmp(key, "outdoor_humidity") == 0) {
    publishRetained(TOPIC_HUMIDITY_OUTDOOR, value);
  } else if (strcmp(key, "exhaust_flow") == 0) {
    publishRetained(TOPIC_FLOW_EXHAUST, value);
  } else if (strcmp(key, "exhaust_power") == 0) {
    publishRetained(TOPIC_POWER_EXHAUST, value);
  } else if (strcmp(key, "preheater_power") == 0) {
    publishRetained(TOPIC_POWER_PREHEATER, value);
  } else if (strcmp(key, "filter_life") == 0) {
    publishRetained(TOPIC_FILTER_LIFE, value);
  }
}

int parseStatField(const String& line, const char* key) {
  String needle = String(key) + "=";
  int start = line.indexOf(needle);
  if (start < 0) {
    return -1;
  }
  start += needle.length();
  int end = line.indexOf(',', start);
  if (end < 0) {
    end = line.length();
  }
  return line.substring(start, end).toInt();
}

void handleBridgeLine(const String& line) {
  Serial.printf("[BRIDGE] %s\n", line.c_str());

  if (line == "READY") {
    publishAvailability(true);
    return;
  }

  if (line.startsWith("MEAS,")) {
    int comma1 = line.indexOf(',');
    int comma2 = line.indexOf(',', comma1 + 1);
    if (comma1 > -1 && comma2 > -1) {
      String key = line.substring(comma1 + 1, comma2);
      String value = line.substring(comma2 + 1);
      publishMeasurement(key.c_str(), value.c_str());
    }
    return;
  }

  if (line.startsWith("STAT,")) {
    int powerVent = parseStatField(line, "power_vent");
    int scheduled = parseStatField(line, "scheduled");
    int level = parseStatField(line, "level");

    if (powerVent >= 0) {
      state.powerVent = powerVent;
      publishRetained(TOPIC_POWER_STATE, powerVent ? "1" : "0");
    }
    if (scheduled >= 0) {
      state.scheduled = scheduled;
      publishRetained(TOPIC_SCHEDULED_STATE, scheduled ? "1" : "0");
    }
    if (level >= 0) {
      state.level = level;
      char lvl[4];
      itoa(level, lvl, 10);
      publishRetained(TOPIC_LEVEL_STATE, lvl);
    }
    return;
  }

  if (line.startsWith("ACK,")) {
    publishRetained(TOPIC_ACK, line.c_str());
  }
}

void readBridgeSerial() {
  while (BridgeSerial.available() > 0) {
    char ch = (char)BridgeSerial.read();
    if (ch == '\n') {
      handleBridgeLine(serialLine);
      serialLine = "";
    } else if (ch != '\r') {
      serialLine += ch;
      if (serialLine.length() > 180) {
        serialLine = "";
      }
    }
  }
}

String usbSerialLine;

void handleUsbCommand(const String& cmd) {
  if (cmd == "3") {
    Serial.println("[USB-CLI] Setting ventilation level to 3 (Power Vent / Boost)");
    if (state.powerVent != 1) {
      sendBridgeCommand("power_vent", "1");
    }
  } else if (cmd == "0" || cmd == "1" || cmd == "2") {
    Serial.printf("[USB-CLI] Setting ventilation level to %s\n", cmd.c_str());
    if (state.powerVent == 1) {
      sendBridgeCommand("power_vent", "0");
    }
    sendBridgeCommand("level", cmd.c_str());
  } else if (cmd == "q") {
    Serial.println("[USB-CLI] Querying bridge status...");
    long commandId = nextCommandId++;
    BridgeSerial.printf("CMD,id=%ld,query=1\n", commandId);
  } else if (cmd == "p") {
    int nextPower = (state.powerVent == 1) ? 0 : 1;
    Serial.printf("[USB-CLI] Toggling power vent to %d\n", nextPower);
    sendBridgeCommand("power_vent", nextPower ? "1" : "0");
  } else if (cmd == "s") {
    int nextScheduled = (state.scheduled == 1) ? 0 : 1;
    Serial.printf("[USB-CLI] Toggling scheduled mode to %d\n", nextScheduled);
    sendBridgeCommand("scheduled", nextScheduled ? "1" : "0");
  } else if (cmd == "w") {
    Serial.println("[USB-CLI] --- Network Diagnostics ---");
    Serial.printf("  WiFi Status : %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    Serial.printf("  IP Address  : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  WiFi RSSI   : %d dBm\n", WiFi.RSSI());
    Serial.printf("  MQTT Status : %s (rc=%d)\n", mqttClient.connected() ? "Connected" : "Disconnected", mqttClient.state());
    Serial.printf("  Current State: Level=%d, Scheduled=%d, PowerVent=%d\n", state.level, state.scheduled, state.powerVent);
  } else if (cmd.startsWith("CMD,")) {
    Serial.printf("[USB-CLI] Forwarding raw command: %s\n", cmd.c_str());
    BridgeSerial.println(cmd);
  } else {
    Serial.println("[USB-CLI] Commands: 0/1/2/3 = set level, q = query bridge, p = toggle power vent, s = toggle schedule, w = wifi/mqtt status");
  }
}

void readUsbSerial() {
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (usbSerialLine.length() > 0) {
        handleUsbCommand(usbSerialLine);
        usbSerialLine = "";
      }
    } else {
      usbSerialLine += ch;
      if (usbSerialLine.length() > 80) {
        usbSerialLine = "";
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);

  BridgeSerial.begin(BRIDGE_SERIAL_BAUD, SERIAL_8N1, BRIDGE_UART_RX_PIN, BRIDGE_UART_TX_PIN);

  connectWifi();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(1024);  // HA discovery payloads exceed default 256
  mqttClient.setCallback(mqttCallback);

  ensureMqttConnected();
}

void loop() {
  uint32_t now = millis();

  // If Wi-Fi is connected, run MQTT tasks asynchronously
  if (WiFi.status() == WL_CONNECTED) {
    ensureMqttConnected();
    if (mqttClient.connected()) {
      mqttClient.loop();
    }
  } else {
    // Non-blocking WiFi reconnect fallback
    if (now - lastWifiAttempt > 10000) {
      lastWifiAttempt = now;
      Serial.println("[WIFI] Disconnected. Reconnecting...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }

  // Parse commands from USB Serial console
  readUsbSerial();

  // Always parse data from the bridge to prevent serial RX buffer overflow
  readBridgeSerial();

  if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
    if (now - lastAvailabilityPublish > 30000) {
      lastAvailabilityPublish = now;
      publishAvailability(true);
    }
  }

  delay(5);
}
