# 🚐 WoWa Level Card v5.3

Eine maßgeschneiderte Lovelace-Karte für **Home Assistant** zur präzisen Ausrichtung und Nivellierung von Wohnwagen und Wohnmobilen.

---

## 🌟 Features

* **Animierte Neigungsanzeige:** Rotations-Visualisierung für Seitenansicht (Längsneigung / Pitch) und Heckansicht (Querneigung / Roll).
* **Nivellier-Anweisungen:**
  * Direkte Höhenanzeige in cm für Keile (links/rechts).
  * Richtungs- und Höhenanweisungen für das Stützrad (hoch/runter in cm).
* **Schnell-Modus / Boost-Modus:**
  * Integrierter Button zum Schalten einer höheren Sensor-Abtastrate (z. B. 5 Hz via ESPHome).
  * Auto-Reset-Timer schaltet den Boost-Modus nach Ablauf der eingestellten Zeit automatisch wieder ab.
* **Erweiterte Konfiguration:**
  * Achsen-Invertierung und Achsen-Tausch (Pitch / Roll).
  * Separates Invertieren der Text-Anweisungen (falls Keilzuordnungen gespiegelt sind).
  * Flacker-Schutz mit Hysterese-Logik an den Toleranzgrenzen.
  * Dauerhafte Textanzeige aller Achsen oder dynamische Aktionsanweisung.
* **Visueller Editor:** Vollständig integrierter GUI-Editor zur bequemen Konfiguration im Dashboard.

![WoWa Level Card](wowalevelcard.png)

---

## 📦 Installation

### Manuelle Installation

1. Erstelle auf deinem Home Assistant Server folgenden Ordner (falls nicht vorhanden):
   ```bash
   /config/www/wowa-level-card/
   ```
2. Speichere die Datei `wowa-level-card.js` in diesem Ordner ab.
3. Lege deine Bilddateien im Unterordner `img` ab:
   ```bash
   /config/www/wowa-level-card/img/seitenansichtkleiner.png
   /config/www/wowa-level-card/img/heckansicht.png
   ```
4. Registriere die Karte als Ressource in Home Assistant:
   * Gehe zu **Einstellungen** ➔ **Dashboards** ➔ oben rechts auf das **3-Punkte-Menü** ➔ **Ressourcen**.
   * Klicke auf **Ressource hinzufügen**.
   * **URL:** `/local/wowa-level-card/wowa-level-card.js?v=5.3`
   * **Typ:** `JavaScript-Modul`

---

## ⚙️ Konfiguration

Die Karte kann direkt über den **visuellen Dashboard-Editor** oder per **YAML** konfiguriert werden.

### YAML-Beispiel

```yaml
type: custom:wowa-level-card
name: Wohnwagen Ausrichtung
entity_roll: sensor.wowa_roll
entity_pitch: sensor.wowa_pitch
entity_level_ok: binary_sensor.wowa_ausgerichtet
entity_wedge_left: sensor.wowa_keil_links
entity_wedge_right: sensor.wowa_keil_rechts
entity_support: sensor.wowa_stutzrad
entity_boost: switch.wowa_sensor_boost
boost_duration: 60
tolerance: 0.5
visual_factor: 3
anti_flicker: true
always_show_text: false
swap_pitch_roll: false
invert_roll: true
invert_pitch: false
invert_roll_text: false
invert_pitch_text: false
image_side: /local/wowa-level-card/img/seitenansichtkleiner.png
image_rear: /local/wowa-level-card/img/heckansicht.png
```

---

## 📋 Optionen

| Parameter | Typ | Standard | Beschreibung |
| :--- | :--- | :--- | :--- |
| `name` | String | `Wohnwagen Ausrichtung` | Titel der Karte im Dashboard |
| `entity_roll` | String | *erforderlich* | Sensor für Querneigung (Roll) in Grad |
| `entity_pitch` | String | *erforderlich* | Sensor für Längsneigung (Pitch) in Grad |
| `entity_level_ok` | String | *optional* | Binary Sensor (`on`/`off`), der die korrekte Ausrichtung meldet |
| `entity_wedge_left` | String | *optional* | Sensor für die Keilhöhe links in cm |
| `entity_wedge_right` | String | *optional* | Sensor für die Keilhöhe rechts in cm |
| `entity_support` | String | *optional* | Sensor für Höhenkorrektur am Stützrad in cm |
| `entity_boost` | String | *optional* | Switch-Entity zur Aktivierung des Fast-Update-Modus |
| `boost_duration` | Zahl | `60` | Auto-Off-Timer für den Boost-Modus in Sekunden |
| `tolerance` | Zahl | `0.5` | Toleranzwert in Grad für die Grün-Bewertung |
| `visual_factor` | Zahl | `3` | Multiplikator für die grafische Neigung |
| `anti_flicker` | Boolean | `true` | Aktiviert die Hysterese gegen Flackern an Grenzwerten |
| `always_show_text` | Boolean | `false` | Zeigt beide Achsen dauerhaft im Textbereich an |
| `swap_pitch_roll` | Boolean | `false` | Vertauscht Pitch- und Roll-Eingänge |
| `invert_roll` | Boolean | `true` | Invertiert die Drehung des Heckbildes |
| `invert_pitch` | Boolean | `false` | Invertiert die Drehung des Seitenbildes |
| `invert_roll_text` | Boolean | `false` | Invertiert Richtungsanweisungen & Keil-Zuordnung links/rechts |
| `invert_pitch_text` | Boolean | `false` | Invertiert Richtungsanweisungen für das Stützrad (hoch/runter) |
| `image_side` | String | `/local/...` | Pfad zum Bild der Seitenansicht |
| `image_rear` | String | `/local/...` | Pfad zum Bild der Heckansicht |

---

## 🖼️ Bild-Assets

Für eine optimale Darstellung sollten die Bilder als **PNG mit transparentem Hintergrund** vorliegen:
* **Seitenansicht (`image_side`):** Seitenprofil des Wohnwagens (Deichsel vorzugsweise nach links).
* **Heckansicht (`image_rear`):** Ansicht des Wohnwagens von hinten.

---

## 📄 Lizenz

MIT License – Frei nutzbar und anpassbar.