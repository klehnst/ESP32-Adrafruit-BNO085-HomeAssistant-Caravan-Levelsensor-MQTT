#include "sensor.h"
#include <Wire.h>
#include <SparkFun_BNO08x_Arduino_Library.h>
#include "webserver.h"   // fuer saveConfig()

static BNO08x imu;
static unsigned long lastSensorRead = 0;
static float filteredPitch = 0.0f;
static float filteredRoll = 0.0f;
static bool filterInitialized = false;

// Totzone fuer Anzeige
#define DEADBAND 0.1f

// ============================================================
// Einbaulage (Remap der Roh-Euler-Winkel je nach Montage)
// ============================================================
static void applyMountingOrientation(float rawPitch, float rawRoll, float* outPitch, float* outRoll) {
    switch (config.mount) {
        // --- Flach auf dem Boden ---
        case MOUNT_USB_FRONT:
            *outPitch = rawPitch;
            *outRoll  = rawRoll;
            break;
        case MOUNT_USB_BACK:
            *outPitch = -rawPitch;
            *outRoll  = -rawRoll;
            break;
        case MOUNT_USB_LEFT:
            *outPitch = rawRoll;
            *outRoll  = -rawPitch;
            break;
        case MOUNT_USB_RIGHT:
            *outPitch = -rawRoll;
            *outRoll  = rawPitch;
            break;

        // --- Wandmontage ---
        case MOUNT_WALL_FRONT:
            *outPitch = rawRoll;
            *outRoll  = rawPitch;
            break;
        case MOUNT_WALL_BACK:
            *outPitch = -rawRoll;
            *outRoll  = -rawPitch;
            break;
        case MOUNT_WALL_LEFT:
            *outPitch = rawPitch;
            *outRoll  = -rawRoll;
            break;
        case MOUNT_WALL_RIGHT:
            *outPitch = -rawPitch;
            *outRoll  = rawRoll;
            break;

        default:
            *outPitch = rawPitch;
            *outRoll  = rawRoll;
            break;
    }
}

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
// Sensor Init — Game Rotation Vector (Accel + Gyro Fusion)
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
    
    // Game Rotation Vector aktiviert 6-DOF Sensor Fusion (Accel + Gyro).
    // Magnetometer ist VOLLSTÄNDIG deaktiviert -> unempfindlich gegen Stromleitungen/Alu.
    // Gyro filtert Rauschen/Vibrationen -> kein Zappeln der Achsen.
    Serial.println("[Sensor] Enabling Game Rotation Vector...");
    if (imu.enableGameRotationVector(SENSOR_UPDATE_MS)) {
        Serial.println("[Sensor] Game Rotation Vector enabled!");
        sensorData.sensor_ok = true;
        sensorData.cal_status = 3; // Fusions-orientiert
        return true;
    }
    
    Serial.println("[Sensor] Game Rotation Vector FAILED!");
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
        Serial.println("[Sensor] BNO085 was reset - re-enabling Game Rotation Vector");
        imu.enableGameRotationVector(SENSOR_UPDATE_MS);
        delay(10);
    }
    
    if (imu.getSensorEvent()) {
        if (imu.getSensorEventID() == SENSOR_REPORTID_GAME_ROTATION_VECTOR) {
            
            // 1. Quaternionen direkt vom BNO085 abfragen
            float qr = imu.getQuatReal();
            float qi = imu.getQuatI();
            float qj = imu.getQuatJ();
            float qk = imu.getQuatK();

            // 2. Mathematisch exakte Umrechnung von Quaternion -> Roll & Pitch
            float sinr_cosp = 2.0f * (qr * qi + qj * qk);
            float cosr_cosp = 1.0f - 2.0f * (qi * qi + qj * qj);
            float rawRoll = atan2f(sinr_cosp, cosr_cosp) * RAD_TO_DEG;

            float sinp = 2.0f * (qr * qj - qk * qi);
            float rawPitch;
            if (fabsf(sinp) >= 1.0f)
                rawPitch = copysignf(90.0f, sinp);
            else
                rawPitch = asinf(sinp) * RAD_TO_DEG;

            // 3. Ausrichtung / Einbaulage anwenden
            float orientedPitch, orientedRoll;
            applyMountingOrientation(rawPitch, rawRoll, &orientedPitch, &orientedRoll);

            sensorData.raw_pitch = orientedPitch;
            sensorData.raw_roll  = orientedRoll;
            sensorData.cal_status = 3;

            // 4. Tare (Nullpunkt-Offset) anwenden
            if (config.tare_active) {
                sensorData.pitch = orientedPitch - config.tare_pitch;
                sensorData.roll  = orientedRoll  - config.tare_roll;
            } else {
                sensorData.pitch = orientedPitch;
                sensorData.roll  = orientedRoll;
            }

            // Exponentielle Glaettung. Ohne diesen Schritt war die per UI/MQTT
            // schaltbare Filter-Option lediglich ein gespeicherter Schalter.
            if (config.filter_active) {
                constexpr float alpha = 0.25f;
                if (!filterInitialized) {
                    filteredPitch = sensorData.pitch;
                    filteredRoll = sensorData.roll;
                    filterInitialized = true;
                } else {
                    filteredPitch += alpha * (sensorData.pitch - filteredPitch);
                    filteredRoll += alpha * (sensorData.roll - filteredRoll);
                }
                sensorData.pitch = filteredPitch;
                sensorData.roll = filteredRoll;
            } else {
                filterInitialized = false;
            }

            // 5. Totzone & Runden auf 0.1° Präzision
            sensorData.pitch = (fabsf(sensorData.pitch) < DEADBAND) ? 0.0f : roundf(sensorData.pitch * 10.0f) / 10.0f;
            sensorData.roll  = (fabsf(sensorData.roll)  < DEADBAND) ? 0.0f : roundf(sensorData.roll  * 10.0f) / 10.0f;

            // 6. Keile & Ausrichtung berechnen
            calculateWedges();

            sensorData.is_level = (fabsf(sensorData.pitch) <= config.tolerance) && 
                                  (fabsf(sensorData.roll)  <= config.tolerance);
        }
    }
    
    // Temperatur-Messung (Dummy/Analog oder intern)
    sensorData.temperature = 25.0f + (analogRead(0) % 100) / 10.0f;
}

