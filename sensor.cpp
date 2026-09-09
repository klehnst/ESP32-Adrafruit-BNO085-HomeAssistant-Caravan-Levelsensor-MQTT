#include "sensor.h"
#include <Wire.h>
#include <SparkFun_BNO08x_Arduino_Library.h>
#include "webserver.h"   // fuer saveConfig()

static BNO08x imu;
static unsigned long lastSensorRead = 0;

// ============================================================
// EMA-Glaettungsfilter
// ============================================================
#define EMA_ALPHA 0.10f
static float ema_pitch = 0.0f;
static float ema_roll  = 0.0f;
static bool  ema_initialized = false;

// Totzone
#define DEADBAND 0.1f

// ============================================================
// Gravity-Vektor → Pitch/Roll
// Der BNO085 liefert den Gravity-Report direkt — kein Drift,
// kein Magnetometer noetig, wie ein praeziser Accelerometer
// aber mit Gyro-Stuetzung (kein Rauschen bei Vibrationen).
//
// gx, gy, gz kommen direkt vom BNO085 Gravity-Report.
// Bei flacher Lage: gx≈0, gy≈0, gz≈9.81
// ============================================================
// ============================================================
// Gravity-Vektor umordnen je nach Einbaulage
// Gibt die 3 Gravity-Komponenten so zurueck, dass:
//   vgx = Fahrzeug-laengs  (+ = Nase runter)
//   vgy = Fahrzeug-quer    (+ = links runter)
//   vgz = Fahrzeug-hoch    (+ = Schwerkraft, ≈9.81 wenn eben)
//
// Flach:  Sensor-Z zeigt nach oben      → vgz = gz
// Wand:   Sensor-Y zeigt nach oben (USB unten) → vgz = -gy
// ============================================================
static void remapGravity(float gx, float gy, float gz,
                          float* vgx, float* vgy, float* vgz) {
    switch (config.mount) {
        // --- Flach (Sensor horizontal, Z zeigt hoch) ---
        case MOUNT_USB_FRONT:
            *vgx = gx;  *vgy = gy;   *vgz = gz;  break;
        case MOUNT_USB_BACK:
            *vgx = -gx; *vgy = -gy;  *vgz = gz;  break;
        case MOUNT_USB_LEFT:
            *vgx = gy;  *vgy = -gx;  *vgz = gz;  break;
        case MOUNT_USB_RIGHT:
            *vgx = -gy; *vgy = gx;   *vgz = gz;  break;
        // --- Wand (Sensor vertikal, USB zeigt nach UNTEN) ---
        // Bestaetigt: gx ≈ +9.81 wenn USB nach unten zeigt
        // → gx ist die "Schwerkraft-Achse" → wird zu vgz (Fahrzeug "oben")
        case MOUNT_WALL_FRONT:   // An Vorderwand, Platine zeigt nach hinten
            *vgx = -gz; *vgy = gy;   *vgz = gx;  break;
        case MOUNT_WALL_BACK:    // An Rueckwand, Platine zeigt nach vorne
            *vgx = gz;  *vgy = -gy;  *vgz = gx;  break;
        case MOUNT_WALL_LEFT:    // An linker Wand, Platine zeigt nach rechts
            *vgx = -gy; *vgy = -gz;  *vgz = gx;  break;
        case MOUNT_WALL_RIGHT:   // An rechter Wand, Platine zeigt nach links
            *vgx = gy;  *vgy = gz;   *vgz = gx;  break;
        default:
            *vgx = gx;  *vgy = gy;   *vgz = gz;  break;
    }
}

// Pitch/Roll aus Fahrzeug-Gravity berechnen
static void vehicleGravityToPitchRoll(float vgx, float vgy, float vgz,
                                       float* pitch, float* roll) {
    *pitch = atan2f(vgx, vgz) * RAD_TO_DEG;
    *roll  = atan2f(-vgy, vgz) * RAD_TO_DEG;
}

// ============================================================
// Einbaulage: dreht Pitch/Roll ins Fahrzeug-Koordinatensystem
// ============================================================


