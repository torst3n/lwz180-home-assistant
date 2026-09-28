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
#include <WebServer.h>
#include <ArduinoOTA.h>

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

// ------------------ REMOTE DEBUG & WEB ----------------
WiFiServer telnetServer(23);
WiFiClient telnetClient;
WebServer webServer(80);

#define LOG_BUFFER_SIZE 60
String logRingBuffer[LOG_BUFFER_SIZE];
int logRingHead = 0;
int logRingCount = 0;

void logMessage(const String& msg) {
  unsigned long s = millis() / 1000;
  char timePrefix[16];
  snprintf(timePrefix, sizeof(timePrefix), "[%02lu:%02lu:%02lu] ", (s / 3600), (s % 3600) / 60, s % 60);
  String entry = String(timePrefix) + msg;

  Serial.println(entry);
  if (telnetClient && telnetClient.connected()) {
    telnetClient.println(entry);
  }

  logRingBuffer[logRingHead] = entry;
  logRingHead = (logRingHead + 1) % LOG_BUFFER_SIZE;
  if (logRingCount < LOG_BUFFER_SIZE) {
    logRingCount++;
  }
}

String serialLine;
uint32_t lastAvailabilityPublish = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastMqttConnectedTime = 0;
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

  logMessage("[MQTT] Received " + String(topic) + ": " + String(msg));

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
    lastMqttConnectedTime = millis();
    return;
  }

  uint32_t now = millis();
  if (now - lastMqttAttempt < 5000) {
    return; // Limit connection attempts to once every 5 seconds
  }
  lastMqttAttempt = now;

  logMessage("[MQTT] Attempting connection...");

  // Explicitly close any existing or half-closed TCP socket to prevent descriptor leaks
  wifiClient.stop();

  String clientId = String("esp32-lwz180-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  mqttClient.setKeepAlive(30);

  IPAddress serverIp;
  if (serverIp.fromString(MQTT_HOST)) {
    mqttClient.setServer(serverIp, MQTT_PORT);
  } else {
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  }

  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD, TOPIC_AVAILABILITY, 1, true, "offline")) {
    logMessage("[MQTT] Connected");
    lastMqttConnectedTime = now;
    publishAvailability(true);
    publishDiscoveryConfig();
    mqttClient.subscribe(TOPIC_LEVEL_SET);
    mqttClient.subscribe(TOPIC_POWER_SET);
    mqttClient.subscribe(TOPIC_SCHEDULED_SET);
    // Request current status from bridge to synchronize state immediately
    BridgeSerial.println("CMD,id=0,query=1");
  } else {
    logMessage("[MQTT] Connection failed, rc=" + String(mqttClient.state()) + ". Will retry.");
    // Watchdog: If disconnected continuously for > 3 minutes while WiFi is OK, reboot ESP32
    if (lastMqttConnectedTime > 0 && (now - lastMqttConnectedTime > 180000)) {
      logMessage("[WATCHDOG] MQTT disconnected for > 3 minutes. Restarting ESP32...");
      delay(300);
      ESP.restart();
    }
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
  logMessage("[BRIDGE] " + line);

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

void handleCommand(const String& cmd) {
  if (cmd == "3") {
    logMessage("[CMD] Setting ventilation level to 3 (Power Vent / Boost)");
    if (state.powerVent != 1) {
      sendBridgeCommand("power_vent", "1");
    }
  } else if (cmd == "0" || cmd == "1" || cmd == "2") {
    logMessage("[CMD] Setting ventilation level to " + cmd);
    if (state.powerVent == 1) {
      sendBridgeCommand("power_vent", "0");
    }
    sendBridgeCommand("level", cmd.c_str());
  } else if (cmd == "q") {
    logMessage("[CMD] Querying bridge status...");
    long commandId = nextCommandId++;
    BridgeSerial.printf("CMD,id=%ld,query=1\n", commandId);
  } else if (cmd == "p") {
    int nextPower = (state.powerVent == 1) ? 0 : 1;
    logMessage("[CMD] Toggling power vent to " + String(nextPower));
    sendBridgeCommand("power_vent", nextPower ? "1" : "0");
  } else if (cmd == "s") {
    int nextScheduled = (state.scheduled == 1) ? 0 : 1;
    logMessage("[CMD] Toggling scheduled mode to " + String(nextScheduled));
    sendBridgeCommand("scheduled", nextScheduled ? "1" : "0");
  } else if (cmd == "w") {
    logMessage("[CMD] --- Diagnostics ---");
    logMessage("  WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + " IP: " + WiFi.localIP().toString() + " RSSI: " + String(WiFi.RSSI()) + " dBm");
    logMessage("  MQTT: " + String(mqttClient.connected() ? "Connected" : "Disconnected") + " (rc=" + String(mqttClient.state()) + ")");
    logMessage("  State: Level=" + String(state.level) + " Sched=" + String(state.scheduled) + " PowerVent=" + String(state.powerVent));
    logMessage("  Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
  } else if (cmd == "reboot" || cmd == "restart") {
    logMessage("[CMD] Rebooting ESP32...");
    delay(250);
    ESP.restart();
  } else if (cmd.startsWith("CMD,")) {
    logMessage("[CMD] Forwarding raw command: " + cmd);
    BridgeSerial.println(cmd);
  } else {
    logMessage("[CMD] Commands: 0/1/2/3 = level, q = query, p = power vent, s = schedule, w = status, reboot = restart");
  }
}

String usbSerialLine;

void readUsbSerial() {
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (usbSerialLine.length() > 0) {
        handleCommand(usbSerialLine);
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

void handleTelnet() {
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) telnetClient.stop();
      telnetClient = telnetServer.available();
      telnetClient.println("\n=== LWZ180 ESP32 Remote Console ===");
      telnetClient.println("Commands: 0/1/2/3 = level, q = query, p = power vent, s = schedule, w = status, reboot = restart");
      telnetClient.print("> ");
    } else {
      WiFiClient rejected = telnetServer.available();
      rejected.println("Busy: Another client is connected.");
      rejected.stop();
    }
  }

  static String telnetLine = "";
  while (telnetClient && telnetClient.available()) {
    char ch = (char)telnetClient.read();
    if (ch == '\r' || ch == '\n') {
      if (telnetLine.length() > 0) {
        handleCommand(telnetLine);
        telnetLine = "";
        if (telnetClient && telnetClient.connected()) {
          telnetClient.print("> ");
        }
      }
    } else {
      telnetLine += ch;
      if (telnetLine.length() > 80) {
        telnetLine = "";
      }
    }
  }
}

void setupWebServer() {
  webServer.on("/", HTTP_GET, []() {
    String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                    "<title>LWZ 180 Bridge</title>"
                    "<style>"
                    "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#121212;color:#eee;margin:0;padding:16px}"
                    ".card{background:#1e1e1e;border-radius:8px;padding:16px;margin-bottom:16px;box-shadow:0 2px 4px rgba(0,0,0,0.4)}"
                    "h2{margin:0 0 12px;color:#4fc3f7;font-size:1.2em}"
                    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}"
                    ".box{background:#2a2a2a;border-radius:6px;padding:10px}"
                    ".lbl{font-size:0.75em;color:#aaa;text-transform:uppercase}"
                    ".val{font-size:1.3em;font-weight:bold;margin-top:2px}"
                    ".ok{color:#81c784}.err{color:#e57373}"
                    ".btn{display:inline-block;background:#0288d1;color:#fff;text-decoration:none;padding:8px 14px;border-radius:4px;font-weight:bold;margin:3px;border:none;cursor:pointer;font-size:0.9em}"
                    ".btn:hover{background:#039be5}"
                    ".btn-red{background:#d32f2f}.btn-red:hover{background:#f44336}"
                    ".log{background:#000;color:#a5d6a7;font-family:monospace;font-size:11px;padding:10px;border-radius:6px;height:240px;overflow-y:scroll;white-space:pre-wrap;word-break:break-all}"
                    "</style></head><body>"
                    "<div class='card'><h2>LWZ 180 Bridge Status</h2>"
                    "<div class='grid'>"
                    "<div class='box'><div class='lbl'>Ventilation Level</div><div class='val'>");
    html += (state.level >= 0 ? String(state.level) : "Unknown");
    html += F("</div></div><div class='box'><div class='lbl'>Scheduled Mode</div><div class='val'>");
    html += (state.scheduled == 1 ? "ACTIVE" : (state.scheduled == 0 ? "OFF" : "Unknown"));
    html += F("</div></div><div class='box'><div class='lbl'>Power Vent</div><div class='val'>");
    html += (state.powerVent == 1 ? "BOOST" : (state.powerVent == 0 ? "OFF" : "Unknown"));
    html += F("</div></div><div class='box'><div class='lbl'>MQTT Connection</div><div class='val ");
    html += (mqttClient.connected() ? "ok'>CONNECTED" : "err'>DISCONNECTED");
    html += F("</div></div><div class='box'><div class='lbl'>WiFi Signal</div><div class='val'>");
    html += String(WiFi.RSSI()) + " dBm";
    html += F("</div></div><div class='box'><div class='lbl'>Uptime</div><div class='val'>");
    unsigned long s = millis() / 1000;
    char upStr[24];
    snprintf(upStr, sizeof(upStr), "%luh %02lum %02lus", s / 3600, (s % 3600) / 60, s % 60);
    html += String(upStr);
    html += F("</div></div></div></div>"
              "<div class='card'><h2>Quick Controls</h2>"
              "<a class='btn' href='/cmd?c=0'>Level 0</a>"
              "<a class='btn' href='/cmd?c=1'>Level 1</a>"
              "<a class='btn' href='/cmd?c=2'>Level 2</a>"
              "<a class='btn' href='/cmd?c=3'>Level 3</a>"
              "<a class='btn' href='/cmd?c=p'>Toggle Boost</a>"
              "<a class='btn' href='/cmd?c=s'>Toggle Schedule</a>"
              "<a class='btn' href='/cmd?c=q'>Query Bridge</a>"
              "<a class='btn btn-red' href='/reboot' onclick=\"return confirm('Restart ESP32?');\">Restart ESP32</a>"
              "</div>"
              "<div class='card'><h2>Live Log Stream (Last 60 lines)</h2>"
              "<div class='log' id='logbox'>");

    int startIdx = (logRingCount < LOG_BUFFER_SIZE) ? 0 : logRingHead;
    for (int i = 0; i < logRingCount; i++) {
      int idx = (startIdx + i) % LOG_BUFFER_SIZE;
      html += logRingBuffer[idx] + "\n";
    }

    html += F("</div><p style='margin-top:8px'><a class='btn' href='/'>Refresh</a> <a class='btn' href='/log' target='_blank'>Raw Log</a></p>"
              "<script>var b=document.getElementById('logbox');b.scrollTop=b.scrollHeight;</script>"
              "</div></body></html>");

    webServer.send(200, "text/html", html);
  });

  webServer.on("/cmd", HTTP_GET, []() {
    if (webServer.hasArg("c")) {
      String c = webServer.arg("c");
      handleCommand(c);
    }
    webServer.sendHeader("Location", "/");
    webServer.send(303);
  });

  webServer.on("/log", HTTP_GET, []() {
    String out = "";
    int startIdx = (logRingCount < LOG_BUFFER_SIZE) ? 0 : logRingHead;
    for (int i = 0; i < logRingCount; i++) {
      int idx = (startIdx + i) % LOG_BUFFER_SIZE;
      out += logRingBuffer[idx] + "\n";
    }
    webServer.send(200, "text/plain", out);
  });

  webServer.on("/reboot", HTTP_GET, []() {
    webServer.send(200, "text/html", F("<html><body style='background:#121212;color:#eee;font-family:sans-serif;padding:30px'><h2>Rebooting ESP32...</h2><p>Please wait 10 seconds, then <a href='/' style='color:#4fc3f7'>return to Dashboard</a>.</p><script>setTimeout(function(){location.href='/';},10000);</script></body></html>"));
    delay(500);
    ESP.restart();
  });

  webServer.begin();
}

