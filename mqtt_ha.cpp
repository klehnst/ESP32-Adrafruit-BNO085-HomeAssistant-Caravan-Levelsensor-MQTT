#include "mqtt_ha.h"
#include "sensor.h"
#include "wifi_manager.h"
#include "webserver.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient espClient;
static PubSubClient mqtt(espClient);
static unsigned long lastPublish = 0;
static unsigned long lastReconnect = 0;
static bool discoveryPublished = false;

// Forward declarations
static void mqttCallback(char* topic, byte* payload, unsigned int length);
static void publishSensor(const char* name, const char* id, const char* unit, 
                          const char* devClass, const char* icon, const char* valueTpl);
static void publishSelect(const char* name, const char* id, const char* icon, const char* optionsCSV);
static void publishNumber(const char* name, const char* id, const char* icon, float minVal, float maxVal, float step, const char* unit);
static void publishButton(const char* name, const char* id, const char* icon, const char* cmdTopic, const char* payload);
static void publishBinarySensor(const char* name, const char* id, const char* icon);
static void publishSwitch(const char* name, const char* id, const char* icon, const char* cmdTopic);
static void removeDiscoveryEntity(const char* component, const char* id);
static void removeOldWedgeEntities(VehicleType newType);
static void addDeviceBlock(JsonDocument& doc);

void mqttInit() {
    if (strlen(config.mqtt_server) == 0) return;
    
    mqtt.setServer(config.mqtt_server, config.mqtt_port);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(1024);
    mqtt.setKeepAlive(30);       // 30s Keepalive (Standard 15s ist zu knapp)
    mqtt.setSocketTimeout(10);   // 10s Socket-Timeout
    
    Serial.printf("[MQTT] Configured: %s:%d\n", config.mqtt_server, config.mqtt_port);
}

void mqttReconfigure() {
    // Host und Port koennen aus der Web-UI geändert werden. PubSubClient
    // übernimmt sie erst nach setServer(), daher die bestehende Session beenden.
    mqtt.disconnect();
    mqtt.setServer(config.mqtt_server, config.mqtt_port);
    discoveryPublished = false;
    lastReconnect = 0;
    Serial.printf("[MQTT] Reconfigured: %s:%d\n", config.mqtt_server, config.mqtt_port);
}

void mqttLoop() {
    if (!isConnected()) return;
    if (strlen(config.mqtt_server) == 0) return;
    
    if (!mqtt.connected()) {
        if (millis() - lastReconnect > 2000) {
            lastReconnect = millis();
            Serial.println("[MQTT] Attempting connection...");
            
            // Feste Client-ID (aus MAC) — verhindert dass der Broker
            // bei jedem Reconnect eine neue Session anlegt
            String clientId = "wowa-level-" + WiFi.macAddress();
            clientId.replace(":", "");
            bool connected;
            
            // Last Will & Testament (LWT):
            // Broker sendet automatisch "offline" auf /status wenn die
            // Verbindung unerwartet abreisst (Absturz, Stromverlust, WLAN weg).
            // HA erkennt den Sensor dann als "nicht verfuegbar".
            String willTopic = String(config.mqtt_prefix) + "/status";
            
            if (strlen(config.mqtt_user) > 0) {
                connected = mqtt.connect(clientId.c_str(), config.mqtt_user, config.mqtt_pass,
                    willTopic.c_str(), 0, true, "offline");
            } else {
                connected = mqtt.connect(clientId.c_str(),
                    NULL, NULL, willTopic.c_str(), 0, true, "offline");
            }
            
            if (connected) {
                Serial.println("[MQTT] Connected!");
                
                // Subscribe to ALL command topics with wildcard
                String cmdBase = String(config.mqtt_prefix) + "/cmd/#";
                mqtt.subscribe(cmdBase.c_str());
                
                // Publish discovery
                if (!discoveryPublished) {
                    mqttPublishDiscovery();
                    discoveryPublished = true;
                }
                
                // Publish online status
                String statusTopic = String(config.mqtt_prefix) + "/status";
                mqtt.publish(statusTopic.c_str(), "online", true);
            } else {
                Serial.printf("[MQTT] Failed, rc=%d\n", mqtt.state());
            }
        }
        return;
    }
    
    mqtt.loop();
    
    // Publish sensor data periodically
    unsigned long mqttInterval = config.mqtt_fast ? MQTT_FAST_MS : MQTT_SLOW_MS;
        if (millis() - lastPublish > mqttInterval) {
        lastPublish = millis();
        mqttPublishState();
    }
}

