'use strict';

// ─── Constantes de protocolo (espelham WSCommands.h) ─────────────────────────
const CMD = {
  HEARTBEAT:    'heartbeat',
  START:        'start',
  STOP:         'stop',
  SET_SPEED:    'setSpeed',
  SET_INCLINE:  'setIncline',
  GET_STATUS:   'getStatus',
  HOMING:       'homing',
  CLEAR_FAULT:  'clearFault',
  GET_PID:      'getPid',
  SET_PID_SPD:  'setPidSpd',
  SET_PID_CUR:  'setPidCur',
  SET_FCEM_MAX: 'setFcemMax',
  SET_SCREEN:   'setScreen',
  SET_MODE:     'setMode',
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
  3: 'F03 — Sobrecorrente instantânea (curto/falha)',
  4: 'F04 — Sobrecorrente sustentada (jam mecânico/sobrecarga)',
  5: 'F05 — Sobrevelocidade (teto absoluto excedido)',
  6: 'F06 — Sobrevelocidade (fuga do alvo — carga removida?)',
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
    this.$dbgVMot        = document.getElementById('dbgVMot');   // DEBUG — rodapé FCEM
    this.$dbgIA          = document.getElementById('dbgIA');     // DEBUG
    this.$dbgFcem        = document.getElementById('dbgFcem');   // DEBUG
    this.$pidSpdKp       = document.getElementById('pidSpdKp');
    this.$pidSpdKi       = document.getElementById('pidSpdKi');
    this.$pidSpdKd       = document.getElementById('pidSpdKd');
    this.$pidCurKp       = document.getElementById('pidCurKp');
    this.$pidCurKi       = document.getElementById('pidCurKi');
    this.$pidCurKd       = document.getElementById('pidCurKd');
    this.$fcemMaxV       = document.getElementById('fcemMaxV');
    this.$btnModeOpen    = document.getElementById('btnModeOpen');
    this.$btnModeClosed  = document.getElementById('btnModeClosed');
    this.$infoIP         = document.getElementById('infoIP');
    this.$infoMode       = document.getElementById('infoMode');
    this.$toast          = document.getElementById('toast');

    // Manutenção — telemetria elétrica sensorless (só atualizada nesta aba)
    this.$maintVBus      = document.getElementById('maintVBus');
    this.$maintVBusRaw   = document.getElementById('maintVBusRaw');
    this.$maintVMot      = document.getElementById('maintVMot');
    this.$maintVMotRaw   = document.getElementById('maintVMotRaw');
    this.$maintIA        = document.getElementById('maintIA');
    this.$maintICurRaw   = document.getElementById('maintICurRaw');
    this.$maintFcem      = document.getElementById('maintFcem');

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
    this._updateModeToggle();
    this.$startBtn.disabled = false;

    // Sincroniza a tela ativa com o backend (ex: reconexão já dentro da aba de Manutenção)
    const activeTab = document.querySelector('.tab-btn.active');
    this.send(CMD.SET_SCREEN, (activeTab && activeTab.dataset.tab === 'maintenance') ? 'maintenance' : 'dashboard');
  }

  _handleStatus(data) {
    if (!this.connected) {
      this.connected = true;
      this._setConnState('connected');
    }
    this.running    = !!data.running;
    this.homingDone = !!data.homingDone;

    const openLoop = !!data.openLoop;
    if (openLoop !== this.openLoop) {
      this.openLoop = openLoop;
      this._updateModeBadge();
    }
    this._updateModeToggle();

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

    // DEBUG FCEM — rodapé do dashboard principal (fase de testes da cascata
    // sensorless): V_mot/I_a/FCEM vêm sempre no payload, independente da aba ativa.
    if (data.vMot !== undefined) {
      const vMot = parseFloat(data.vMot) || 0;
      const iA   = parseFloat(data.iA)   || 0;
      const fcem = parseFloat(data.fcem) || 0;

      this.$dbgVMot.textContent = vMot.toFixed(1);
      this.$dbgIA.textContent   = iA.toFixed(2);
      this.$dbgFcem.textContent = fcem.toFixed(1);

      this.$maintVMot.textContent = vMot.toFixed(1);
      this.$maintIA.textContent   = iA.toFixed(2);
      this.$maintFcem.textContent = fcem.toFixed(1);
    }

    // Manutenção — V_bus + leituras brutas (só vêm no payload se a aba de
    // Manutenção estiver ativa, ver _bindUI/setScreen)
    if (data.vBus !== undefined) {
      this.$maintVBus.textContent    = parseFloat(data.vBus).toFixed(1);
      this.$maintVBusRaw.textContent = parseFloat(data.vBusRaw).toFixed(3);
      this.$maintVMotRaw.textContent = parseFloat(data.vMotRaw).toFixed(3);
      this.$maintICurRaw.textContent = parseFloat(data.vCurRaw).toFixed(3);
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
    if (data.spdKp !== undefined) this.$pidSpdKp.value = parseFloat(data.spdKp).toFixed(3);
    if (data.spdKi !== undefined) this.$pidSpdKi.value = parseFloat(data.spdKi).toFixed(4);
    if (data.spdKd !== undefined) this.$pidSpdKd.value = parseFloat(data.spdKd).toFixed(4);
    if (data.curKp !== undefined) this.$pidCurKp.value = parseFloat(data.curKp).toFixed(3);
    if (data.curKi !== undefined) this.$pidCurKi.value = parseFloat(data.curKi).toFixed(4);
    if (data.curKd !== undefined) this.$pidCurKd.value = parseFloat(data.curKd).toFixed(4);
    if (data.fcemMaxV !== undefined) this.$fcemMaxV.value = parseFloat(data.fcemMaxV).toFixed(1);
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
        // Avisa o firmware se a telemetria elétrica extra deve ser enviada
        // (economiza banda/processamento fora da tela de Manutenção)
        this.send(CMD.SET_SCREEN, btn.dataset.tab === 'maintenance' ? 'maintenance' : 'dashboard');
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

    // Modo de controle (malha aberta/fechada) — TEMPORÁRIO para testes
    this.$btnModeOpen.addEventListener('click', () => {
      if (this.running || this.openLoop) return;
      this.send(CMD.SET_MODE, true);
    });
    this.$btnModeClosed.addEventListener('click', () => {
      if (this.running || !this.openLoop) return;
      this.send(CMD.SET_MODE, false);
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

    // PID save — malha de velocidade/FCEM
    document.getElementById('btnSavePidSpd').addEventListener('click', () => {
      const kp = parseFloat(this.$pidSpdKp.value);
      const ki = parseFloat(this.$pidSpdKi.value);
      const kd = parseFloat(this.$pidSpdKd.value);
      if ([kp, ki, kd].some(v => isNaN(v) || v < 0)) {
        this._showToast('Valores inválidos', 'error');
        return;
      }
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
        this._showToast('Sem conexão', 'error');
        return;
      }
      this.ws.send(JSON.stringify({ cmd: CMD.SET_PID_SPD, kp, ki, kd }));
      this._showToast(`Malha de velocidade salva — Kp=${kp} Ki=${ki} Kd=${kd}`, 'success');
    });

    // PID save — malha de corrente
    document.getElementById('btnSavePidCur').addEventListener('click', () => {
      const kp = parseFloat(this.$pidCurKp.value);
      const ki = parseFloat(this.$pidCurKi.value);
      const kd = parseFloat(this.$pidCurKd.value);
      if ([kp, ki, kd].some(v => isNaN(v) || v < 0)) {
        this._showToast('Valores inválidos', 'error');
        return;
      }
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
        this._showToast('Sem conexão', 'error');
        return;
      }
      this.ws.send(JSON.stringify({ cmd: CMD.SET_PID_CUR, kp, ki, kd }));
      this._showToast(`Malha de corrente salva — Kp=${kp} Ki=${ki} Kd=${kd}`, 'success');
    });

    // FCEM máxima (Ke sensorless)
    document.getElementById('btnSaveFcemMax').addEventListener('click', () => {
      const value = parseFloat(this.$fcemMaxV.value);
      if (isNaN(value) || value < 0) {
        this._showToast('Valor inválido', 'error');
        return;
      }
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
        this._showToast('Sem conexão', 'error');
        return;
      }
      this.ws.send(JSON.stringify({ cmd: CMD.SET_FCEM_MAX, value }));
      this._showToast(`FCEM máxima salva — ${value}V`, 'success');
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

  _updateModeToggle() {
    this.$btnModeOpen.classList.toggle('active', this.openLoop);
    this.$btnModeClosed.classList.toggle('active', !this.openLoop);
    this.$btnModeOpen.disabled   = this.running;
    this.$btnModeClosed.disabled = this.running;
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
