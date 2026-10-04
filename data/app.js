'use strict';

// ─── Constantes de protocolo (espelham WSCommands.h) ─────────────────────────
const CMD = {
  HEARTBEAT:   'heartbeat',
  START:       'start',
  STOP:        'stop',
  SET_SPEED:   'setSpeed',
  SET_INCLINE: 'setIncline',
  GET_STATUS:  'getStatus',
  HOMING:      'homing',
  CLEAR_FAULT: 'clearFault',
  GET_PID:     'getPid',
  SET_PID:     'setPid',
};

const TYPE = {
  STATUS:        'status',
  CONNECTED:     'connected',
  ERROR:         'error',
  INFO:          'info',
  HEARTBEAT_ACK: 'heartbeatAck',
  PID_CONFIG:    'pidConfig',
};

// ─── Mensagens de falha (espelham FaultCode em Motor.h) ──────────────────────
const FAULT_MESSAGES = {
  0: '',
  1: 'F01 — Perda de comunicação (heartbeat)',
  2: 'F02 — Encoder óptico sem sinal (malha fechada)',
};

// ─── Endereço base: segue a origem da página (IP ou mDNS, STA ou AP) ─────────
const ESP_WS  = `${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws`;
const ESP_API = '';   // URLs relativas — funciona em qualquer origem

// ─── Beeper (Web Audio API) ───────────────────────────────────────────────────
class Beeper {
  constructor() { this._actx = null; }
  _getCtx() {
    if (!this._actx) this._actx = new (window.AudioContext || window.webkitAudioContext)();
    if (this._actx.state === 'suspended') this._actx.resume();
    return this._actx;
  }
  _tone(freq, ms, vol = 0.3) {
    try {
      const ctx  = this._getCtx();
      const osc  = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.connect(gain); gain.connect(ctx.destination);
      osc.type = 'sine'; osc.frequency.value = freq;
      gain.gain.setValueAtTime(vol, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + ms / 1000);
      osc.start(); osc.stop(ctx.currentTime + ms / 1000);
    } catch {}
  }
  btn()  { this._tone(1500,  40, 0.28); }  // clique +/-
  tick() { this._tone(1500,  60, 0.35); }  // contagem regressiva
  go()   {                                  // largada — dois bips rápidos
    this._tone(1500, 80, 0.4);
    setTimeout(() => this._tone(1500, 80, 0.4), 130);
  }
}

// ─── Gauge SVG arc helper ─────────────────────────────────────────────────────
class GaugeArc {
  constructor(fillEl, totalLength) {
    this._el     = fillEl;
    this._len    = totalLength;
    this._el.style.strokeDasharray  = `${this._len}`;
    this._el.style.strokeDashoffset = `${this._len}`;
  }
  setValue(fraction) {
    fraction = Math.max(0, Math.min(1, fraction));
    const offset = this._len * (1 - fraction);
    this._el.style.strokeDashoffset = `${offset}`;
  }
}

// ─── Main App ─────────────────────────────────────────────────────────────────
class TreadmillApp {
  constructor() {
    this.ws              = null;
    this.connected       = false;
    this.running         = false;
    this.openLoop        = true;
    this.homingDone      = false;
    this.reconnectDelay  = 1000;
    this._hbTimer        = null;
    this._statusTimer    = null;

    this.targetSpeed     = 0;
    this.targetIncline   = 0;
    this.maxSpeed        = 12;
    this._countdown      = null;
    this.beeper          = new Beeper();

    // DOM refs
    this.$connDot        = document.getElementById('connDot');
    this.$connLabel      = document.getElementById('connLabel');
    this.$gaugeText      = document.getElementById('gaugeText');
    this.$gaugeMax       = document.getElementById('gaugeMax');
    this.$speedSlider    = document.getElementById('speedSlider');
    this.$targetSpeed    = document.getElementById('targetSpeedDisplay');
    this.$inclineBar     = document.getElementById('inclineBar');
    this.$inclineDisplay = document.getElementById('inclineDisplay');
    this.$targetIncline  = document.getElementById('targetInclineDisplay');
    this.$startBtn       = document.getElementById('startBtn');
    this.$modeBadge      = document.getElementById('modeBadge');
    this.$homingStatus   = document.getElementById('homingStatus');
    this.$faultBanner    = document.getElementById('faultBanner');
    this.$faultMsg       = document.getElementById('faultMsg');
    this.$statSpeed      = document.getElementById('statSpeed');
    this.$statRPM        = document.getElementById('statRPM');
    this.$statPWM        = document.getElementById('statPWM');
    this.$dbgPulses      = document.getElementById('dbgPulses');   // DEBUG
    this.$dbgPeriod      = document.getElementById('dbgPeriod');   // DEBUG
    this.$pidKp          = document.getElementById('pidKp');
    this.$pidKi          = document.getElementById('pidKi');
    this.$pidKd          = document.getElementById('pidKd');
    this.$infoIP         = document.getElementById('infoIP');
    this.$infoMode       = document.getElementById('infoMode');
    this.$toast          = document.getElementById('toast');

    // Gauge arc — arco de 240° com r=76 → 240/360 * 2π*76 ≈ 318.35
    const ARC_LENGTH = (240 / 360) * 2 * Math.PI * 76;
    this.gauge = new GaugeArc(document.getElementById('gaugeFill'), ARC_LENGTH);

    this._bindUI();
    this.connect();
    this._pollNetworkInfo();
  }

