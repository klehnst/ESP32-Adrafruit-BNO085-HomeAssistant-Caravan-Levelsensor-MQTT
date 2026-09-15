/**
 * 🚐 WoWa Level Card v5.3 – Home Assistant Custom Card
 *
 * Pitch/Roll-Anzeige + Nivellier-Anweisungen für Wohnwagen.
 * Inklusive dauerhafter Achsen-Anzeige, Flacker-Schutz & Text-Invertierung für Keil-Entitäten.
 */

/* ── Card Picker Registration ──────────────────────────────── */
window.customCards = window.customCards || [];
window.customCards.push({
  type: "wowa-level-card",
  name: "Wohnwagen Ausrichtung",
  description:
    "Neigungsanzeige mit Nivellier-Anweisungen und Boost-Modus für Wohnwagen",
  preview: false,
});


![WoWa Level Card](WoWa-Level-Card/wowalevelcard.png)


/* ── Defaults ──────────────────────────────────────────────── */
const WLC_DEFAULTS = {
  name: "Wohnwagen Ausrichtung",
  entity_roll: "",
  entity_pitch: "",
  entity_wedge_left: "",
  entity_wedge_right: "",
  entity_support: "",
  entity_level_ok: "",
  entity_boost: "",
  boost_duration: 60,
  swap_pitch_roll: false,
  invert_roll: true,
  invert_pitch: false,
  invert_roll_text: false,
  invert_pitch_text: false,
  anti_flicker: true,
  always_show_text: false,
  visual_factor: 3,
  tolerance: 0.5,
  image_side: "/local/wowa-level-card/img/seitenansichtkleiner.png",
  image_rear: "/local/wowa-level-card/img/heckansicht.png",
};

/* ═══════════════════════════════════════════════════════════════
   VISUAL EDITOR
   ═══════════════════════════════════════════════════════════════ */
class WowaLevelCardEditor extends HTMLElement {
  constructor() {
    super();
    this._config = {};
    this._hass = null;
    this._els = {};
  }

  set hass(hass) {
    this._hass = hass;
    Object.keys(this._els).forEach((k) => {
      if (this._els[k] && typeof this._els[k].hass !== "undefined") {
        this._els[k].hass = hass;
      }
    });
  }

  setConfig(config) {
    this._config = { ...WLC_DEFAULTS, ...config };
    this._build();
  }

  _build() {
    this.innerHTML = "";
    this._els = {};

    const f = document.createElement("div");
    f.style.cssText =
      "display:flex;flex-direction:column;gap:16px;padding:16px 0";

    f.appendChild(this._mkField("name", "Kartenname"));

    f.appendChild(this._mkHeading("Neigungssensoren"));
    f.appendChild(this._mkEntity("entity_roll", "Roll Entity (Seitliche Neigung)"));
    f.appendChild(this._mkEntity("entity_pitch", "Pitch Entity (Längsneigung)"));

    f.appendChild(this._mkHeading("Nivellierung"));
    f.appendChild(this._mkEntity("entity_level_ok", "Level Status (Optional: Sensor mit ON/OFF)"));
    f.appendChild(this._mkEntity("entity_wedge_left", "Keil links (Optional: cm)"));
    f.appendChild(this._mkEntity("entity_wedge_right", "Keil rechts (Optional: cm)"));
    f.appendChild(this._mkEntity("entity_support", "Stützrad (Optional: cm)"));

    f.appendChild(this._mkHeading("Schnell-Modus / Boost"));
    f.appendChild(this._mkEntity("entity_boost", "Boost Switch Entity"));
    f.appendChild(this._mkNum("boost_duration", "Dauer bis Auto-Off (Sekunden)", 1, 60));

    f.appendChild(this._mkHeading("Optionen"));
    f.appendChild(
      this._mkRow(
        this._mkNum("tolerance", "Toleranz (°)", 0.1, 0.5),
        this._mkNum("visual_factor", "Visuelle Verstärkung", 0.5, 3)
      )
    );
    f.appendChild(this._mkToggle("always_show_text", "Texte (Querneigung/Längsneigung) dauerhaft anzeigen"));
    f.appendChild(this._mkToggle("anti_flicker", "Flacker-Schutz (Hysterese) bei Toleranz"));
    f.appendChild(this._mkToggle("swap_pitch_roll", "Pitch & Roll Achsen tauschen"));
    f.appendChild(this._mkToggle("invert_roll", "Bild Roll (Querneigung) invertieren"));
    f.appendChild(this._mkToggle("invert_pitch", "Bild Pitch (Längsneigung) invertieren"));
    f.appendChild(this._mkToggle("invert_roll_text", "Text Querneigung umkehren"));
    f.appendChild(this._mkToggle("invert_pitch_text", "Text Längsneigung umkehren"));

    f.appendChild(this._mkHeading("Bilder"));
    f.appendChild(this._mkField("image_side", "Seitenansicht (Pfad)"));
    f.appendChild(this._mkField("image_rear", "Heckansicht (Pfad)"));

    this.appendChild(f);
  }