bool mqttIsConnected() {
    return mqtt.connected();
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String topicStr = String(topic);
    String payloadStr;
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }
    payloadStr.trim();
    
    Serial.printf("[MQTT] Received: %s = %s\n", topic, payloadStr.c_str());
    
    String cmdPrefix = String(config.mqtt_prefix) + "/cmd/";
    String payloadUpper = payloadStr;
    payloadUpper.toUpperCase();
    
    if (topicStr == cmdPrefix + "tare/set") {
        // Button: Tare setzen
        sensorSetTare();
        // Publish confirmation
        String prefix = String(config.mqtt_prefix);
        mqtt.publish((prefix + "/tare_active").c_str(), "ON", true);
        Serial.println("[MQTT] Tare SET via HA");
    } else if (topicStr == cmdPrefix + "tare_reset/set") {
        // Button: Tare zuruecksetzen
        sensorResetTare();
        String prefix = String(config.mqtt_prefix);
        mqtt.publish((prefix + "/tare_active").c_str(), "OFF", true);
        Serial.println("[MQTT] Tare RESET via HA");
    } else if (topicStr == cmdPrefix + "calibration_reset/set") {
        // Button: Kalibrierung zuruecksetzen
        sensorResetCalibration();
        Serial.println("[MQTT] Calibration RESET via HA");
    } else if (topicStr == cmdPrefix + "tare") {
        // Legacy: cmd/tare with SET/RESET payload
        if (payloadUpper == "SET") {
            sensorSetTare();
            String prefix = String(config.mqtt_prefix);
            mqtt.publish((prefix + "/tare_active").c_str(), "ON", true);
        } else if (payloadUpper == "RESET") {
            sensorResetTare();
            String prefix = String(config.mqtt_prefix);
            mqtt.publish((prefix + "/tare_active").c_str(), "OFF", true);
        }
    } else if (topicStr == cmdPrefix + "calibration") {
        // Legacy: cmd/calibration with RESET payload
        if (payloadUpper == "RESET") {
            sensorResetCalibration();
        }
    } else if (topicStr == cmdPrefix + "mount/set") {
        // Einbaulage aendern
        String mountLower = payloadStr;
        mountLower.toLowerCase();
        if (mountLower == "flach usb vorne") config.mount = MOUNT_USB_FRONT;
        else if (mountLower == "flach usb hinten") config.mount = MOUNT_USB_BACK;
        else if (mountLower == "flach usb links") config.mount = MOUNT_USB_LEFT;
        else if (mountLower == "flach usb rechts") config.mount = MOUNT_USB_RIGHT;
        else if (mountLower == "wand vorne") config.mount = MOUNT_WALL_FRONT;
        else if (mountLower == "wand hinten") config.mount = MOUNT_WALL_BACK;
        else if (mountLower == "wand links") config.mount = MOUNT_WALL_LEFT;
        else if (mountLower == "wand rechts") config.mount = MOUNT_WALL_RIGHT;
        else {
            Serial.printf("[MQTT] Unknown mount: %s\n", payloadStr.c_str());
            return;
        }
        saveConfig();
        const char* mountNames[] = {"Flach USB vorne", "Flach USB hinten", "Flach USB links", "Flach USB rechts", "Wand vorne", "Wand hinten", "Wand links", "Wand rechts"};
        String prefix = String(config.mqtt_prefix);
        mqtt.publish((prefix + "/mount").c_str(), mountNames[config.mount], true);
        Serial.printf("[MQTT] Mount set to: %s\n", mountNames[config.mount]);
    } else if (topicStr == cmdPrefix + "tolerance/set") {
        // Toleranz aendern
        float val = payloadStr.toFloat();
        if (val >= 0.0f && val <= 1.0f) {
            config.tolerance = val;
            saveConfig();
            char buf[8];
            dtostrf(config.tolerance, 1, 1, buf);
            String prefix = String(config.mqtt_prefix);
            mqtt.publish((prefix + "/tolerance").c_str(), buf, true);
            Serial.printf("[MQTT] Tolerance set to: %.1f\n", config.tolerance);
        }
    } else if (topicStr == cmdPrefix + "vehicle_type/set") {
        // Fahrzeugtyp aendern
        String vtLower = payloadStr;
        vtLower.toLowerCase();
        VehicleType newType = config.vehicle_type;
        if (vtLower == "wohnwagen") newType = VEHICLE_CARAVAN;
        else if (vtLower == "wohnmobil") newType = VEHICLE_MOTORHOME;
        else {
            Serial.printf("[MQTT] Unknown vehicle type: %s\n", payloadStr.c_str());
            return;
        }
        
        if (newType != config.vehicle_type) {
            // IMPORTANT: Remove old wedge entities from HA BEFORE switching type
            removeOldWedgeEntities(newType);
            
            config.vehicle_type = newType;
            saveConfig();
            
            // Publish new vehicle type state
            const char* vtNames[] = {"Wohnwagen", "Wohnmobil"};
            String prefix = String(config.mqtt_prefix);
            mqtt.publish((prefix + "/vehicle_type").c_str(), vtNames[config.vehicle_type], true);
            
            // Re-publish discovery with new wedge entities
            mqttPublishDiscovery();
            discoveryPublished = true;
            
            Serial.printf("[MQTT] Vehicle type switched to: %s\n", vtNames[config.vehicle_type]);
        }
    } else if (topicStr == cmdPrefix + "filter/set") {
        // Filter ein/ausschalten
        String fLower = payloadStr;
        fLower.toLowerCase();
        if (fLower == "toggle") {
            config.filter_active = !config.filter_active;
        } else if (fLower == "on" || fLower == "true" || fLower == "1") {
            config.filter_active = true;
        } else {
            config.filter_active = false;
        }
        saveConfig();
        String prefix = String(config.mqtt_prefix);
        mqtt.publish((prefix + "/filter_active").c_str(), config.filter_active ? "ON" : "OFF", true);
        Serial.printf("[MQTT] Filter: %s\n", config.filter_active ? "ON" : "OFF");
    } else if (topicStr == cmdPrefix + "mqtt_speed/set") {
        // MQTT-Geschwindigkeit umschalten
        String sLower = payloadStr;
        sLower.toLowerCase();
        if (sLower == "toggle") {
            config.mqtt_fast = !config.mqtt_fast;
        } else if (sLower == "fast" || sLower == "on" || sLower == "5hz") {
            config.mqtt_fast = true;
        } else {
            config.mqtt_fast = false;
        }
        saveConfig();
        String prefix = String(config.mqtt_prefix);
        mqtt.publish((prefix + "/mqtt_fast").c_str(), config.mqtt_fast ? "ON" : "OFF", true);
        Serial.printf("[MQTT] Speed: %s\n", config.mqtt_fast ? "5 Hz" : "0.5 Hz");
    } else if (topicStr == cmdPrefix + "track_width/set") {
        // Spurweite aendern
        float val = payloadStr.toFloat();
        if (val >= 100.0f && val <= 300.0f) {
            config.track_width = val;
            saveConfig();
            char buf[8];
            dtostrf(config.track_width, 1, 0, buf);
            String prefix = String(config.mqtt_prefix);
            mqtt.publish((prefix + "/track_width").c_str(), buf, true);
            Serial.printf("[MQTT] Track width set to: %.0f cm\n", config.track_width);
        }
    } else if (topicStr == cmdPrefix + "wheelbase/set") {
        // Radstand aendern
        float val = payloadStr.toFloat();
        if (val >= 200.0f && val <= 800.0f) {
            config.wheelbase = val;
            saveConfig();
            char buf[8];
            dtostrf(config.wheelbase, 1, 0, buf);
            String prefix = String(config.mqtt_prefix);
            mqtt.publish((prefix + "/wheelbase").c_str(), buf, true);
            Serial.printf("[MQTT] Wheelbase set to: %.0f cm\n", config.wheelbase);
        }
    } else if (topicStr == cmdPrefix + "axle_to_jockey/set") {
        // Achse zu Stuetzrad aendern
        float val = payloadStr.toFloat();
        if (val >= 100.0f && val <= 600.0f) {
            config.axle_to_jockey = val;
            saveConfig();
            char buf[8];
            dtostrf(config.axle_to_jockey, 1, 0, buf);
            String prefix = String(config.mqtt_prefix);
            mqtt.publish((prefix + "/axle_to_jockey").c_str(), buf, true);
            Serial.printf("[MQTT] Axle to jockey set to: %.0f cm\n", config.axle_to_jockey);
        }
    } else if (topicStr == cmdPrefix + "tare_pitch/set") {
        // Tare Pitch direkt setzen
        float val = payloadStr.toFloat();
        if (val >= -10.0f && val <= 10.0f) {
            config.tare_pitch = val;
            config.tare_active = true;
            saveConfig();
            String prefix = String(config.mqtt_prefix);
            char buf[8];
            dtostrf(val, 1, 2, buf);
            mqtt.publish((prefix + "/tare_pitch").c_str(), buf, true);
            mqtt.publish((prefix + "/tare_active").c_str(), "ON", true);
            Serial.printf("[MQTT] Tare pitch set to: %.2f\n", val);
        }
    } else if (topicStr == cmdPrefix + "tare_roll/set") {
        // Tare Roll direkt setzen
        float val = payloadStr.toFloat();
        if (val >= -10.0f && val <= 10.0f) {
            config.tare_roll = val;
            config.tare_active = true;
            saveConfig();
            String prefix = String(config.mqtt_prefix);
            char buf[8];
            dtostrf(val, 1, 2, buf);
            mqtt.publish((prefix + "/tare_roll").c_str(), buf, true);
            mqtt.publish((prefix + "/tare_active").c_str(), "ON", true);
            Serial.printf("[MQTT] Tare roll set to: %.2f\n", val);
        }
    }
}

