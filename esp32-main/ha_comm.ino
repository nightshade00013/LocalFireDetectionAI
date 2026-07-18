#include "ha_comm.h"
#include <WiFi.h>
#include <WebServer.h>         // simple REST endpoints
#include <PubSubClient.h>      // MQTT

// ========== CONFIGURE ==========
#define ENABLE_HA_MQTT 1
#define ENABLE_HA_REST 1

// Replace these with your values or edit at compile time
const char* WIFI_SSID = "fire-hotspot";   // default from sketch; kept for convenience
const char* WIFI_PASS = "password";

const char* MQTT_BROKER = "192.168.1.10";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = ""; // set if needed
const char* MQTT_PASS = "";

const char* HA_DISCOVERY_PREFIX = "homeassistant"; // default Home Assistant discovery prefix
const char* DEVICE_ID = "fire_detector_1";         // unique id used in HA discovery and topics
const char* BASE_TOPIC = "fire";                   // base topic: e.g., fire/fire_detector_1/...

const uint16_t REST_PORT = 80;
// =================================

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer webServer(REST_PORT);

// last published values (kept for REST)
static bool last_is_fire = false;
static float last_prob = 0.0f;
static int last_mq2 = 0, last_mq5 = 0, last_mq7 = 0;
static float last_temp = 0.0f, last_hum = 0.0f;
static bool last_flame = false;

static unsigned long lastMqttReconnect = 0;
static const unsigned long MQTT_RECONNECT_INTERVAL = 5000;

void send_ha_mqtt_discovery() {
  // Binary sensor discovery for fire (device_class smoke)
  String base = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + DEVICE_ID + "/config";
  String state_topic = String(BASE_TOPIC) + "/" + DEVICE_ID + "/state";
  String payload = "{";
  payload += "\"name\":\"Fire Detector\",";
  payload += "\"state_topic\":\"" + state_topic + "\",";
  payload += "\"device_class\":\"smoke\",";
  payload += "\"unique_id\":\"" + String(DEVICE_ID) + "_smoke\",";
  payload += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],\"name\":\"Fire Detector\"}";
  payload += "}";
  mqttClient.publish(base.c_str(), payload.c_str(), true);

  // Sensor for probability
  String base2 = String(HA_DISCOVERY_PREFIX) + "/sensor/" + DEVICE_ID + "/prob/config";
  String prob_topic = String(BASE_TOPIC) + "/" + DEVICE_ID + "/prob";
  String payload2 = "{";
  payload2 += "\"name\":\"Fire Probability\",";
  payload2 += "\"state_topic\":\"" + prob_topic + "\",";
  payload2 += "\"unit_of_measurement\":\"\",";
  payload2 += "\"unique_id\":\"" + String(DEVICE_ID) + "_prob\",";
  payload2 += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],\"name\":\"Fire Detector\"}";
  payload2 += "}";
  mqttClient.publish(base2.c_str(), payload2.c_str(), true);

  // Individual sensor discovery messages (MQ2, MQ5, MQ7, Temperature, Humidity, Flame)
  String sensorsBase = String(HA_DISCOVERY_PREFIX) + "/sensor/" + DEVICE_ID;
  String mq2_topic = String(BASE_TOPIC) + "/" + DEVICE_ID + "/mq2";
  String mq5_topic = String(BASE_TOPIC) + "/" + DEVICE_ID + "/mq5";
  String mq7_topic = String(BASE_TOPIC) + "/" + DEVICE_ID + "/mq7";
  String temp_topic = String(BASE_TOPIC) + "/" + DEVICE_ID + "/temp";
  String hum_topic = String(BASE_TOPIC) + "/" + DEVICE_ID + "/hum";
  String flame_topic = String(BASE_TOPIC) + "/" + DEVICE_ID + "/flame";

  // MQ2
  String mq2_cfg = "{";
  mq2_cfg += "\"name\":\"MQ2 Smoke\",";
  mq2_cfg += "\"state_topic\":\"" + mq2_topic + "\",";
  mq2_cfg += "\"unit_of_measurement\":\"ADC\",";
  mq2_cfg += "\"unique_id\":\"" + String(DEVICE_ID) + "_mq2\",";
  mq2_cfg += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],\"name\":\"Fire Detector\"}";
  mq2_cfg += "}";
  mqttClient.publish((sensorsBase + "/mq2/config").c_str(), mq2_cfg.c_str(), true);

  // MQ5
  String mq5_cfg = "{";
  mq5_cfg += "\"name\":\"MQ5 LPG\",";
  mq5_cfg += "\"state_topic\":\"" + mq5_topic + "\",";
  mq5_cfg += "\"unit_of_measurement\":\"ADC\",";
  mq5_cfg += "\"unique_id\":\"" + String(DEVICE_ID) + "_mq5\",";
  mq5_cfg += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],\"name\":\"Fire Detector\"}";
  mq5_cfg += "}";
  mqttClient.publish((sensorsBase + "/mq5/config").c_str(), mq5_cfg.c_str(), true);

  // MQ7
  String mq7_cfg = "{";
  mq7_cfg += "\"name\":\"MQ7 CO\",";
  mq7_cfg += "\"state_topic\":\"" + mq7_topic + "\",";
  mq7_cfg += "\"unit_of_measurement\":\"ADC\",";
  mq7_cfg += "\"unique_id\":\"" + String(DEVICE_ID) + "_mq7\",";
  mq7_cfg += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],\"name\":\"Fire Detector\"}";
  mq7_cfg += "}";
  mqttClient.publish((sensorsBase + "/mq7/config").c_str(), mq7_cfg.c_str(), true);

  // Temperature sensor
  String temp_cfg = "{";
  temp_cfg += "\"name\":\"Ambient Temperature\",";
  temp_cfg += "\"state_topic\":\"" + temp_topic + "\",";
  temp_cfg += "\"unit_of_measurement\":\"°C\",";
  temp_cfg += "\"unique_id\":\"" + String(DEVICE_ID) + "_temp\",";
  temp_cfg += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],\"name\":\"Fire Detector\"}";
  temp_cfg += "}";
  mqttClient.publish((sensorsBase + "/temp/config").c_str(), temp_cfg.c_str(), true);

  // Humidity sensor
  String hum_cfg = "{";
  hum_cfg += "\"name\":\"Ambient Humidity\",";
  hum_cfg += "\"state_topic\":\"" + hum_topic + "\",";
  hum_cfg += "\"unit_of_measurement\":\"%\",";
  hum_cfg += "\"unique_id\":\"" + String(DEVICE_ID) + "_hum\",";
  hum_cfg += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],\"name\":\"Fire Detector\"}";
  hum_cfg += "}";
  mqttClient.publish((sensorsBase + "/hum/config").c_str(), hum_cfg.c_str(), true);

  // Flame binary sensor (use binary_sensor discovery)
  String flame_base = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + DEVICE_ID + "/flame/config";
  String flame_payload = "{";
  flame_payload += "\"name\":\"Flame Sensor\",";
  flame_payload += "\"state_topic\":\"" + flame_topic + "\",";
  flame_payload += "\"device_class\":\"safety\","; // best-effort device class
  flame_payload += "\"unique_id\":\"" + String(DEVICE_ID) + "_flame\",";
  flame_payload += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],\"name\":\"Fire Detector\"}";
  flame_payload += "}";
  mqttClient.publish(flame_base.c_str(), flame_payload.c_str(), true);
}