  _mkHeading(t) {
    const h = document.createElement("div");
    h.textContent = t;
    h.style.cssText =
      "font-size:.85em;font-weight:600;text-transform:uppercase;" +
      "letter-spacing:.5px;color:var(--secondary-text-color);margin:8px 0 -4px";
    return h;
  }

  _mkEntity(k, label) {
    const el = document.createElement("ha-entity-picker");
    el.hass = this._hass;
    el.value = this._config[k] || "";
    el.label = label;
    el.allowCustomEntity = true;
    el.addEventListener("value-changed", (e) => this._set(k, e.detail.value));
    this._els[k] = el;
    return el;
  }

  _mkField(k, label) {
    const wrapper = document.createElement("div");
    wrapper.style.cssText = "display:flex;flex-direction:column;gap:6px;width:100%";

    const lbl = document.createElement("label");
    lbl.textContent = label;
    lbl.style.cssText = "font-size:0.85rem;color:var(--secondary-text-color);font-weight:500";

    const el = document.createElement("input");
    el.type = "text";
    el.value = this._config[k] ?? "";
    el.style.cssText = `
      padding: 10px 12px;
      border-radius: 8px;
      border: 1px solid var(--divider-color, rgba(127,127,127,0.3));
      background: var(--card-background-color, rgba(255,255,255,0.05));
      color: var(--primary-text-color, #fff);
      font-size: 0.95rem;
      outline: none;
      box-sizing: border-box;
      width: 100%;
    `;
    el.addEventListener("input", (e) => this._set(k, e.target.value));

    wrapper.appendChild(lbl);
    wrapper.appendChild(el);
    this._els[k] = el;
    return wrapper;
  }

  _mkNum(k, label, step, defaultVal = 0) {
    const wrapper = document.createElement("div");
    wrapper.style.cssText = "display:flex;flex-direction:column;gap:6px;width:100%";

    const lbl = document.createElement("label");
    lbl.textContent = label;
    lbl.style.cssText = "font-size:0.85rem;color:var(--secondary-text-color);font-weight:500";

    const el = document.createElement("input");
    el.type = "number";
    el.step = step;
    const curVal = this._config[k];
    el.value = (curVal !== undefined && curVal !== null && curVal !== "") ? curVal : defaultVal;
    el.style.cssText = `
      padding: 10px 12px;
      border-radius: 8px;
      border: 1px solid var(--divider-color, rgba(127,127,127,0.3));
      background: var(--card-background-color, rgba(255,255,255,0.05));
      color: var(--primary-text-color, #fff);
      font-size: 0.95rem;
      outline: none;
      box-sizing: border-box;
      width: 100%;
    `;
    el.addEventListener("input", (e) => {
      const v = parseFloat(e.target.value);
      this._set(k, isNaN(v) ? 0 : v);
    });

    wrapper.appendChild(lbl);
    wrapper.appendChild(el);
    this._els[k] = el;
    return wrapper;
  }

  _mkToggle(k, label) {
    const row = document.createElement("div");
    row.style.cssText =
      "display:flex;align-items:center;justify-content:space-between;padding:4px 0";
    const lbl = document.createElement("span");
    lbl.textContent = label;
    lbl.style.color = "var(--primary-text-color)";
    row.appendChild(lbl);
    const sw = document.createElement("ha-switch");
    sw.checked = !!this._config[k];
    sw.addEventListener("change", (e) => this._set(k, e.target.checked));
    this._els[k] = sw;
    row.appendChild(sw);
    return row;
  }

  _mkRow(...ch) {
    const r = document.createElement("div");
    r.style.cssText = "display:flex;gap:12px";
    ch.forEach((c) => {
      c.style.flex = "1";
      r.appendChild(c);
    });
    return r;
  }