  // ── WebSocket ───────────────────────────────────────────────────────────────
  connect() {
    const url   = ESP_WS;

    this._setConnState('connecting');
    try {
      this.ws = new WebSocket(url);
    } catch (e) {
      this._scheduleReconnect();
      return;
    }

    this.ws.onopen    = () => this._onOpen();
    this.ws.onclose   = () => this._onClose();
    this.ws.onerror   = () => {};   // onclose sempre dispara após onerror
    this.ws.onmessage = (ev) => this._onMessage(ev);
  }

  _onOpen() {
    // WS aberto não significa ESP respondendo — aguarda primeira mensagem válida
    this.reconnectDelay = 1000;
    this._setConnState('connecting');
    this._startHeartbeat();
    this.send(CMD.GET_STATUS);
  }

  _onClose() {
    this.connected = false;
    this._stopHeartbeat();
    this._setConnState('disconnected');
    this._disableControls();
    this._scheduleReconnect();
  }



  _scheduleReconnect() {
    setTimeout(() => this.connect(), this.reconnectDelay);
    this.reconnectDelay = Math.min(this.reconnectDelay * 1.5, 15000);
  }

  _startHeartbeat() {
    this._stopHeartbeat();
    // Envia heartbeat a cada 1000ms (timeout no ESP é 3000ms)
    this._hbTimer = setInterval(() => {
      this.send(CMD.HEARTBEAT);   // send() verifica readyState
    }, 1000);
  }

  _stopHeartbeat() {
    if (this._hbTimer) { clearInterval(this._hbTimer); this._hbTimer = null; }
  }

