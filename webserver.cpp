#include "webserver.h"
#include "wifi_manager.h"
#include "sensor.h"
#include "mqtt_ha.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static unsigned long lastWsUpdate = 0;

// Forward declarations
static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len);
static void handleApiGetConfig(AsyncWebServerRequest* request);
static void handleApiSaveConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
static void handleApiTare(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
static void handleApiCalibration(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
static void handleApiRestart(AsyncWebServerRequest* request);

// Load config from file
void loadConfig();
// Save config to file
void saveConfig();

void webserverInit() {
    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[Web] LittleFS mount failed!");
        return;
    }
    Serial.println("[Web] LittleFS mounted");
    
    // Load configuration
    loadConfig();
    Serial.println("[Web] step: config loaded, setting up WS");
    
    // WebSocket
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    Serial.println("[Web] step: WS handler added");
    
    // Serve static files from LittleFS (only if index.html exists)
    if (LittleFS.exists("/index.html")) {
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
        Serial.println("[Web] step: static route added");
    } else {
        Serial.println("[Web] WARNING: /index.html not found on LittleFS - Web-UI not available!");
        Serial.println("[Web] -> Bitte data/ Ordner via LittleFS Upload hochladen");
        // Fallback page so the AP still shows something
        server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
            request->send(200, "text/html",
                "<html><body style='font-family:sans-serif;background:#0f1419;color:#e8f0f8;padding:20px'>"
                "<h1>WoWa-Level</h1><p>Web-UI (index.html) wurde noch nicht auf LittleFS hochgeladen.</p>"
                "<p>Bitte data/ Ordner via LittleFS Upload hochladen.</p></body></html>");
        });
    }
    
    // API endpoints
    server.on("/api/config", HTTP_GET, handleApiGetConfig);
    server.on("/api/config", HTTP_POST, 
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiSaveConfig
    );
    server.on("/api/tare", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiTare
    );
    server.on("/api/calibration", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiCalibration
    );
    server.on("/api/restart", HTTP_POST, handleApiRestart);
    server.on("/api/filter", HTTP_GET,
        [](AsyncWebServerRequest* request) {
            config.filter_active = !config.filter_active;
            saveConfig();
            String msg = config.filter_active ? "ON" : "OFF";
            Serial.printf("[Web] Filter toggled: %s\n", msg.c_str());
            request->send(200, "application/json", "{\"filter\":\"" + msg + "\"}");
        }
    );
    Serial.println("[Web] step: API routes added");
    Serial.println("[Web] Init done (server not started yet - waiting for WiFi)");
}

void webserverStart() {
    // Called AFTER WiFi is up - AsyncTCP needs the network stack ready
    server.begin();
    Serial.println("[Web] Server started on port 80");
}

void webserverLoop() {
    ws.cleanupClients(2);  // Max 2 simultaneous WS clients — kills oldest if exceeded

    // Send WebSocket updates
    if (millis() - lastWsUpdate > WS_UPDATE_MS) {
        lastWsUpdate = millis();
        if (ws.count() > 0) {
            wsSendSensorData();
        }
    }
}

void wsSendSensorData() {
    JsonDocument doc;
    
    doc["pitch"] = round(sensorData.pitch * 100.0f) / 100.0f;
    doc["roll"] = round(sensorData.roll * 100.0f) / 100.0f;
    doc["temp"] = round(sensorData.temperature * 10.0f) / 10.0f;
    doc["wedge_l"] = round(sensorData.wedge_left * 10.0f) / 10.0f;
    doc["wedge_r"] = round(sensorData.wedge_right * 10.0f) / 10.0f;
    doc["wedge_fl"] = round(sensorData.wedge_fl * 10.0f) / 10.0f;
    doc["wedge_fr"] = round(sensorData.wedge_fr * 10.0f) / 10.0f;
    doc["wedge_rl"] = round(sensorData.wedge_rl * 10.0f) / 10.0f;
    doc["wedge_rr"] = round(sensorData.wedge_rr * 10.0f) / 10.0f;
    doc["vehicle_type"] = (int)config.vehicle_type;
    doc["jockey"] = round(sensorData.jockey_wheel * 10.0f) / 10.0f;
    doc["cal"] = sensorData.cal_status;
    doc["sensor_ok"] = sensorData.sensor_ok;
    doc["is_level"] = sensorData.is_level;
    doc["rssi"] = sensorData.rssi;
    doc["tare"] = config.tare_active;
    doc["filter"] = config.filter_active;
    doc["wifi"] = getWiFiStateString();
    doc["mqtt"] = mqttIsConnected();
    doc["tolerance"] = config.tolerance;
    
    char buffer[384];
    size_t len = serializeJson(doc, buffer);
    ws.textAll(buffer, len);
}

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WS] Client #%u connected\n", client->id());
        // Send initial data
        wsSendSensorData();
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WS] Client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        // Handle incoming WebSocket messages
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            String msg = String((char*)data);
            
            if (msg == "TARE_SET") {
                sensorSetTare();
                saveConfig();
            } else if (msg == "TARE_RESET") {
                sensorResetTare();
                saveConfig();
            } else if (msg == "CAL_RESET") {
                sensorResetCalibration();
            }
        }
    }
}