  _set(k, v) {
    this._config = { ...this._config, [k]: v };
    this.dispatchEvent(
      new CustomEvent("config-changed", {
        detail: { config: { ...this._config } },
        bubbles: true,
        composed: true,
      })
    );
  }
}
customElements.define("wowa-level-card-editor", WowaLevelCardEditor);

/* ═══════════════════════════════════════════════════════════════
   MAIN CARD
   ═══════════════════════════════════════════════════════════════ */
class WowaLevelCard extends HTMLElement {
  static getConfigElement() {
    return document.createElement("wowa-level-card-editor");
  }
  static getStubConfig() {
    return { ...WLC_DEFAULTS };
  }

  constructor() {
    super();
    this._boostTimer = null;
    this._rollBad = false;
    this._pitchBad = false;
  }

  setConfig(config) {
    this.config = { ...WLC_DEFAULTS, ...config };
  }

  set hass(hass) {
    this._hass = hass;
    this._render();
  }

  _num(entityId) {
    if (!entityId) return null;
    const s = this._hass.states[entityId];
    if (!s || s.state === "unavailable" || s.state === "unknown") return null;
    const v = parseFloat(s.state);
    return isNaN(v) ? null : v;
  }

  _bool(entityId) {
    if (!entityId) return null;
    const s = this._hass.states[entityId];
    if (!s || s.state === "unavailable" || s.state === "unknown") return null;
    const v = String(s.state).toLowerCase().trim();
    return v === "on" || v === "true" || v === "1" || v === "yes";
  }

  _toggleBoost() {
    const cfg = this.config;
    if (!cfg.entity_boost || !this._hass) return;

    const currentState = this._bool(cfg.entity_boost);
    const newState = !currentState;

    const domain = cfg.entity_boost.split(".")[0];
    const service = newState ? "turn_on" : "turn_off";
    
    this._hass.callService(domain, service, {
      entity_id: cfg.entity_boost,
    });

    if (newState) {
      this._startBoostTimer();
    } else {
      this._clearBoostTimer();
    }
  }

  _startBoostTimer() {
    this._clearBoostTimer();
    const durationSec = Number(this.config.boost_duration) || 60;
    const durationMs = durationSec * 1000;
    
    this._boostTimer = setTimeout(() => {
      const cfg = this.config;
      if (cfg.entity_boost && this._hass) {
        const domain = cfg.entity_boost.split(".")[0];
        this._hass.callService(domain, "turn_off", {
          entity_id: cfg.entity_boost,
        });
      }
      this._boostTimer = null;
    }, durationMs);
  }

  _clearBoostTimer() {
    if (this._boostTimer) {
      clearTimeout(this._boostTimer);
      this._boostTimer = null;
    }
  }