// ============================================================
// Remove old wedge discovery entities when switching vehicle type
// ============================================================
static void removeOldWedgeEntities(VehicleType newType) {
    if (newType == VEHICLE_MOTORHOME) {
        // Switching TO motorhome → remove caravan entities
        removeDiscoveryEntity("sensor", "wedge_left");
        removeDiscoveryEntity("sensor", "wedge_right");
        removeDiscoveryEntity("sensor", "jockey_wheel");
        Serial.println("[MQTT] Removed caravan wedge entities");
    } else {
        // Switching TO caravan → remove motorhome entities
        removeDiscoveryEntity("sensor", "wedge_fl");
        removeDiscoveryEntity("sensor", "wedge_fr");
        removeDiscoveryEntity("sensor", "wedge_rl");
        removeDiscoveryEntity("sensor", "wedge_rr");
        Serial.println("[MQTT] Removed motorhome wedge entities");
    }
}

// Publish empty payload to HA discovery config topic → removes entity
static void removeDiscoveryEntity(const char* component, const char* id) {
    String configTopic = String("homeassistant/") + component + "/wowa_level/" + id + "/config";
    mqtt.publish(configTopic.c_str(), "", true);  // empty retained = delete
}

// ============================================================
// State publishing
// ============================================================
void mqttPublishState() {
    if (!mqtt.connected()) return;
    
    String prefix = String(config.mqtt_prefix);
    char buf[16];
    
    dtostrf(sensorData.pitch, 1, 1, buf);
    mqtt.publish((prefix + "/pitch").c_str(), buf, true);
    
    dtostrf(sensorData.roll, 1, 1, buf);
    mqtt.publish((prefix + "/roll").c_str(), buf, true);
    
    dtostrf(sensorData.temperature, 1, 1, buf);
    mqtt.publish((prefix + "/temperature").c_str(), buf, true);
    
    if (config.vehicle_type == VEHICLE_CARAVAN) {
        dtostrf(sensorData.wedge_left, 1, 1, buf);
        mqtt.publish((prefix + "/wedge_left").c_str(), buf, true);
        dtostrf(sensorData.wedge_right, 1, 1, buf);
        mqtt.publish((prefix + "/wedge_right").c_str(), buf, true);
        dtostrf(sensorData.jockey_wheel, 1, 1, buf);
        mqtt.publish((prefix + "/jockey_wheel").c_str(), buf, true);
    } else {
        dtostrf(sensorData.wedge_fl, 1, 1, buf);
        mqtt.publish((prefix + "/wedge_fl").c_str(), buf, true);
        dtostrf(sensorData.wedge_fr, 1, 1, buf);
        mqtt.publish((prefix + "/wedge_fr").c_str(), buf, true);
        dtostrf(sensorData.wedge_rl, 1, 1, buf);
        mqtt.publish((prefix + "/wedge_rl").c_str(), buf, true);
        dtostrf(sensorData.wedge_rr, 1, 1, buf);
        mqtt.publish((prefix + "/wedge_rr").c_str(), buf, true);
    }
    
    mqtt.loop();  // Keepalive zwischen den Publishes bedienen
    yield();
    
    itoa(sensorData.cal_status, buf, 10);
    mqtt.publish((prefix + "/calibration").c_str(), buf, true);
    
    itoa(sensorData.rssi, buf, 10);
    mqtt.publish((prefix + "/rssi").c_str(), buf, true);
    
    const char* mountNames[] = {"Flach USB vorne", "Flach USB hinten", "Flach USB links", "Flach USB rechts", "Wand vorne", "Wand hinten", "Wand links", "Wand rechts"};
    mqtt.publish((prefix + "/mount").c_str(), mountNames[config.mount], true);
    
    mqtt.publish((prefix + "/tare_active").c_str(), config.tare_active ? "ON" : "OFF", true);
    
    yield();  // give WiFi time to transmit
    
    // Level-Status (innerhalb Toleranz = ON)
    mqtt.publish((prefix + "/is_level").c_str(), sensorData.is_level ? "ON" : "OFF", true);
    
    // Tolerance
    char tolBuf[8];
    dtostrf(config.tolerance, 1, 1, tolBuf);
    mqtt.publish((prefix + "/tolerance").c_str(), tolBuf, true);
    
    // Vehicle type
    const char* vtNames[] = {"Wohnwagen", "Wohnmobil"};
    mqtt.publish((prefix + "/vehicle_type").c_str(), vtNames[config.vehicle_type], true);
    
    // Filter status
    mqtt.publish((prefix + "/filter_active").c_str(), config.filter_active ? "ON" : "OFF", true);
    
    // MQTT speed
    mqtt.publish((prefix + "/mqtt_fast").c_str(), config.mqtt_fast ? "ON" : "OFF", true);
    
    // Fahrzeugmasse (fuer HA Number Entities)
    dtostrf(config.track_width, 1, 0, buf);
    mqtt.publish((prefix + "/track_width").c_str(), buf, true);
    
    dtostrf(config.wheelbase, 1, 0, buf);
    mqtt.publish((prefix + "/wheelbase").c_str(), buf, true);
    
    dtostrf(config.axle_to_jockey, 1, 0, buf);
    mqtt.publish((prefix + "/axle_to_jockey").c_str(), buf, true);
    
    // Tare-Werte (fuer Anzeige in HA)
    dtostrf(config.tare_pitch, 1, 2, buf);
    mqtt.publish((prefix + "/tare_pitch").c_str(), buf, true);
    
    dtostrf(config.tare_roll, 1, 2, buf);
    mqtt.publish((prefix + "/tare_roll").c_str(), buf, true);
    
    mqtt.publish((prefix + "/status").c_str(), "online", true);
}

