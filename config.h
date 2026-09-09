#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================
// PIN DEFINITIONS - ESP32 WROOM-32 + BNO085 I2C
#define PIN_I2C_SDA   21
#define PIN_I2C_SCL   22
#define BNO08X_I2C_ADDR 0x4A  // Default I2C address
// ============================================================

// ============================================================
// DEFAULT CONFIGURATION VALUES
// ============================================================
#define DEFAULT_AP_SSID       "WoWa-Level"
#define DEFAULT_AP_PASSWORD   "levelsensor"
#define DEFAULT_AP_IP         "192.168.4.1"
#define WIFI_CONNECT_TIMEOUT  60000  // 60 seconds

#define DEFAULT_MQTT_SERVER   "192.168.1.100"
#define DEFAULT_MQTT_PORT     1883
#define DEFAULT_MQTT_USER     ""
#define DEFAULT_MQTT_PASS     ""
#define DEFAULT_MQTT_PREFIX   "wowa/level"

#define DEFAULT_TRACK_WIDTH   180.0f   // cm - Spurweite
#define DEFAULT_WHEELBASE     500.0f   // cm - Radstand (Wohnmobil)
#define DEFAULT_AXLE_JOCKEY   450.0f   // cm - Achse zu Stuetzrad (Wohnwagen)

#define DEFAULT_TOLERANCE     0.5f     // degrees

#define SENSOR_UPDATE_MS      100      // 10 Hz sensor read
#define MQTT_FAST_MS          200      // 5 Hz (Einrichten)
#define MQTT_SLOW_MS          2000     // 0.5 Hz (Dauerbetrieb)
#define WS_UPDATE_MS          200      // 5 Hz WebSocket

#define CONFIG_FILE           "/config.json"
#define OTA_HOSTNAME          "wowa-level"

// ============================================================
// VEHICLE TYPE
// ============================================================
enum VehicleType {
    VEHICLE_CARAVAN   = 0,  // Wohnwagen (1 Achse + Stuetzrad)
    VEHICLE_MOTORHOME = 1   // Wohnmobil (2 Achsen)
};

// ============================================================
// MOUNTING ORIENTATION
// Flach: Sensor liegt horizontal, USB zeigt in angegebene Richtung
// Wand:  Sensor haengt vertikal an der Wand, USB zeigt nach UNTEN
//        "Wand vorne" = an der Vorderwand montiert
// ============================================================
enum MountOrientation {
    // Flachmontage
    MOUNT_USB_FRONT = 0,   // Flach, USB zeigt nach vorne (Fahrtrichtung)
    MOUNT_USB_BACK  = 1,   // Flach, USB zeigt nach hinten
    MOUNT_USB_LEFT  = 2,   // Flach, USB zeigt nach links
    MOUNT_USB_RIGHT = 3,   // Flach, USB zeigt nach rechts
    // Wandmontage (USB zeigt nach unten)
    MOUNT_WALL_FRONT = 4,  // An Vorderwand (Platine zeigt nach hinten)
    MOUNT_WALL_BACK  = 5,  // An Rueckwand (Platine zeigt nach vorne)
    MOUNT_WALL_LEFT  = 6,  // An linker Wand (Platine zeigt nach rechts)
    MOUNT_WALL_RIGHT = 7   // An rechter Wand (Platine zeigt nach links)
};

// ============================================================
// CONFIGURATION STRUCT
// ============================================================
struct AppConfig {
    // WiFi Station
    char wifi_ssid[64];
    char wifi_pass[64];
    
    // WiFi AP
    char ap_ssid[32];
    char ap_pass[32];
    
    // MQTT
    char mqtt_server[64];
    uint16_t mqtt_port;
    char mqtt_user[32];
    char mqtt_pass[32];
    char mqtt_prefix[32];
    
    // Sensor / Vehicle
    VehicleType vehicle_type;
    MountOrientation mount;
    float track_width;     // Spurweite in cm
    float wheelbase;       // Radstand in cm (Wohnmobil)
    float axle_to_jockey;  // Achse zu Stuetzrad (Wohnwagen) in cm
    float tolerance;       // Toleranz in Grad
    
    // Tare offsets
    float tare_pitch;
    float tare_roll;
    bool tare_active;
    bool filter_active;    // Glaettungsfilter ein/aus
    bool mqtt_fast;        // true=5Hz, false=0.5Hz
};

// ============================================================
// SENSOR DATA STRUCT
// ============================================================
struct SensorData {
    float raw_pitch;
    float raw_roll;
    float pitch;           // nach Tare und Einbaulage
    float roll;            // nach Tare und Einbaulage
    float temperature;
    float wedge_left;      // Keilhoehe links in cm
    float wedge_right;     // Keilhoehe rechts in cm
    float jockey_wheel;    // Stuetzrad-Korrektur in cm
    // Wohnmobil (motorhome)
    float wedge_fl;        // Keil vorne links
    float wedge_fr;        // Keil vorne rechts
    float wedge_rl;        // Keil hinten links
    float wedge_rr;        // Keil hinten rechts
    // Raw gravity vector (for debugging)
    float gx, gy, gz;
    uint8_t cal_status;    // Kalibrierungsstatus 0-3
    bool is_level;         // true wenn innerhalb Toleranz
    bool sensor_ok;
    int rssi;
};

// ============================================================
// GLOBAL EXTERNS
// ============================================================
extern AppConfig config;
extern SensorData sensorData;

#endif // CONFIG_H