  _render() {
    const cfg = this.config;
    const hass = this._hass;
    if (!hass) return;

    if (!this._card) {
      this._card = document.createElement("ha-card");
      this._wrap = document.createElement("div");
      this._wrap.className = "wlc";
      this._card.appendChild(this._wrap);
      this.appendChild(this._card);
      const s = document.createElement("style");
      s.textContent = WowaLevelCard.CSS;
      this._card.appendChild(s);

      this._wrap.addEventListener("click", (e) => {
        const boostBtn = e.target.closest("#wlc-boost-btn");
        if (boostBtn) {
          this._toggleBoost();
        }
      });
    }
    this._card.header = cfg.name;

    if (!cfg.entity_roll || !cfg.entity_pitch) {
      this._wrap.innerHTML = `
        <div class="wlc-msg">
          ⚙️ Bitte mindestens Roll- und Pitch-Entität im Editor konfigurieren.
        </div>`;
      return;
    }

    let rawRoll = this._num(cfg.entity_roll);
    let rawPitch = this._num(cfg.entity_pitch);

    if (cfg.swap_pitch_roll) {
      const temp = rawRoll;
      rawRoll = rawPitch;
      rawPitch = temp;
    }

    const roll = rawRoll !== null ? (cfg.invert_roll ? -rawRoll : rawRoll) : null;
    const pitch = rawPitch !== null ? (cfg.invert_pitch ? -rawPitch : rawPitch) : null;

    const rollStr = roll !== null ? roll.toFixed(1) : "–";
    const pitchStr = pitch !== null ? pitch.toFixed(1) : "–";

    const rollDeg = (roll ?? 0) * cfg.visual_factor;
    const pitchDeg = (pitch ?? 0) * cfg.visual_factor;

    const rawWedgeLeft = this._num(cfg.entity_wedge_left);
    const rawWedgeRight = this._num(cfg.entity_wedge_right);

    // Tauscht Keil links und Keil rechts, falls Text Querneigung umgekehrt ist
    const wedgeLeft = cfg.invert_roll_text ? rawWedgeRight : rawWedgeLeft;
    const wedgeRight = cfg.invert_roll_text ? rawWedgeLeft : rawWedgeRight;
    const supportWheel = this._num(cfg.entity_support);

    const tol = typeof cfg.tolerance === "number" ? cfg.tolerance : (parseFloat(cfg.tolerance) || 0.5);
    const hysteresis = 0.15;

    // Hysterese-Logik
    if (roll !== null) {
      if (cfg.anti_flicker) {
        if (this._rollBad) {
          if (Math.abs(roll) <= tol - hysteresis) this._rollBad = false;
        } else {
          if (Math.abs(roll) > tol) this._rollBad = true;
        }
      } else {
        this._rollBad = Math.abs(roll) > tol;
      }
    }

    if (pitch !== null) {
      if (cfg.anti_flicker) {
        if (this._pitchBad) {
          if (Math.abs(pitch) <= tol - hysteresis) this._pitchBad = false;
        } else {
          if (Math.abs(pitch) > tol) this._pitchBad = true;
        }
      } else {
        this._pitchBad = Math.abs(pitch) > tol;
      }
    }

    let rawLevelOk;
    if (cfg.entity_level_ok && cfg.entity_level_ok.trim() !== "") {
      rawLevelOk = this._bool(cfg.entity_level_ok);
    } else {
      if (roll !== null && pitch !== null) {
        rawLevelOk = !this._rollBad && !this._pitchBad;
      } else {
        rawLevelOk = null;
      }
    }

    const levelOk = rawLevelOk;
    const sensorsOk = roll !== null && pitch !== null;

    let boostHtml = "";
    if (cfg.entity_boost) {
      const isBoostActive = this._bool(cfg.entity_boost) === true;
      boostHtml = `
        <div class="wlc-boost-container">
          <button id="wlc-boost-btn" class="wlc-btn ${isBoostActive ? 'active' : ''}">
            <span class="wlc-btn-icon">${isBoostActive ? '⚡' : '⏱️'}</span>
            <div class="wlc-btn-text">
              <span class="wlc-btn-title">Schnell-Modus (5Hz)</span>
              <span class="wlc-btn-sub">${isBoostActive ? 'Aktiv (Auto-Reset läuft)' : 'Inaktiv (Standard 1Hz)'}</span>
            </div>
            <div class="wlc-indicator"></div>
          </button>
        </div>
      `;
    }

    let levelHtml = "";

    if (!sensorsOk) {
      levelHtml = `
        <div class="wlc-st wlc-na">
          <span>⚠️</span><span>Sensor nicht verfügbar</span>
        </div>`;
    } else if (cfg.always_show_text) {
      // ── DAUERHAFTE ANZEIGE ──
      const rollOk = !this._rollBad;
      const pitchOk = !this._pitchBad;

      // Querneigung Text
      let rollVal = "";
      if (rollOk) {
        rollVal = "Ausgerichtet";
      } else if (wedgeLeft !== null && Math.abs(wedgeLeft) >= 0.1) {
        rollVal = `Keil links ${Math.abs(wedgeLeft).toFixed(1)} cm`;
      } else if (wedgeRight !== null && Math.abs(wedgeRight) >= 0.1) {
        rollVal = `Keil rechts ${Math.abs(wedgeRight).toFixed(1)} cm`;
      } else {
        const isRollPos = cfg.invert_roll_text ? roll < 0 : roll > 0;
        rollVal = isRollPos ? "Links anheben" : "Rechts anheben";
      }

      // Längsneigung Text
      let pitchVal = "";
      if (pitchOk) {
        pitchVal = "Ausgerichtet";
      } else if (supportWheel !== null && Math.abs(supportWheel) >= 0.1) {
        const effSupport = cfg.invert_pitch_text ? -supportWheel : supportWheel;
        const dir = effSupport > 0 ? "hoch" : "runter";
        pitchVal = `Stützrad ${Math.abs(effSupport).toFixed(1)} cm ${dir}`;
      } else {
        const isPitchPos = cfg.invert_pitch_text ? pitch < 0 : pitch > 0;
        pitchVal = isPitchPos ? "Stützrad absenken" : "Stützrad anheben";
      }

      levelHtml = `
        <div class="wlc-act ${rollOk ? 'ok' : 'warn'}">
          <span class="wlc-dot">${rollOk ? '✓' : '⬥'}</span>
          <span class="wlc-al">Querneigung</span>
          <span class="wlc-av">${rollVal}</span>
        </div>
        <div class="wlc-act ${pitchOk ? 'ok' : 'warn'}">
          <span class="wlc-dot">${pitchOk ? '✓' : '⬥'}</span>
          <span class="wlc-al">Längsneigung</span>
          <span class="wlc-av">${pitchVal}</span>
        </div>
      `;
    } else {
      // ── DYNAMISCHE ANZEIGE ──
      const actions = [];
      const hasCmEntities = !!(cfg.entity_wedge_left || cfg.entity_wedge_right || cfg.entity_support);

      if (levelOk !== true) {
        const ACTION_THRESHOLD = 0.1;

        if (wedgeLeft !== null && Math.abs(wedgeLeft) >= ACTION_THRESHOLD) {
          actions.push({
            label: "Keil links",
            value: `${Math.abs(wedgeLeft).toFixed(1)} cm hoch`,
          });
        }
        if (wedgeRight !== null && Math.abs(wedgeRight) >= ACTION_THRESHOLD) {
          actions.push({
            label: "Keil rechts",
            value: `${Math.abs(wedgeRight).toFixed(1)} cm hoch`,
          });
        }
        if (supportWheel !== null && Math.abs(supportWheel) >= ACTION_THRESHOLD) {
          const effSupport = cfg.invert_pitch_text ? -supportWheel : supportWheel;
          const dir = effSupport > 0 ? "hoch" : "runter";
          actions.push({
            label: "Stützrad",
            value: `${Math.abs(effSupport).toFixed(1)} cm ${dir}`,
          });
        }

        if (actions.length === 0 && !hasCmEntities) {
          if (roll !== null && this._rollBad) {
            const isRollPos = cfg.invert_roll_text ? roll < 0 : roll > 0;
            const rollText = isRollPos ? "Wohnwagen links anheben" : "Wohnwagen rechts anheben";
            actions.push({
              label: "Querneigung",
              value: rollText,
            });
          }

          if (pitch !== null && this._pitchBad) {
            const isPitchPos = cfg.invert_pitch_text ? pitch < 0 : pitch > 0;
            const pitchText = isPitchPos ? "Stützrad absenken" : "Stützrad anheben";
            actions.push({
              label: "Längsneigung",
              value: pitchText,
            });
          }
        }
      }

      if (levelOk === true) {
        levelHtml = `
          <div class="wlc-st wlc-ok">
            <span>✅</span><span>Wohnwagen ist ausgerichtet</span>
          </div>`;
      } else if (actions.length > 0) {
        levelHtml = actions
          .map(
            (a) => `
          <div class="wlc-act warn">
            <span class="wlc-dot">⬥</span>
            <span class="wlc-al">${a.label}</span>
            <span class="wlc-av">${a.value}</span>
          </div>`
          )
          .join("");
      } else {
        levelHtml = `
          <div class="wlc-st wlc-warn">
            <span>⚠️</span><span>Wohnwagen ist nicht ausgerichtet</span>
          </div>`;
      }
    }

    this._wrap.innerHTML = `
      <div class="wlc-tilt">
        <div class="wlc-view">
          <div class="wlc-img">
            <img src="${cfg.image_side}"
                 style="transform:rotate(${pitchDeg}deg)" alt="Pitch">
          </div>
          <div class="wlc-val">
            <span class="wlc-deg">${pitchStr}°</span>
            <span class="wlc-lbl">Längsneigung (Pitch)</span>
          </div>
        </div>
        <div class="wlc-view">
          <div class="wlc-img">
            <img src="${cfg.image_rear}"
                 style="transform:rotate(${rollDeg}deg)" alt="Roll">
          </div>
          <div class="wlc-val">
            <span class="wlc-deg">${rollStr}°</span>
            <span class="wlc-lbl">Querneigung (Roll)</span>
          </div>
        </div>
      </div>

      ${boostHtml}

      <div class="wlc-hr"></div>
      <div class="wlc-hdr">Nivellierung</div>

      ${levelHtml}
    `;
  }