// ============================================================
// HA Discovery
// ============================================================
void mqttPublishDiscovery() {
    Serial.println("[MQTT] Publishing HA Discovery...");
    
    // --- Sensors ---
    publishSensor("Pitch", "pitch", "\u00b0", "None", "mdi:angle-acute", "{{ value }}");
    publishSensor("Roll", "roll", "\u00b0", "None", "mdi:angle-acute", "{{ value }}");
    publishSensor("Temperatur", "temperature", "\u00b0C", "temperature", "mdi:thermometer", "{{ value }}");
    
    if (config.vehicle_type == VEHICLE_CARAVAN) {
        publishSensor("Keil Links", "wedge_left", "cm", "distance", "mdi:triangle", "{{ value }}");
        publishSensor("Keil Rechts", "wedge_right", "cm", "distance", "mdi:triangle", "{{ value }}");
        publishSensor("Stuetzrad", "jockey_wheel", "cm", "distance", "mdi:arrow-up-down", "{{ value }}");
    } else {
        publishSensor("Keil VL", "wedge_fl", "cm", "distance", "mdi:triangle", "{{ value }}");
        publishSensor("Keil VR", "wedge_fr", "cm", "distance", "mdi:triangle", "{{ value }}");
        publishSensor("Keil HL", "wedge_rl", "cm", "distance", "mdi:triangle", "{{ value }}");
        publishSensor("Keil HR", "wedge_rr", "cm", "distance", "mdi:triangle", "{{ value }}");
    }
    
    publishSensor("Kalibrierung", "calibration", "", "None", "mdi:crosshairs-gps", "{{ value }}");
    publishSensor("RSSI", "rssi", "dBm", "signal_strength", "mdi:wifi", "{{ value }}");
    publishBinarySensor("Tare Aktiv", "tare_active", "mdi:target");
    publishBinarySensor("Level Status", "is_level", "mdi:spirit-level");
    publishSensor("Toleranz", "tolerance", "\u00b0", "None", "mdi:arrow-expand-horizontal", "{{ value }}");
    publishSensor("Status", "status", "", "None", "mdi:check-circle", "{{ value }}");
    
    // --- Selects ---
    publishSelect("Einbaulage", "mount", "mdi:rotate-3d-variant",
        "Flach USB vorne,Flach USB hinten,Flach USB links,Flach USB rechts,Wand vorne,Wand hinten,Wand links,Wand rechts");
    publishSelect("Fahrzeugtyp", "vehicle_type", "mdi:caravan",
        "Wohnwagen,Wohnmobil");
    
    // --- Number ---
    publishNumber("Toleranz Einstellung", "tolerance", "mdi:arrow-expand-horizontal",
        0.0f, 1.0f, 0.1f, "\u00b0");
    
    // --- Fahrzeugmasse (Number Entities fuer HA) ---
    publishNumber("Spurweite", "track_width", "mdi:arrow-left-right",
        100.0f, 300.0f, 1.0f, "cm");
    publishNumber("Radstand", "wheelbase", "mdi:arrow-up-down",
        200.0f, 800.0f, 1.0f, "cm");
    publishNumber("Achse Stuetzrad", "axle_to_jockey", "mdi:arrow-up-down",
        100.0f, 600.0f, 1.0f, "cm");

    // --- Tare-Werte direkt editierbar ---
    publishNumber("Tare Pitch", "tare_pitch", "mdi:target",
        -10.0f, 10.0f, 0.05f, "\u00b0");
    publishNumber("Tare Roll", "tare_roll", "mdi:target",
        -10.0f, 10.0f, 0.05f, "\u00b0");
    
    // --- Filter Switch ---
    publishBinarySensor("Filter Aktiv", "filter_active", "mdi:blur");
    
    // --- MQTT Speed ---
    publishBinarySensor("MQTT Schnell", "mqtt_fast", "mdi:speedometer");
    
    // --- Buttons ---
    String cmdBase = String(config.mqtt_prefix) + "/cmd/";
    publishButton("Tare setzen", "tare", "mdi:target",
        (cmdBase + "tare/set").c_str(), "PRESS");
    publishButton("Tare zuruecksetzen", "tare_reset", "mdi:target-off",
        (cmdBase + "tare_reset/set").c_str(), "PRESS");
    publishButton("Kalibrierung zuruecksetzen", "calibration_reset", "mdi:crosshairs-question",
        (cmdBase + "calibration_reset/set").c_str(), "PRESS");
    // Switches
    String cmdBase2 = String(config.mqtt_prefix) + "/cmd/";
    publishSwitch("Glaettungsfilter", "filter_active", "mdi:blur",
        (cmdBase2 + "filter/set").c_str());
    publishSwitch("MQTT Schnell", "mqtt_fast", "mdi:speedometer",
        (cmdBase2 + "mqtt_speed/set").c_str());
    
    Serial.println("[MQTT] Discovery complete");
}