// ============================================================
// Keil-Berechnung
// ============================================================
static void calculateWedges() {
    float rollRad = sensorData.roll * DEG_TO_RAD;
    float pitchRad = sensorData.pitch * DEG_TO_RAD;
    
    if (config.vehicle_type == VEHICLE_CARAVAN) {
        float lateralOffset = tanf(rollRad) * config.track_width / 2.0f;
        if (lateralOffset > 0) {
            sensorData.wedge_right = lateralOffset;
            sensorData.wedge_left = 0.0f;
        } else {
            sensorData.wedge_left = -lateralOffset;
            sensorData.wedge_right = 0.0f;
        }
        sensorData.jockey_wheel = tanf(pitchRad) * config.axle_to_jockey;
        sensorData.wedge_fl = sensorData.wedge_fr = 0.0f;
        sensorData.wedge_rl = sensorData.wedge_rr = 0.0f;
    } else {
        float latOff = tanf(rollRad) * config.track_width / 2.0f;
        float lonOff = tanf(pitchRad) * config.wheelbase / 2.0f;
        sensorData.wedge_fl = fmaxf(0.0f, -latOff - lonOff);
        sensorData.wedge_fr = fmaxf(0.0f,  latOff - lonOff);
        sensorData.wedge_rl = fmaxf(0.0f, -latOff + lonOff);
        sensorData.wedge_rr = fmaxf(0.0f,  latOff + lonOff);
        sensorData.wedge_left = sensorData.wedge_right = 0.0f;
        sensorData.jockey_wheel = 0.0f;
    }
}

// ============================================================
// Sensor Init — NUR Gravity-Report (1 Report, kein Drift)
// ============================================================
bool sensorInit() {
    Serial.println("[Sensor] Initializing I2C...");
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Serial.printf("[Sensor] Pins: SDA=%d SCL=%d\n", PIN_I2C_SDA, PIN_I2C_SCL);
    Serial.println("[Sensor] Connecting to BNO085...");
    
    delay(200);
    
    if (!imu.begin(BNO08X_I2C_ADDR, Wire)) {
        Serial.println("[Sensor] BNO085 init FAILED!");
        sensorData.sensor_ok = false;
        return false;
    }
    
    Serial.println("[Sensor] BNO085 found!");
    delay(100);
    
    // Reiner Accelerometer (KEINE Gyro-Fusion → absolut driftfrei)
    // Bei Stillstand misst der Accelerometer nur die Schwerkraft.
    // Wie die CaraTech, aber praeziserer Sensor + On-Chip Temp-Kompensation.
    // Rauschen bei Vibration wird per EMA-Filter geglaettet.
    Serial.println("[Sensor] Enabling Accelerometer...");
    if (imu.enableAccelerometer(SENSOR_UPDATE_MS)) {
        Serial.println("[Sensor] Accelerometer enabled!");
        // KEIN saveCalibration() beim Boot! Wuerde einen evtl. temperatur-
        // verzogenen Zustand dauerhaft speichern. Nur manuell per "cal"-Befehl
        // bei stabiler Temperatur.
        sensorData.sensor_ok = true;
        sensorData.cal_status = 3;
        return true;
    }
    
    // Fallback: Gravity-Report
    Serial.println("[Sensor] Trying Gravity report (fallback)...");
    if (imu.enableGravity(SENSOR_UPDATE_MS)) {
        Serial.println("[Sensor] Gravity report enabled (fallback)");
        sensorData.sensor_ok = true;
        sensorData.cal_status = 3;
        return true;
    }
    
    Serial.println("[Sensor] All reports FAILED!");
    sensorData.sensor_ok = false;
    return false;
}