static void handleApiGetConfig(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    doc["wifi_ssid"] = config.wifi_ssid;
    doc["wifi_pass"] = config.wifi_pass;
    doc["ap_ssid"] = config.ap_ssid;
    doc["ap_pass"] = config.ap_pass;
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = config.mqtt_user;
    doc["mqtt_pass"] = config.mqtt_pass;
    doc["mqtt_prefix"] = config.mqtt_prefix;
    doc["mount"] = (int)config.mount;
    doc["track_width"] = config.track_width;
    doc["vehicle_type"] = (int)config.vehicle_type;
    doc["axle_to_jockey"] = config.axle_to_jockey;
    doc["wheelbase"] = config.wheelbase;
    doc["tolerance"] = config.tolerance;
    doc["tare_pitch"] = config.tare_pitch;
    doc["tare_roll"] = config.tare_roll;
    doc["tare_active"] = config.tare_active;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

static void handleApiSaveConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    static String body;
    if (index == 0) body = "";
    body += String((char*)data, len);
    
    if (index + len == total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        // Update config
        if (doc.containsKey("wifi_ssid")) strlcpy(config.wifi_ssid, doc["wifi_ssid"] | "", sizeof(config.wifi_ssid));
        if (doc.containsKey("wifi_pass")) strlcpy(config.wifi_pass, doc["wifi_pass"] | "", sizeof(config.wifi_pass));
        if (doc.containsKey("ap_ssid")) strlcpy(config.ap_ssid, doc["ap_ssid"] | DEFAULT_AP_SSID, sizeof(config.ap_ssid));
        if (doc.containsKey("ap_pass")) strlcpy(config.ap_pass, doc["ap_pass"] | DEFAULT_AP_PASSWORD, sizeof(config.ap_pass));
        if (doc.containsKey("mqtt_server")) strlcpy(config.mqtt_server, doc["mqtt_server"] | "", sizeof(config.mqtt_server));
        if (doc.containsKey("mqtt_port")) config.mqtt_port = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
        if (doc.containsKey("mqtt_user")) strlcpy(config.mqtt_user, doc["mqtt_user"] | "", sizeof(config.mqtt_user));
        if (doc.containsKey("mqtt_pass")) strlcpy(config.mqtt_pass, doc["mqtt_pass"] | "", sizeof(config.mqtt_pass));
        if (doc.containsKey("mqtt_prefix")) strlcpy(config.mqtt_prefix, doc["mqtt_prefix"] | DEFAULT_MQTT_PREFIX, sizeof(config.mqtt_prefix));
        if (doc.containsKey("mount")) config.mount = (MountOrientation)(doc["mount"].as<int>());
        if (doc.containsKey("track_width")) config.track_width = doc["track_width"];
        if(doc.containsKey("vehicle_type")) config.vehicle_type = (VehicleType)(int)doc["vehicle_type"];
        if(doc.containsKey("axle_to_jockey")) config.axle_to_jockey = doc["axle_to_jockey"] | DEFAULT_AXLE_JOCKEY;
        if (doc.containsKey("wheelbase")) config.wheelbase = doc["wheelbase"] | DEFAULT_WHEELBASE;
        if (doc.containsKey("tolerance")) config.tolerance = doc["tolerance"] | DEFAULT_TOLERANCE;
        
        // Tare/Filter/MQTT-Status nur uebernehmen wenn explizit gesendet
        if (doc.containsKey("tare_pitch")) config.tare_pitch = doc["tare_pitch"];
        if (doc.containsKey("tare_roll")) config.tare_roll = doc["tare_roll"];
        if (doc.containsKey("tare_active")) config.tare_active = doc["tare_active"];
        if (doc.containsKey("filter_active")) config.filter_active = doc["filter_active"];
        if (doc.containsKey("mqtt_fast")) config.mqtt_fast = doc["mqtt_fast"];
        
        saveConfig();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
}

static void handleApiTare(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    static String body;
    if (index == 0) body = "";
    body += String((char*)data, len);
    
    if (index + len == total) {
        JsonDocument doc;
        deserializeJson(doc, body);
        
        String action = doc["action"] | "SET";
        if (action == "SET") {
            sensorSetTare();   // ruft intern saveConfig() auf
        } else if (action == "RESET") {
            sensorResetTare(); // ruft intern saveConfig() auf
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
}

static void handleApiCalibration(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    static String body;
    if (index == 0) body = "";
    body += String((char*)data, len);
    
    if (index + len == total) {
        sensorResetCalibration();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
}

static void handleApiRestart(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
}

// ============ Config Load/Save ============

void loadConfig() {
    // Set defaults first
    strlcpy(config.ap_ssid, DEFAULT_AP_SSID, sizeof(config.ap_ssid));
    strlcpy(config.ap_pass, DEFAULT_AP_PASSWORD, sizeof(config.ap_pass));
    memset(config.wifi_ssid, 0, sizeof(config.wifi_ssid));
    memset(config.wifi_pass, 0, sizeof(config.wifi_pass));
    strlcpy(config.mqtt_server, DEFAULT_MQTT_SERVER, sizeof(config.mqtt_server));
    config.mqtt_port = DEFAULT_MQTT_PORT;
    memset(config.mqtt_user, 0, sizeof(config.mqtt_user));
    memset(config.mqtt_pass, 0, sizeof(config.mqtt_pass));
    strlcpy(config.mqtt_prefix, DEFAULT_MQTT_PREFIX, sizeof(config.mqtt_prefix));
    config.mount = MOUNT_USB_FRONT;
    config.vehicle_type = VEHICLE_CARAVAN;
    config.track_width = DEFAULT_TRACK_WIDTH;
    config.wheelbase = DEFAULT_WHEELBASE;
    config.axle_to_jockey = DEFAULT_AXLE_JOCKEY;
    config.tolerance = DEFAULT_TOLERANCE;
    config.tare_pitch = 0.0f;
    config.tare_roll = 0.0f;
    config.tare_active = false;
    config.filter_active = false;
    config.mqtt_fast = false;   // Default: 0.5 Hz (Dauerbetrieb)
    
    // Try to load from file
    if (!LittleFS.exists(CONFIG_FILE)) {
        Serial.println("[Config] No config file - using defaults");
        saveConfig();
        return;
    }
    
    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        Serial.println("[Config] Failed to open config file");
        return;
    }
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    
    if (err) {
        Serial.printf("[Config] JSON parse error: %s\n", err.c_str());
        return;
    }
    
    strlcpy(config.wifi_ssid, doc["wifi_ssid"] | "", sizeof(config.wifi_ssid));
    strlcpy(config.wifi_pass, doc["wifi_pass"] | "", sizeof(config.wifi_pass));
    strlcpy(config.ap_ssid, doc["ap_ssid"] | DEFAULT_AP_SSID, sizeof(config.ap_ssid));
    strlcpy(config.ap_pass, doc["ap_pass"] | DEFAULT_AP_PASSWORD, sizeof(config.ap_pass));
    strlcpy(config.mqtt_server, doc["mqtt_server"] | DEFAULT_MQTT_SERVER, sizeof(config.mqtt_server));
    config.mqtt_port = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
    strlcpy(config.mqtt_user, doc["mqtt_user"] | "", sizeof(config.mqtt_user));
    strlcpy(config.mqtt_pass, doc["mqtt_pass"] | "", sizeof(config.mqtt_pass));
    strlcpy(config.mqtt_prefix, doc["mqtt_prefix"] | DEFAULT_MQTT_PREFIX, sizeof(config.mqtt_prefix));
    config.mount = (MountOrientation)(doc["mount"].as<int>());
    config.track_width = doc["track_width"];
        if(doc.containsKey("vehicle_type")) config.vehicle_type = (VehicleType)(int)doc["vehicle_type"];
        if(doc.containsKey("axle_to_jockey")) config.axle_to_jockey = doc["axle_to_jockey"] | DEFAULT_TRACK_WIDTH;
    config.wheelbase = doc["wheelbase"] | DEFAULT_WHEELBASE;
    config.tolerance = doc["tolerance"] | DEFAULT_TOLERANCE;
    config.tare_pitch = doc["tare_pitch"] | 0.0f;
    config.tare_roll = doc["tare_roll"] | 0.0f;
    config.tare_active = doc["tare_active"] | false;
    config.filter_active = doc["filter_active"] | false;
    config.mqtt_fast = doc["mqtt_fast"] | false;
    
    Serial.println("[Config] Loaded from file");
}

void saveConfig() {
    JsonDocument doc;
    
    doc["wifi_ssid"] = config.wifi_ssid;
    doc["wifi_pass"] = config.wifi_pass;
    doc["ap_ssid"] = config.ap_ssid;
    doc["ap_pass"] = config.ap_pass;
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = config.mqtt_user;
    doc["mqtt_pass"] = config.mqtt_pass;
    doc["mqtt_prefix"] = config.mqtt_prefix;
    doc["mount"] = (int)config.mount;
    doc["track_width"] = config.track_width;
    doc["vehicle_type"] = (int)config.vehicle_type;
    doc["axle_to_jockey"] = config.axle_to_jockey;
    doc["wheelbase"] = config.wheelbase;
    doc["tolerance"] = config.tolerance;
    doc["tare_pitch"] = config.tare_pitch;
    doc["tare_roll"] = config.tare_roll;
    doc["tare_active"] = config.tare_active;
    doc["filter_active"] = config.filter_active;
    doc["mqtt_fast"] = config.mqtt_fast;
    
    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("[Config] Failed to write config file!");
        return;
    }
    
    serializeJsonPretty(doc, file);
    file.close();
    Serial.println("[Config] Saved to file");
}