// ============================================================
// Helper: shared device block
// ============================================================
static void addDeviceBlock(JsonDocument& doc) {
    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = "wowa_level_esp32c3";
    device["name"] = "WoWa Neigungssensor";
    device["model"] = "ESP32 WROOM-32 + BNO085";
    device["manufacturer"] = "DIY";
    device["sw_version"] = "1.0.0";
    
    doc["availability_topic"] = String(config.mqtt_prefix) + "/status";
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";
}

// ============================================================
// Discovery publishers
// ============================================================
static void publishSensor(const char* name, const char* id, const char* unit,
                          const char* devClass, const char* icon, const char* valueTpl) {
    JsonDocument doc;
    
    String uniqueId = String("wowa_level_") + id;
    String stateTopic = String(config.mqtt_prefix) + "/" + id;
    String configTopic = String("homeassistant/sensor/wowa_level/") + id + "/config";
    
    doc["name"] = name;
    doc["unique_id"] = uniqueId;
    doc["state_topic"] = stateTopic;
    doc["value_template"] = valueTpl;
    doc["icon"] = icon;
    
    if (strlen(unit) > 0) {
        doc["unit_of_measurement"] = unit;
    }
    
    if (strcmp(devClass, "None") != 0) {
        doc["device_class"] = devClass;
    }
    
    addDeviceBlock(doc);
    
    char buffer[768];
    serializeJson(doc, buffer);
    mqtt.publish(configTopic.c_str(), buffer, true);
    delay(10);
    yield();
}

