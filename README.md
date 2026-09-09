# WoWa-Level – Wohnwagen/Wohnmobil Neigungssensor

Präziser DIY-Neigungssensor zum Nivellieren von Wohnwagen und Wohnmobilen, mit Web-UI und Home-Assistant-Anbindung.

## Hardware

- **MCU:** ESP32 WROOM-32 (DevKitC 38-Pin)
- **IMU:** Adafruit BNO085 9-DoF Breakout (#4754), angebunden über **I2C**
- **Stromversorgung:** USB (Micro-USB) oder 5V/VIN

> Hinweis: Ursprünglich war ein ESP32-C3 Super Mini geplant, dessen 3.3V-Regler aber unter WiFi-Last zusammen mit dem BNO085 instabil wurde. Der WROOM-32 mit stärkerem Regler und Dual-Core läuft stabil.

## Pin-Belegung (I2C)

| ESP32 WROOM-32 | BNO085 | Funktion |
| --- | --- | --- |
| GPIO21 | SDA | I2C Data |
| GPIO22 | SCL | I2C Clock |
| 3V3 (oder VIN/5V) | VIN | Versorgung |
| GND | GND | Masse |

## Arduino IDE Setup

### Board

- **Board Manager URL:** `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- **Board:** ESP32 Dev Module
- **Flash Size:** 4MB
- **Partition Scheme:** Default 4MB with spiffs

### Benötigte Libraries (Library Manager)

1. **SparkFun BNO08x Cortex Based IMU** (by SparkFun)
2. **PubSubClient** (by Nick O'Leary)
3. **ArduinoJson** (by Benoit Blanchon) – Version 7.x
4. **ESPAsyncWebServer** (ESP32Async-Fork: github.com/ESP32Async/ESPAsyncWebServer)
5. **AsyncTCP** (ESP32Async-Fork) – Dependency von ESPAsyncWebServer

### LittleFS Upload (Web-UI)

Der `data/`-Ordner enthält die Web-UI (`index.html`) und muss separat hochgeladen werden:

1. Arduino IDE 2.x: Plugin `arduino-littlefs-upload` installieren
2. Serial Monitor schließen (sonst ist der Port blockiert)
3. **Strg+Shift+P** → `Upload LittleFS to Pico/ESP8266/ESP32`

> ⚠️ Der LittleFS-Upload **löscht die gespeicherte config.json** (WLAN, MQTT, Tare). Der normale Sketch-Upload (Upload-Button) lässt die Config unangetastet.

### Reihenfolge

1. Board auf "ESP32 Dev Module" stellen, Port wählen
2. `data/` per LittleFS hochladen (nur nötig wenn sich die Web-UI geändert hat)
3. Sketch kompilieren und hochladen

## Features

- ✅ BNO085 **Accelerometer**-Modus (driftarm, kein Gyro-Drift, temperaturkompensiert)
- ✅ **8 Einbaulagen**: Flach (USB vorne/hinten/links/rechts) + Wand (vorne/hinten/links/rechts)
- ✅ **Fahrzeugtypen**: Wohnwagen (1 Achse + Stützrad) oder Wohnmobil (2 Achsen, 4 Keile)
- ✅ **Tare-Funktion** mit persistenter Speicherung (überlebt Neustart)
- ✅ **Keil-Berechnung** (Spurweite / Radstand / Achsabstand konfigurierbar)
- ✅ **Glättungsfilter** (EMA, zuschaltbar) gegen Zittern
- ✅ **Totzone** (±0.1°) + Anzeige auf 1 Dezimalstelle → ruhiges Bild
- ✅ **Ausreißerfilter** (verwirft korrupte I2C-Reads über Vektorlänge)
- ✅ WiFi Manager mit blockierendem Boot-Connect + AP-Fallback
- ✅ WiFi Power-Save deaktiviert (stabile Verbindung unter Last)
- ✅ MQTT mit Home Assistant Auto-Discovery + **Last Will** (offline-Erkennung)
- ✅ **MQTT-Geschwindigkeit** umschaltbar: 0.5 Hz (Dauerbetrieb) / 5 Hz (Einrichten)
- ✅ Web-UI mit Live-Wasserwaage (WebSocket, 5 Hz), Dark-Theme, responsive
- ✅ OTA Updates
- ✅ JSON-Konfiguration auf LittleFS

## Serial-Befehle (115200 baud, Zeilenende: Newline)

| Befehl | Aktion |
| --- | --- |
| `tare` | Tare setzen (aktuelle Position = 0°) |
| `notare` | Tare löschen |
| `cal` | Sensor-Kalibrierung (DCD) im BNO085-Flash speichern |
| `filter` | Glättungsfilter ein/aus |
| `fast` | MQTT-Geschwindigkeit 0.5 Hz ↔ 5 Hz |
| `grav` | Rohen Gravity-Vektor + Pitch/Roll ausgeben (Debug) |
| `help` | Befehlsübersicht |

## MQTT Topics (Prefix: `wowa/level`)

| Topic | Beschreibung |
| --- | --- |
| `.../pitch` | Längsneigung in Grad |
| `.../roll` | Querneigung in Grad |
| `.../temperature` | Temperatur in °C |
| `.../calibration` | Kalibrierungsstatus (0-3) |
| `.../rssi` | WiFi RSSI (dBm) |
| `.../mount` | Einbaulage |
| `.../vehicle_type` | Wohnwagen / Wohnmobil |
| `.../tare_active` | Tare aktiv (ON/OFF) |
| `.../filter_active` | Filter aktiv (ON/OFF) |
| `.../mqtt_fast` | MQTT schnell (ON/OFF) |
| `.../is_level` | Innerhalb Toleranz (ON/OFF) |
| `.../tolerance` | Toleranz in Grad |
| `.../status` | online / offline (Last Will) |
| **Wohnwagen:** |  |
| `.../wedge_left`, `.../wedge_right` | Keilhöhe links/rechts (cm) |
| `.../jockey_wheel` | Stützrad-Korrektur (cm) |
| **Wohnmobil:** |  |
| `.../wedge_fl`, `.../wedge_fr`, `.../wedge_rl`, `.../wedge_rr` | 4 Keile (cm) |

### MQTT Command Topics

| Topic | Payload | Aktion |
| --- | --- | --- |
| `.../cmd/tare/set` | `PRESS` | Tare setzen (Button) |
| `.../cmd/tare_reset/set` | `PRESS` | Tare löschen (Button) |
| `.../cmd/calibration_reset/set` | `PRESS` | Kalibrierung speichern (Button) |
| `.../cmd/filter/set` | `ON`/`OFF`/`toggle` | Filter schalten (Switch) |
| `.../cmd/mqtt_speed/set` | `fast`/`slow`/`toggle` | MQTT-Geschwindigkeit |
| `.../cmd/mount/set` | z.B. `Flach USB vorne`, `Wand vorne` | Einbaulage |
| `.../cmd/vehicle_type/set` | `Wohnwagen`/`Wohnmobil` | Fahrzeugtyp |
| `.../cmd/tolerance/set` | Float `0.0`–`1.0` | Toleranz in Grad |

## Home Assistant Entities (Auto-Discovery)

- **Sensoren:** Pitch, Roll, Temperatur, Kalibrierung, RSSI, Toleranz, Keile
- **Binary Sensoren:** Tare Aktiv, Level Status, Filter Aktiv, MQTT Schnell
- **Selects:** Einbaulage, Fahrzeugtyp
- **Number:** Toleranz Einstellung (0.0–1.0°)
- **Switches:** Glättungsfilter, MQTT Schnell
- **Buttons:** Tare setzen, Tare zurücksetzen, Kalibrierung zurücksetzen

## Erstbenutzung

1. Nach dem Flashen startet der ESP im AP-Mode
2. Mit WLAN **"WoWa-Level"** verbinden (Passwort: `levelsensor`)
3. Browser: `http://192.168.4.1`
4. Im WiFi-Tab WLAN-Zugangsdaten eingeben
5. Im Setup-Tab MQTT-Server und Fahrzeugmaße konfigurieren
6. Einbaulage wählen (z.B. "USB vorne" bei flacher Montage)

## Kalibrierung & Nivellierung

1. **Einmalig:** Sensor bei stabiler Temperatur und ruhiger, ebener Lage → `cal` ausführen (speichert Accelerometer-Kalibrierung im BNO085-Flash)
2. **Beim Aufstellen:** Wohnwagen nivellieren, dann **Tare setzen** → aktuelle Position wird als 0°/0° Referenz gespeichert
3. Bei jedem weiteren Stellplatz: Sensor zeigt die Abweichung → nach Anzeige Keile unterlegen / Stützrad kurbeln bis "Level Status: ON"

> `cal` = Sensor absolut eichen (relativ zur Schwerkraft) `tare` = mechanischen Einbau-Versatz nullen (deine Referenz) Beide werden gespeichert und überleben Neustarts.

## OTA Update

- Hostname: `wowa-level`
- Passwort: `wowa2024`
- Arduino IDE: Port → Netzwerk-Port "wowa-level"

## Bekannte Hinweise

- **Montage:** Starre Montage bevorzugen. Viskoelastische Gummidämpfer (Flightcontroller-Gel) können über Stunden "kriechen" → langsamer Drift.
- **Einbauort:** Nicht direkt an ungedämmter Außenwand / hinter Fenster (Temperaturgradienten verursachen minimalen Drift).
- **BNO085 nicht verspannt montieren** — mechanische Spannung auf die Platine überträgt sich als Messfehler.

## Projektdateien

```
wowa_level/
├── wowa_level.ino      – Hauptsketch (setup/loop, Serial-Befehle, OTA)
├── config.h            – Pins, Defaults, Structs, Enums
├── sensor.h/.cpp       – BNO085 I2C, Accelerometer, Tare, Keil-Berechnung
├── wifi_manager.h/.cpp – WiFi Connect/AP/Fallback, Power-Save aus
├── mqtt_ha.h/.cpp      – MQTT, HA Auto-Discovery, LWT, Command-Handler
├── webserver.h/.cpp    – REST API, WebSocket, LittleFS Config
└── data/
    └── index.html      – Web-UI (Dark-Theme, 4 Tabs, Live-Wasserwaage)

```