  getCardSize() {
    return 6;
  }
}

/* ── Card Styles ── */
WowaLevelCard.CSS = `
  .wlc { padding: 0 16px 16px }

  .wlc-tilt {
    display: flex; flex-wrap: wrap;
    justify-content: center; gap: 16px;
  }
  .wlc-view {
    flex: 1 1 200px; max-width: 400px; text-align: center;
  }
  .wlc-img {
    position: relative; width: 100%;
    padding-top: 60%; overflow: hidden;
  }
  .wlc-img img {
    position: absolute; top: 0; left: 0;
    width: 100%; height: 100%;
    object-fit: contain;
    transition: transform .3s ease;
  }
  .wlc-val {
    display: flex; flex-direction: column;
    align-items: center; margin-top: 6px;
  }
  .wlc-deg { font-size: 1.8rem; font-weight: 500; line-height: 1.2 }
  .wlc-lbl { color: var(--secondary-text-color); font-size: .85rem }

  .wlc-boost-container {
    margin-top: 14px;
  }
  .wlc-btn {
    display: flex; align-items: center; width: 100%;
    padding: 10px 14px; border-radius: 12px;
    background: rgba(127, 127, 127, 0.04);
    border: 1px solid var(--divider-color, rgba(127, 127, 127, 0.15));
    cursor: pointer; text-align: left; transition: all 0.2s ease;
    box-sizing: border-box;
  }
  .wlc-btn:hover {
    background: rgba(127, 127, 127, 0.08);
  }
  .wlc-btn-icon {
    font-size: 1.3rem; margin-right: 12px;
  }
  .wlc-btn-text {
    flex: 1; display: flex; flex-direction: column;
  }
  .wlc-btn-title {
    font-weight: 500; font-size: 0.95rem;
    color: var(--primary-text-color);
  }
  .wlc-btn-sub {
    font-size: 0.8rem; color: var(--secondary-text-color);
  }
  .wlc-indicator {
    width: 10px; height: 10px; border-radius: 50%;
    background-color: var(--disabled-text-color, #9e9e9e);
    transition: background-color 0.2s ease, box-shadow 0.2s ease;
  }
  .wlc-btn.active {
    border-color: var(--label-badge-green, #4caf50);
    background: rgba(76, 175, 80, 0.06);
  }
  .wlc-btn.active .wlc-indicator {
    background-color: var(--label-badge-green, #4caf50);
    box-shadow: 0 0 6px var(--label-badge-green, #4caf50);
  }

  .wlc-hr {
    border-top: 1px solid var(--divider-color, #e0e0e0);
    margin: 14px 0 10px;
  }
  .wlc-hdr {
    font-size: .8rem; font-weight: 600;
    text-transform: uppercase; letter-spacing: .5px;
    color: var(--secondary-text-color); margin-bottom: 8px;
  }

  .wlc-st {
    display: flex; align-items: center; gap: 8px;
    padding: 10px 14px; border-radius: 12px;
    font-weight: 500; font-size: .95rem;
  }
  .wlc-ok {
    background: rgba(76, 175, 80, .1);
    color: var(--label-badge-green, #4caf50);
  }
  .wlc-na {
    background: rgba(158, 158, 158, .1);
    color: var(--secondary-text-color);
  }
  .wlc-warn {
    background: rgba(255, 152, 0, .1);
    color: var(--warning-color, #ff9800);
  }

  .wlc-act {
    display: flex; align-items: center; gap: 10px;
    padding: 10px 14px; border-radius: 12px;
    margin-bottom: 6px;
    transition: background 0.2s ease, color 0.2s ease;
  }
  .wlc-act:last-child { margin-bottom: 0 }
  
  .wlc-act.warn {
    background: rgba(255, 152, 0, .08);
  }
  .wlc-act.warn .wlc-dot,
  .wlc-act.warn .wlc-av {
    color: var(--warning-color, #ff9800);
  }

  .wlc-act.ok {
    background: rgba(76, 175, 80, .08);
  }
  .wlc-act.ok .wlc-dot,
  .wlc-act.ok .wlc-av {
    color: var(--label-badge-green, #4caf50);
  }

  .wlc-dot {
    font-size: 1rem; flex-shrink: 0; font-weight: bold;
  }
  .wlc-al {
    flex: 1; font-weight: 500;
    color: var(--primary-text-color);
  }
  .wlc-av {
    font-weight: 600;
    font-size: 1.05rem; white-space: nowrap;
  }

  .wlc-msg {
    text-align: center; padding: 24px;
    color: var(--secondary-text-color); font-size: .95rem;
  }
`;

customElements.define("wowa-level-card", WowaLevelCard);