static void publishSelect(const char* name, const char* id, const char* icon,
                           const char* optionsCSV) {
    JsonDocument doc;
    
    String uniqueId = String("wowa_level_") + id + "_select";
    String stateTopic = String(config.mqtt_prefix) + "/" + id;
    String cmdTopic = String(config.mqtt_prefix) + "/cmd/" + id + "/set";
    String configTopic = String("homeassistant/select/wowa_level/") + id + "/config";
    
    doc["name"] = name;
    doc["unique_id"] = uniqueId;
    doc["state_topic"] = stateTopic;
    doc["command_topic"] = cmdTopic;
    doc["icon"] = icon;
    
    // Parse CSV options into array
    JsonArray opts = doc["options"].to<JsonArray>();
    String csv = String(optionsCSV);
    int start = 0;
    for (int i = 0; i <= (int)csv.length(); i++) {
        if (i == (int)csv.length() || csv[i] == ',') {
            opts.add(csv.substring(start, i));
            start = i + 1;
        }
    }
    
    addDeviceBlock(doc);
    
    char buffer[768];
    serializeJson(doc, buffer);
    mqtt.publish(configTopic.c_str(), buffer, true);
    delay(10);
    yield();
}

static void publishNumber(const char* name, const char* id, const char* icon,
                           float minVal, float maxVal, float step, const char* unit) {
    JsonDocument doc;
    
    String uniqueId = String("wowa_level_") + id + "_number";
    String stateTopic = String(config.mqtt_prefix) + "/" + id;
    String cmdTopic = String(config.mqtt_prefix) + "/cmd/" + id + "/set";
    String configTopic = String("homeassistant/number/wowa_level/") + id + "/config";
    
    doc["name"] = name;
    doc["unique_id"] = uniqueId;
    doc["state_topic"] = stateTopic;
    doc["command_topic"] = cmdTopic;
    doc["icon"] = icon;
    doc["min"] = minVal;
    doc["max"] = maxVal;
    doc["step"] = step;
    if (strlen(unit) > 0) doc["unit_of_measurement"] = unit;
    doc["mode"] = "box";
    
    addDeviceBlock(doc);
    
    char buffer[768];
    serializeJson(doc, buffer);
    mqtt.publish(configTopic.c_str(), buffer, true);
    delay(10);
    yield();
}