// ============================================================
// Sensor Loop
// ============================================================
void sensorLoop() {
    if (millis() - lastSensorRead < SENSOR_UPDATE_MS) {
        yield();
        return;
    }
    lastSensorRead = millis();
    
    if (!sensorData.sensor_ok) return;
    
    if (imu.wasReset()) {
        static unsigned long lastReset = 0;
        static int resetCount = 0;
        
        if (millis() - lastReset < 2000) {
            resetCount++;
            if (resetCount > 3) {
                Serial.println("[Sensor] Too many resets - pausing 5s");
                delay(10);
                lastReset = millis() + 5000;
                resetCount = 0;
                return;
            }
        } else {
            resetCount = 0;
        }
        
        lastReset = millis();
        Serial.println("[Sensor] BNO085 was reset - re-enabling");
        imu.enableAccelerometer(SENSOR_UPDATE_MS);
        delay(10);
    }
    
    if (imu.getSensorEvent()) {
        uint8_t eventId = imu.getSensorEventID();
        
        if (eventId == SENSOR_REPORTID_ACCELEROMETER ||
            eventId == SENSOR_REPORTID_GRAVITY) {
            // Beschleunigungs-Vektor vom BNO085 (in m/s²)
            // Bei Stillstand = reine Schwerkraft (driftfrei)
            float gx, gy, gz;
            if (eventId == SENSOR_REPORTID_ACCELEROMETER) {
                gx = imu.getAccelX();
                gy = imu.getAccelY();
                gz = imu.getAccelZ();
            } else {
                gx = imu.getGravityX();
                gy = imu.getGravityY();
                gz = imu.getGravityZ();
            }
            
            // --- Plausibilitaetspruefung ---
            // Der Schwerkraft-Vektor MUSS eine Laenge von ~9.81 m/s² haben.
            // Korrupte I2C-Reads (Sprung auf 69°, 142° etc.) haben eine
            // voellig falsche Vektorlaenge → verwerfen.
            float gMag = sqrtf(gx*gx + gy*gy + gz*gz);
            if (gMag < 8.0f || gMag > 11.5f) {
                // Ungueltige Messung — ignorieren, alten Wert behalten
                return;
            }
            
            // Debug-Werte speichern
            sensorData.gx = gx;
            sensorData.gy = gy;
            sensorData.gz = gz;
            
            // Gravity → Fahrzeug-Koordinaten umrechnen (inkl. Einbaulage)
            float vgx, vgy, vgz;
            remapGravity(gx, gy, gz, &vgx, &vgy, &vgz);
            
            // Fahrzeug-Gravity → Pitch/Roll
            float orientedPitch, orientedRoll;
            vehicleGravityToPitchRoll(vgx, vgy, vgz, &orientedPitch, &orientedRoll);
            
            sensorData.raw_pitch = orientedPitch;
            sensorData.raw_roll = orientedRoll;
            sensorData.cal_status = 3;  // Gravity ist immer kalibriert
            
            // Tare
            if (config.tare_active) {
                sensorData.pitch = orientedPitch - config.tare_pitch;
                sensorData.roll = orientedRoll - config.tare_roll;
            } else {
                sensorData.pitch = orientedPitch;
                sensorData.roll = orientedRoll;
            }
            
            // EMA-Filter
            if (config.filter_active) {
                if (!ema_initialized) {
                    ema_pitch = sensorData.pitch;
                    ema_roll  = sensorData.roll;
                    ema_initialized = true;
                } else {
                    ema_pitch = EMA_ALPHA * sensorData.pitch + (1.0f - EMA_ALPHA) * ema_pitch;
                    ema_roll  = EMA_ALPHA * sensorData.roll  + (1.0f - EMA_ALPHA) * ema_roll;
                }
                sensorData.pitch = ema_pitch;
                sensorData.roll  = ema_roll;
            } else {
                ema_initialized = false;
            }
            
            // Totzone + Rundung
            sensorData.pitch = (fabsf(sensorData.pitch) < DEADBAND) ? 0.0f : roundf(sensorData.pitch * 10.0f) / 10.0f;
            sensorData.roll  = (fabsf(sensorData.roll)  < DEADBAND) ? 0.0f : roundf(sensorData.roll  * 10.0f) / 10.0f;
            
            calculateWedges();
            
            sensorData.is_level = (fabsf(sensorData.pitch) <= config.tolerance) && 
                                  (fabsf(sensorData.roll) <= config.tolerance);
        }
    }
    
    sensorData.temperature = 25.0f + (analogRead(0) % 100) / 10.0f;
}

// ============================================================
// Tare
// ============================================================
void sensorSetTare() {
    // raw_pitch/raw_roll sind schon im Fahrzeug-Frame (nach remapGravity)
    config.tare_pitch = sensorData.raw_pitch;
    config.tare_roll = sensorData.raw_roll;
    config.tare_active = true;
    saveConfig();  // Tare-Offset dauerhaft speichern (ueberlebt Neustart!)
    Serial.printf("[Sensor] Tare set: pitch=%.2f roll=%.2f\n", config.tare_pitch, config.tare_roll);
}

void sensorResetTare() {
    config.tare_pitch = 0.0f;
    config.tare_roll = 0.0f;
    config.tare_active = false;
    saveConfig();  // Aenderung dauerhaft speichern
    Serial.println("[Sensor] Tare reset");
}

void sensorResetCalibration() {
    imu.softReset();
    delay(300);
    imu.enableAccelerometer(SENSOR_UPDATE_MS);
    delay(100);
    imu.saveCalibration();   // DCD im Flash speichern
    sensorData.cal_status = 3;
    Serial.println("[Sensor] Kalibrierung gespeichert");
}

// ============================================================
// Getters
// ============================================================
float getPitchDeg() { return sensorData.pitch; }
float getRollDeg()  { return sensorData.roll; }

String getCalStatusString() {
    switch (sensorData.cal_status) {
        case 0: return "Nicht kalibriert";
        case 1: return "Niedrig";
        case 2: return "Mittel";
        case 3: return "Hoch";
        default: return "Unbekannt";
    }
}