// ============================================================
// Tare
// ============================================================
void sensorSetTare() {
    config.tare_pitch = sensorData.raw_pitch;
    config.tare_roll  = sensorData.raw_roll;
    config.tare_active = true;
    saveConfig();
    Serial.printf("[Sensor] Tare set: pitch=%.2f roll=%.2f\n", config.tare_pitch, config.tare_roll);
}

void sensorAdjustTare(float deltaPitch, float deltaRoll) {
    // Delta auf bestehenden Tare-Offset addieren
    // Wenn Tare noch nicht aktiv war, wird er aktiviert
    config.tare_pitch += deltaPitch;
    config.tare_roll  += deltaRoll;
    config.tare_active = true;
    saveConfig();
    Serial.printf("[Sensor] Tare adjusted by delta P=%.2f R=%.2f -> new P=%.2f R=%.2f\n",
        deltaPitch, deltaRoll, config.tare_pitch, config.tare_roll);
}

void sensorResetTare() {
    config.tare_pitch = 0.0f;
    config.tare_roll  = 0.0f;
    config.tare_active = false;
    saveConfig();
    Serial.println("[Sensor] Tare reset");
}

void sensorResetCalibration() {
    imu.softReset();
    delay(300);
    imu.enableGameRotationVector(SENSOR_UPDATE_MS);
    delay(100);
    sensorData.cal_status = 3;
    Serial.println("[Sensor] BNO085 neugestartet");
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