// NEW: HA Button entity (press-only, no state)
static void publishButton(const char* name, const char* id, const char* icon,
                           const char* cmdTopic, const char* payload) {
    JsonDocument doc;
    
    String uniqueId = String("wowa_level_") + id + "_button";
    String configTopic = String("homeassistant/button/wowa_level/") + id + "/config";
    
    doc["name"] = name;
    doc["unique_id"] = uniqueId;
    doc["command_topic"] = cmdTopic;
    doc["payload_press"] = payload;
    doc["icon"] = icon;
    
    addDeviceBlock(doc);
    
    char buffer[512];
    serializeJson(doc, buffer);
    mqtt.publish(configTopic.c_str(), buffer, true);
    delay(10);
    yield();
}

// Binary Sensor (read-only ON/OFF entity)
static void publishBinarySensor(const char* name, const char* id, const char* icon) {
    JsonDocument doc;
    
    String uniqueId = String("wowa_level_") + id + "_binary";
    String stateTopic = String(config.mqtt_prefix) + "/" + id;
    String configTopic = String("homeassistant/binary_sensor/wowa_level/") + id + "/config";
    
    doc["name"] = name;
    doc["unique_id"] = uniqueId;
    doc["state_topic"] = stateTopic;
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["icon"] = icon;
    
    addDeviceBlock(doc);
    
    char buffer[512];
    serializeJson(doc, buffer);
    mqtt.publish(configTopic.c_str(), buffer, true);
    delay(10);
    yield();
}

// Switch (toggleable ON/OFF entity with command topic)
static void publishSwitch(const char* name, const char* id, const char* icon,
                           const char* cmdTopic) {
    JsonDocument doc;
    
    String uniqueId = String("wowa_level_") + id + "_switch";
    String stateTopic = String(config.mqtt_prefix) + "/" + id;
    String configTopic = String("homeassistant/switch/wowa_level/") + id + "/config";
    
    doc["name"] = name;
    doc["unique_id"] = uniqueId;
    doc["state_topic"] = stateTopic;
    doc["command_topic"] = cmdTopic;
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["icon"] = icon;
    
    addDeviceBlock(doc);
    
    char buffer[512];
    serializeJson(doc, buffer);
    mqtt.publish(configTopic.c_str(), buffer, true);
    delay(10);
    yield();
}