void setupOta() {
  ArduinoOTA.setHostname("esp32-lwz180");
  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    logMessage("[OTA] Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    logMessage("[OTA] Finished. Rebooting...");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    logMessage("[OTA] Error (" + String(error) + ")");
  });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  delay(50);

  BridgeSerial.begin(BRIDGE_SERIAL_BAUD, SERIAL_8N1, BRIDGE_UART_RX_PIN, BRIDGE_UART_TX_PIN);

  connectWifi();

  // Start Telnet Server on port 23
  telnetServer.begin();
  telnetServer.setNoDelay(true);

  // Start Web Server on port 80
  setupWebServer();

  // Start Arduino OTA on port 3232
  setupOta();

  lastMqttConnectedTime = millis();
  mqttClient.setBufferSize(1024);  // HA discovery payloads exceed default 256
  mqttClient.setCallback(mqttCallback);

  ensureMqttConnected();
  logMessage("[SYSTEM] ESP32 Ready. Web: http://" + WiFi.localIP().toString() + ", Telnet: port 23, OTA active");
}

void loop() {
  uint32_t now = millis();

  // Handle OTA updates
  ArduinoOTA.handle();

  // Handle Web Server requests
  webServer.handleClient();

  // Handle Telnet connections & commands
  handleTelnet();

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
      logMessage("[WIFI] Disconnected. Reconnecting...");
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

  delay(2);
}