void mqtt_connect() {
  if (mqttClient.connected()) return;
  if (!WiFi.isConnected()) return;

  unsigned long now = millis();
  if (now - lastMqttReconnect < MQTT_RECONNECT_INTERVAL) return;
  lastMqttReconnect = now;

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  bool ok;
  if (strlen(MQTT_USER) == 0) {
    ok = mqttClient.connect(DEVICE_ID);
  } else {
    ok = mqttClient.connect(DEVICE_ID, MQTT_USER, MQTT_PASS);
  }
  if (ok) {
    // publish discovery
    send_ha_mqtt_discovery();
  } else {
    // failed to connect; will retry later
  }
}

void ha_init() {
#if ENABLE_HA_MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
#endif

#if ENABLE_HA_REST
  webServer.on("/status", []() {
    String json = "{";
    json += "\"is_fire\":" + String(last_is_fire ? "true" : "false") + ",";
    json += "\"prob\":" + String(last_prob, 4) + ",";
    json += "\"mq2\":" + String(last_mq2) + ",";
    json += "\"mq5\":" + String(last_mq5) + ",";
    json += "\"mq7\":" + String(last_mq7) + ",";
    json += "\"temp\":" + String(last_temp, 2) + ",";
    json += "\"hum\":" + String(last_hum, 2) + ",";
    json += "\"flame\":" + String(last_flame ? "1":"0");
    json += "}";
    webServer.send(200, "application/json", json);
  });
  webServer.begin();
#endif
}

void ha_loop() {
#if ENABLE_HA_MQTT
  if (!mqttClient.connected()) {
    mqtt_connect();
  } else {
    mqttClient.loop();
  }
#endif

#if ENABLE_HA_REST
  webServer.handleClient();
#endif
}

void ha_publish_detection(bool is_fire, float prob) {
  last_is_fire = is_fire;
  last_prob = prob;

#if ENABLE_HA_MQTT
  if (mqttClient.connected()) {
    String stateTopic = String(BASE_TOPIC) + "/" + DEVICE_ID + "/state";
    String probTopic = String(BASE_TOPIC) + "/" + DEVICE_ID + "/prob";
    mqttClient.publish(stateTopic.c_str(), is_fire ? "ON" : "OFF", true);
    mqttClient.publish(probTopic.c_str(), String(prob, 4).c_str(), true);
  }
#endif
}

void ha_publish_sensors(int mq2, int mq5, int mq7, float temp, float hum, bool flame) {
  last_mq2 = mq2; last_mq5 = mq5; last_mq7 = mq7;
  last_temp = temp; last_hum = hum; last_flame = flame;

#if ENABLE_HA_MQTT
  if (mqttClient.connected()) {
    // publish sensor values under topics like fire/fire_detector_1/mq2 etc.
    String base = String(BASE_TOPIC) + "/" + DEVICE_ID + "/";
    mqttClient.publish((base + "mq2").c_str(), String(mq2).c_str(), true);
    mqttClient.publish((base + "mq5").c_str(), String(mq5).c_str(), true);
    mqttClient.publish((base + "mq7").c_str(), String(mq7).c_str(), true);
    mqttClient.publish((base + "temp").c_str(), String(temp,2).c_str(), true);
    mqttClient.publish((base + "hum").c_str(), String(hum,2).c_str(), true);
    mqttClient.publish((base + "flame").c_str(), flame ? "1":"0", true);
  }
#endif
}