  send(cmd, value) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
    const msg = { cmd };
    if (value !== undefined) msg.value = value;
    this.ws.send(JSON.stringify(msg));
  }

  _onMessage(ev) {
    let data;
    try { data = JSON.parse(ev.data); } catch { return; }

    switch (data.type) {
      case TYPE.STATUS:        this._handleStatus(data);    break;
      case TYPE.CONNECTED:     this._handleConnected(data); break;
      case TYPE.PID_CONFIG:    this._handlePidConfig(data); break;
      case TYPE.ERROR:         this._showToast(data.message, 'error'); break;
      case TYPE.INFO:          this._showToast(data.message); break;
      case TYPE.HEARTBEAT_ACK: break;
    }
  }

  _handleConnected(data) {
    this.connected = true;
    this._setConnState('connected');
    this.openLoop = !!data.openLoop;
    this._updateModeBadge();
    this.$startBtn.disabled = false;
  }

  _handleStatus(data) {
    if (!this.connected) {
      this.connected = true;
      this._setConnState('connected');
    }
    this.running    = !!data.running;
    this.homingDone = !!data.homingDone;

    const speed    = parseFloat(data.speed)  || 0;
    const rpm      = parseFloat(data.rpm)    || 0;
    const pwm      = parseFloat(data.pwmPct) || 0;
    const incline  = parseFloat(data.incline)       || 0;
    const tInc     = parseFloat(data.targetIncline) || 0;
    const tSpd     = parseFloat(data.targetSpeed)   || 0;

    // Gauge
    this.gauge.setValue(speed / this.maxSpeed);
    this.$gaugeText.textContent = speed.toFixed(1);

    // Stats
    this.$statSpeed.textContent = speed.toFixed(1);
    this.$statRPM.textContent   = Math.round(rpm);
    this.$statPWM.textContent   = pwm.toFixed(1);

    // DEBUG encoder
    if (data.dbgPulses !== undefined) {
      this.$dbgPulses.textContent = data.dbgPulses;
      // período em µs → mostra em µs ou ms conforme magnitude
      const per = parseFloat(data.dbgPeriodUs) || 0;
      this.$dbgPeriod.textContent = per > 0
        ? (per >= 1000 ? (per / 1000).toFixed(1) + ' ms' : per + ' µs')
        : '—';
    }

    // Incline — sincroniza variável local com o servidor
    this.targetIncline               = tInc;
    this.$inclineDisplay.textContent = incline.toFixed(1);
    this.$targetIncline.textContent  = tInc.toFixed(1);
    this.$inclineBar.style.width     = `${(incline / 12) * 100}%`;

    // Target speed display (não mexe no slider se está arrastando)
    if (document.activeElement !== this.$speedSlider) {
      this.$speedSlider.value         = tSpd;
    }
    this.$targetSpeed.textContent = tSpd.toFixed(1);

    // Start button — bloqueado em falha ativa ou sem conexão
    const faultActive = (data.fault || 0) !== 0;
    if (!this._countdown) {
      this.$startBtn.textContent = this.running ? 'PARAR' : 'INICIAR';
      this.$startBtn.classList.toggle('running', this.running);
      this.$startBtn.classList.remove('countdown');
    }
    this.$startBtn.disabled = !this.connected || faultActive;

    // Homing status
    this.$homingStatus.textContent = this.homingDone ? '' : '⚠ Homing necessário';

    // Fault banner
    const fault = data.fault || 0;
    if (fault !== 0) {
      this.$faultMsg.textContent = FAULT_MESSAGES[fault] || `F${String(fault).padStart(2,'0')} — Falha desconhecida`;
      this.$faultBanner.classList.remove('hidden');
    } else {
      this.$faultBanner.classList.add('hidden');
    }
  }

  _handlePidConfig(data) {
    if (data.kp !== undefined) this.$pidKp.value = parseFloat(data.kp).toFixed(3);
    if (data.ki !== undefined) this.$pidKi.value = parseFloat(data.ki).toFixed(4);
    if (data.kd !== undefined) this.$pidKd.value = parseFloat(data.kd).toFixed(4);
  }

  // ── UI bindings ─────────────────────────────────────────────────────────────
  _bindUI() {
    // Tabs
    document.querySelectorAll('.tab-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
        document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
        btn.classList.add('active');
        document.getElementById(`tab-${btn.dataset.tab}`).classList.add('active');
      });
    });

    // Start/Stop com contagem regressiva
    this.$startBtn.addEventListener('click', () => {
      if (this._countdown) { this._cancelCountdown(); return; }
      if (this.running)    { this.send(CMD.STOP);     return; }
      this._startCountdown();
    });

    // Speed slider
    this.$speedSlider.addEventListener('input', () => {
      const v = parseFloat(this.$speedSlider.value);
      this.$targetSpeed.textContent = v.toFixed(1);
      this.targetSpeed = v;
      this.send(CMD.SET_SPEED, v);
    });

    // Speed +/-
    document.getElementById('btnSpeedUp').addEventListener('click', () => {
      const v = Math.min(parseFloat(this.$speedSlider.value) + 0.5, this.maxSpeed);
      this.$speedSlider.value = v;
      this.targetSpeed = v;
      this.$targetSpeed.textContent = v.toFixed(1);
      this.send(CMD.SET_SPEED, v);
      this.beeper.btn();
    });
    document.getElementById('btnSpeedDown').addEventListener('click', () => {
      const v = Math.max(parseFloat(this.$speedSlider.value) - 0.5, 0);
      this.$speedSlider.value = v;
      this.targetSpeed = v;
      this.$targetSpeed.textContent = v.toFixed(1);
      this.send(CMD.SET_SPEED, v);
      this.beeper.btn();
    });

    // Incline
    document.getElementById('btnInclineUp').addEventListener('click', () => {
      this.targetIncline = Math.min(this.targetIncline + 1, 12);
      this.send(CMD.SET_INCLINE, this.targetIncline);
    });
    document.getElementById('btnInclineDown').addEventListener('click', () => {
      this.targetIncline = Math.max(this.targetIncline - 1, 0);
      this.send(CMD.SET_INCLINE, this.targetIncline);
    });
    document.getElementById('btnHoming').addEventListener('click', () => {
      this.send(CMD.HOMING);
      this.targetIncline = 0;
    });

    document.getElementById('btnClearFault').addEventListener('click', () => {
      this.send(CMD.CLEAR_FAULT);
    });

    // WiFi save
    document.getElementById('btnSaveWifi').addEventListener('click', () => {
      const ssid = document.getElementById('wifiSSID').value.trim();
      const pass = document.getElementById('wifiPass').value;
      if (!ssid) { this._showToast('Informe o SSID', 'error'); return; }
      fetch(`${ESP_API}/api/wifi`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid, password: pass })
      }).then(r => r.json()).then(() => {
        this._showToast('Credenciais salvas — reconectando…', 'success');
      }).catch(() => this._showToast('Erro ao salvar', 'error'));
    });

    document.getElementById('btnClearWifi').addEventListener('click', () => {
      if (!confirm('Apagar credenciais WiFi salvas?')) return;
      fetch(`${ESP_API}/api/wifi`, { method: 'DELETE' })
        .then(() => this._showToast('Credenciais apagadas', 'success'))
        .catch(() => this._showToast('Erro', 'error'));
    });

    // PID save
    document.getElementById('btnSavePid').addEventListener('click', () => {
      const kp = parseFloat(this.$pidKp.value);
      const ki = parseFloat(this.$pidKi.value);
      const kd = parseFloat(this.$pidKd.value);
      if ([kp, ki, kd].some(v => isNaN(v) || v < 0)) {
        this._showToast('Valores inválidos', 'error');
        return;
      }
      const msg = { cmd: CMD.SET_PID, kp, ki, kd };
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
        this._showToast('Sem conexão', 'error');
        return;
      }
      this.ws.send(JSON.stringify(msg));
      this._showToast(`PID salvo — Kp=${kp} Ki=${ki} Kd=${kd}`, 'success');
    });
  }

  // ── Contagem regressiva ──────────────────────────────────────────────────────
  _startCountdown() {
    const btn = this.$startBtn;
    let n = 3;
    const tick = () => {
      if (n === 0) {
        this._countdown = null;
        btn.classList.remove('countdown');
        this.send(CMD.START);
        this.beeper.go();
        return;
      }
      btn.textContent = String(n);
      btn.classList.add('countdown');
      this.beeper.tick();
      n--;
      this._countdown = setTimeout(tick, 1000);
    };
    tick();
  }

  _cancelCountdown() {
    if (this._countdown) { clearTimeout(this._countdown); this._countdown = null; }
    this.$startBtn.textContent = 'INICIAR';
    this.$startBtn.classList.remove('running', 'countdown');
    this.$startBtn.disabled = !this.connected;
  }

  // ── Helpers ─────────────────────────────────────────────────────────────────
  _setConnState(state) {
    const dot = this.$connDot;
    dot.className = `conn-dot ${state}`;
    const labels = { connected: 'Conectado', disconnected: 'Desconectado', connecting: 'Conectando…' };
    this.$connLabel.textContent = labels[state] || state;
  }

  _disableControls() {
    this._cancelCountdown();
    this.$startBtn.disabled = true;
  }

  _updateModeBadge() {
    if (this.openLoop) {
      this.$modeBadge.textContent = 'MALHA ABERTA';
      this.$modeBadge.className   = 'mode-badge';
    } else {
      this.$modeBadge.textContent = 'MALHA FECHADA (PID)';
      this.$modeBadge.className   = 'mode-badge closed';
    }
  }

  _showToast(msg, type = '') {
    const t = this.$toast;
    t.textContent = msg;
    t.className   = `toast ${type} show`;
    clearTimeout(this._toastTimer);
    this._toastTimer = setTimeout(() => t.classList.remove('show'), 3000);
  }

  _pollNetworkInfo() {
    const refresh = () => {
      fetch(`${ESP_API}/api/status`)
        .then(r => r.json())
        .then(d => {
          this.$infoIP.textContent   = d.ip   || '—';
          this.$infoMode.textContent = d.mode || '—';
        })
        .catch(() => {});
    };
    refresh();
    setInterval(refresh, 10000);
  }
}

// Inicializa ao carregar a página
window.addEventListener('DOMContentLoaded', () => { window._app = new TreadmillApp(); });
