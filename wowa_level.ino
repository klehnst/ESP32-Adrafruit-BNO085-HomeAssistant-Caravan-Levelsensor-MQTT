/*
 * WoWa-Level - Wohnwagen Neigungssensor
 * ======================================
 * Hardware: ESP32-C3 Super Mini + Adafruit BNO085 (I2C)
 * 
 * Pin Belegung:
 *   GPIO4  -> SDA (I2C)
 *   GPIO10 -> SCL (I2C)
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "config.h"
#include "wifi_manager.h"
#include "sensor.h"
#include "mqtt_ha.h"
#include "webserver.h"

AppConfig config;
SensorData sensorData;

static unsigned long lastStatusPrint = 0;
static bool sensorStarted = false;

void setupOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword("wowa2024");
    
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("[OTA] Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Update complete!");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]\n", error);
    });
    
    ArduinoOTA.begin();
    Serial.println("[OTA] Ready");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println();
    Serial.println("========================================");
    Serial.println("  WoWa-Level v1.0 (ESP32-C3 + BNO085)");
    Serial.println("========================================");
    Serial.println();
    
    memset(&sensorData, 0, sizeof(SensorData));
    
    // Phase 1: Config
    webserverInit();
    
    // Phase 2: WiFi (blockierend)
    wifiManagerInit();
    
    // Phase 3: Webserver
    webserverStart();
    
    // Phase 4: OTA + MQTT
    setupOTA();
    mqttInit();
    
    // Phase 5: Sensor wird NICHT hier gestartet!
    // Startet erst im loop() nach 3 Sekunden stabilem Betrieb
    Serial.println("[MAIN] Sensor init deferred to loop()");
    
    Serial.printf("[MAIN] Setup done! Heap: %d\n", ESP.getFreeHeap());
}

void loop() {
    wifiManagerLoop();
    ArduinoOTA.handle();
    
    // Deferred sensor init — erst nach 3s im Loop
    if (!sensorStarted && millis() > 5000) {
        sensorStarted = true;
        Serial.println("[MAIN] Deferred sensor init starting...");
        if (!sensorInit()) {
            Serial.println("[MAIN] WARNING: Sensor init failed");
        } else {
            Serial.println("[MAIN] Sensor OK!");
        }
    }
    
    if (sensorStarted) {
        sensorLoop();
    }
    
    mqttLoop();
    webserverLoop();
    
    // Serial commands
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "cal") {
            Serial.println("[CMD] Calibration reset...");
            sensorResetCalibration();
        } else if (cmd == "tare") {
            Serial.println("[CMD] Tare set");
            sensorSetTare();
        } else if (cmd == "notare") {
            Serial.println("[CMD] Tare reset");
            sensorResetTare();
        } else if (cmd == "grav") {
            Serial.printf("[GRAV] gx=%.3f gy=%.3f gz=%.3f\n",
                sensorData.gx, sensorData.gy, sensorData.gz);
            Serial.printf("[GRAV] Mount=%d → P=%.2f R=%.2f\n",
                (int)config.mount, sensorData.pitch, sensorData.roll);
        } else if (cmd == "filter") {
            config.filter_active = !config.filter_active;
            Serial.printf("[CMD] Filter: %s\n", config.filter_active ? "ON" : "OFF");
        } else if (cmd == "fast") {
            config.mqtt_fast = !config.mqtt_fast;
            Serial.printf("[CMD] MQTT: %s\n", config.mqtt_fast ? "5 Hz" : "0.5 Hz");
        } else if (cmd == "help") {
            Serial.println("[CMD] Commands: cal, tare, notare, grav, filter, fast, help");
        }
    }
    
    if (millis() - lastStatusPrint > 10000) {
        lastStatusPrint = millis();
        Serial.printf("[STATUS] P:%.2f R:%.2f Cal:%d WiFi:%s MQTT:%s Heap:%d\n",
            sensorData.pitch, sensorData.roll, sensorData.cal_status,
            getWiFiStateString().c_str(),
            mqttIsConnected() ? "OK" : "---",
            ESP.getFreeHeap());
    }
    
    delay(5);
    yield();
}
