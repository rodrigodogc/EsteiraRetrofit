#include "Motor.h"

Motor::Motor(SpeedSensor& sensor)
    : _sensor(sensor),
      _pidSpd(&_pidSpdInput, &_pidSpdOutput, &_pidSpdSetpoint,
              PID_SPD_KP, PID_SPD_KI, PID_SPD_KD, DIRECT),
      _pidCur(&_pidCurInput, &_pidCurOutput, &_pidCurSetpoint,
              PID_CUR_KP, PID_CUR_KI, PID_CUR_KD, DIRECT) {}

void Motor::begin() {
    // Paridade com o sketch de referência (Measurements.ino) — garante que os
    // pinos não fiquem com pull-up/pull-down residual de um estado anterior
    // antes da primeira leitura ADC.
    pinMode(PINO_VBUS, INPUT);
    pinMode(PINO_VMOT, INPUT);
    pinMode(PINO_CURR, INPUT);

    ledcSetup(MOTOR_PWM_CHANNEL, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_RESOLUTION);
    ledcAttachPin(MOTOR_PWM_PIN, MOTOR_PWM_CHANNEL);
    ledcWrite(MOTOR_PWM_CHANNEL, 0);

    _loadTuningFromNvs();   // carrega os 7 valores de tuning da NVS (ou defaults de Config.h)

    // Malha externa: FCEM medida → I_ref (corrente de referência, nunca negativa)
    _pidSpd.SetOutputLimits(0.0f, MAX_ARMATURE_CURRENT_A);
    _pidSpd.SetTunings(_spdKp, _spdKi, _spdKd);
    _pidSpd.SetSampleTime(PID_SPD_INTERVAL_MS);
    _pidSpd.SetMode(MANUAL);   // habilitado só ao cruzar a zona cega (ver update())

    // Malha interna: I_a medida → duty-cycle do PWM
    _pidCur.SetOutputLimits(0.0f, MAX_PWM_PERCENT);
    _pidCur.SetTunings(_curKp, _curKi, _curKd);
    _pidCur.SetSampleTime(PID_CUR_INTERVAL_MS);
    _pidCur.SetMode(MANUAL);

    _currentPwmPct   = 0.0f;
    _targetPwmPct    = 0.0f;
    _running         = false;
    _cascadeActive   = false;
    _lastHeartbeatMs = millis();   // carência inicial para cliente conectar
}

void Motor::start() {
    if (_running) return;
    clearFault();

    // Garante velocidade mínima ao iniciar
    if (_targetSpeedKmh < MIN_SPEED_KMH) {
        _targetSpeedKmh = MIN_SPEED_KMH;
    }

    _running = true;
    resetHeartbeat();   // carência para o cliente enviar o primeiro heartbeat

    // Reinicia os dois PIDs sem estado acumulado de uma sessão anterior
    _pidSpd.SetMode(MANUAL);
    _pidSpdOutput = 0.0;
    _pidCur.SetMode(MANUAL);
    _pidCurOutput = 0.0;
    _cascadeActive = false;

    _rampedSetpoint = 0.0f;   // sempre sobe do zero — rampa no setpoint controla aceleração
    _sensor.reset();
    Serial.println("[Motor] Start");
}

void Motor::stop() {
    if (_openLoop) {
        _running        = false;
        _activeRampDown = RAMP_RATE_DOWN;
        _targetPwmPct   = 0.0f;
        _rampedSetpoint = 0.0f;
        _sensor.reset();
        Serial.println("[Motor] Stop");
    } else {
        // Malha fechada: mantém _running=true e desacelera via rampedSetpoint → 0
        // O PID rastreia a queda suavemente; update() corta ao rampedSetpoint chegar a zero
        _targetSpeedKmh = 0.0f;
        _stopping       = true;
        Serial.println("[Motor] Stop (controlled decel)");
    }
}

bool Motor::setOpenLoop(bool openLoop) {
    if (_running) return false;   // não troca de malha com o motor rodando
    _openLoop = openLoop;
    Serial.printf("[Motor] Modo alterado — %s\n", _openLoop ? "malha aberta" : "malha fechada (cascata sensorless)");
    return true;
}

void Motor::emergencyStop() {
    _stopping       = false;
    _running        = false;
    _activeRampDown = RAMP_RATE_EMERGENCY;
    _targetPwmPct   = 0.0f;
    _rampedSetpoint = 0.0f;
    _sensor.reset();
    Serial.println("[Motor] EMERGENCY STOP");
}

void Motor::faultStop(FaultCode code) {
    _fault = code;
    emergencyStop();
    Serial.printf("[Motor] FAULT STOP — code %u\n", (uint8_t)code);
}

void Motor::setTargetSpeed(float kmh) {
    // Abaixo do mínimo mas acima de zero → sobe para mínimo (evita velocidades inoperáveis)
    if (kmh > 0.0f && kmh < MIN_SPEED_KMH) {
        kmh = MIN_SPEED_KMH;
    }
    _targetSpeedKmh = constrain(kmh, 0.0f, MAX_SPEED_KMH);
}

void Motor::resetHeartbeat() {
    _lastHeartbeatMs = millis();
}

bool Motor::isHeartbeatOk() const {
    return (millis() - _lastHeartbeatMs) < WS_HEARTBEAT_TIMEOUT_MS;
}

void Motor::update() {
    unsigned long now = millis();

    // ── Amostragem elétrica sensorless — sempre viva, cadência própria (~100Hz) ─
    // Desacoplada do gate de controle abaixo: a constante de tempo do filtro EMA
    // (que alimenta FCEM e a proteção de sobrecorrente) não deve depender de
    // eventuais mudanças futuras na cadência do laço de controle.
    if (now - _lastAdcSampleMs >= ADC_SAMPLE_INTERVAL_MS) {
        _lastAdcSampleMs = now;
        _sampleElectricalSignals();
        _checkOvercurrent();   // sempre ativo, independente de _running/_openLoop
        _checkOverspeed();     // teto absoluto sempre ativo; fuga do alvo só em malha fechada rodando
    }

    float dt = (now - _lastUpdateMs) / 1000.0f;
    if (dt < 0.01f) return;
    _lastUpdateMs = now;

    // Heartbeat centralizado — qualquer interface (WS ou BLE) pode resetar o timer
    if (_running && !isHeartbeatOk()) {
        Serial.println("[Motor] Heartbeat timeout — fault stop");
        faultStop(FaultCode::HEARTBEAT_TIMEOUT);
        return;
    }

    // Tunings pendentes (setados pela task assíncrona do WS/BLE) aplicados aqui,
    // de forma síncrona na task loop() — evita escrita cross-task nos internals
    // de 64 bits do PID_v1.
    if (_spdTuningDirty) {
        _pidSpd.SetTunings(_spdKp, _spdKi, _spdKd);
        _spdTuningDirty = false;
    }
    if (_curTuningDirty) {
        _pidCur.SetTunings(_curKp, _curKi, _curKd);
        _curTuningDirty = false;
    }

    if (_running) {
        if (_openLoop) {
            // Malha aberta: converte velocidade alvo → duty-cycle; rampa de PWM abaixo cuida da subida
            _targetPwmPct = speedToPwmPct(_targetSpeedKmh);
        } else {
            // Desaceleração controlada: rampedSetpoint chegou a zero → corta motor
            if (_stopping && _rampedSetpoint <= 0.001f) {
                _stopping      = false;
                _running       = false;
                _currentPwmPct = 0.0f;
                applyPwmPct(0.0f);
                _sensor.reset();
                Serial.println("[Motor] Stopped");
                return;
            }

            // Rampa no setpoint de velocidade (km/h/s) — cascata controla PWM livremente sem conflito
            // Taxa derivada das constantes de PWM pela relação linear velocidade/duty-cycle
            constexpr float kScale = MAX_SPEED_KMH / MAX_PWM_PERCENT;
            const float rampRate = (_rampedSetpoint <= _targetSpeedKmh)
                                   ? RAMP_RATE_UP   * kScale   // aceleração
                                   : RAMP_RATE_DOWN * kScale;  // desaceleração de ajuste

            if (_rampedSetpoint < _targetSpeedKmh) {
                _rampedSetpoint += rampRate * dt;
                if (_rampedSetpoint > _targetSpeedKmh) _rampedSetpoint = _targetSpeedKmh;
            } else if (_rampedSetpoint > _targetSpeedKmh) {
                _rampedSetpoint -= rampRate * dt;
                if (_rampedSetpoint < _targetSpeedKmh) _rampedSetpoint = _targetSpeedKmh;
            }

            if (_rampedSetpoint < FCEM_BLIND_ZONE_KMH) {
                // Zona cega: perto de zero a FCEM estimada (V_mot - I_a*R_a) é
                // uma subtração de duas medições ruidosas — não confiável para
                // fechar malha. Usa feedforward puro, igual à malha aberta.
                _targetPwmPct = speedToPwmPct(_rampedSetpoint);
                _pidCurOutput = _targetPwmPct;   // pré-carga para handoff bumpless
                if (_cascadeActive) {
                    _pidSpd.SetMode(MANUAL);
                    _pidCur.SetMode(MANUAL);
                    _cascadeActive = false;
                }
            } else {
                if (!_cascadeActive) {
                    // Handoff bumpless: _pidCurOutput já está pré-carregado com o
                    // feedforward do ciclo anterior — Initialize() do PID_v1 usa
                    // esse valor como ponto de partida do integrador.
                    _pidSpd.SetMode(AUTOMATIC);
                    _pidCur.SetMode(AUTOMATIC);
                    _cascadeActive = true;
                }

                // Malha externa: FCEM alvo (derivada do setpoint rampado) → I_ref
                float fcemTarget = (_rampedSetpoint / MAX_SPEED_KMH) * _fcemMaxV;
                _pidSpdSetpoint = fcemTarget;
                _pidSpdInput    = _fcem;
                if (_pidSpd.Compute()) {
                    _pidCurSetpoint = _pidSpdOutput;   // I_ref, já clampado por SetOutputLimits
                }

                // Malha interna: I_a → duty%
                _pidCurInput = _iA;
                if (_pidCur.Compute()) {
                    _targetPwmPct = (float)_pidCurOutput;   // já clampado por SetOutputLimits
                }
            }
        }
        // Feedforward de compensação de barramento — aplicado a qualquer uma das
        // 3 fontes de duty% acima (malha aberta, zona cega, ou PID de corrente),
        // antes do clamp final de segurança.
        _targetPwmPct = _compensateBusVoltage(_targetPwmPct);
        _targetPwmPct = constrain(_targetPwmPct, 0.0f, MAX_PWM_PERCENT);
    } else {
        _targetPwmPct = 0.0f;
    }

    // ── Aplicação de PWM ─────────────────────────────────────────────────────
    // Malha aberta : rampa de PWM na subida e na descida
    // Malha fechada: cascata aplica direto quando rodando; rampa só ao parar (stop/emergency)
    if (_openLoop) {
        if (_currentPwmPct < _targetPwmPct) {
            _currentPwmPct += RAMP_RATE_UP * dt;
            if (_currentPwmPct > _targetPwmPct) _currentPwmPct = _targetPwmPct;
        } else if (_currentPwmPct > _targetPwmPct) {
            _currentPwmPct -= _activeRampDown * dt;
            if (_currentPwmPct < _targetPwmPct) _currentPwmPct = _targetPwmPct;
        }
    } else {
        if (_running) {
            _currentPwmPct = _targetPwmPct;   // Cascata tem controle total durante operação
        } else if (_currentPwmPct > 0.0f) {
            _currentPwmPct -= _activeRampDown * dt;
            if (_currentPwmPct < 0.0f) _currentPwmPct = 0.0f;
        }
    }

    _currentPwmPct = constrain(_currentPwmPct, 0.0f, MAX_PWM_PERCENT);
    applyPwmPct(_currentPwmPct);

    if (_currentPwmPct <= 0.0f) {
        _activeRampDown = RAMP_RATE_DOWN;
    }
}

float Motor::getSpeedFromFcemKmh() const {
    if (_fcemMaxV <= 0.0f) return 0.0f;
    float kmh = (_fcem / _fcemMaxV) * MAX_SPEED_KMH;
    return (kmh < 0.0f) ? 0.0f : kmh;
}

void Motor::_sampleElectricalSignals() {
    float vBusRaw = analogReadMilliVolts(PINO_VBUS) / 1000.0f;
    float vMotRaw = analogReadMilliVolts(PINO_VMOT) / 1000.0f;
    float vCurRaw = analogReadMilliVolts(PINO_CURR) / 1000.0f;

    _vBusRawFilt = (EMA_ALPHA * vBusRaw) + ((1.0f - EMA_ALPHA) * _vBusRawFilt);
    _vMotRawFilt = (EMA_ALPHA * vMotRaw) + ((1.0f - EMA_ALPHA) * _vMotRawFilt);
    _vCurRawFilt = (EMA_ALPHA * vCurRaw) + ((1.0f - EMA_ALPHA) * _vCurRawFilt);

    _vBus = _vBusRawFilt * K_V_BUS;
    _vMot = _vMotRawFilt * K_V_MOT;

    _iA = (_vCurRawFilt - OFFSET_I) / CONST_I;
    if (_iA < 0.0f) _iA = 0.0f;

    _fcem = _vMot - (_iA * MOTOR_R_A);
}////

void Motor::_checkOvercurrent() {
    unsigned long now = millis();

    // Trip rápido — curto/falha (corte imediato de PWM, sem esperar rampa de emergência)
    if (_iA > OVERCURRENT_TRIP_A) {
        if (_overCurrentSinceMs == 0) _overCurrentSinceMs = now;
        if (now - _overCurrentSinceMs > OVERCURRENT_TRIP_MS) {
            Serial.printf("[Motor] SOBRECORRENTE INSTANTANEA — I_a=%.2fA (limite %.1fA) por %lums — corte imediato\n",
                          _iA, OVERCURRENT_TRIP_A, now - _overCurrentSinceMs);
            _currentPwmPct = 0.0f;
            applyPwmPct(0.0f);
            faultStop(FaultCode::OVERCURRENT);
            _overCurrentSinceMs = 0;
            return;
        }
    } else {
        _overCurrentSinceMs = 0;
    }

    // Trip lento — corrente sustentada (jam mecânico/sobrecarga térmica)
    if (_iA > SUSTAINED_CURRENT_TRIP_A) {
        if (_sustainedCurrentSinceMs == 0) _sustainedCurrentSinceMs = now;
        if (now - _sustainedCurrentSinceMs > SUSTAINED_CURRENT_TRIP_MS) {
            Serial.printf("[Motor] SOBRECORRENTE SUSTENTADA — I_a=%.2fA (limite %.1fA) por %lums — possivel jam mecanico\n",
                          _iA, SUSTAINED_CURRENT_TRIP_A, now - _sustainedCurrentSinceMs);
            _currentPwmPct = 0.0f;
            applyPwmPct(0.0f);
            faultStop(FaultCode::OVERCURRENT_SUSTAINED);
            _sustainedCurrentSinceMs = 0;
            return;
        }
    } else {
        _sustainedCurrentSinceMs = 0;
    }
}

void Motor::_checkOverspeed() {
    unsigned long now = millis();
    float speedKmh = getSpeedFromFcemKmh();

    // Teto absoluto — vale em qualquer modo (aberta ou fechada), mesmo parado,
    // corte imediato de PWM (mesmo padrão da sobrecorrente).
    if (speedKmh > OVERSPEED_ABS_KMH) {
        if (_overspeedAbsSinceMs == 0) _overspeedAbsSinceMs = now;
        if (now - _overspeedAbsSinceMs > OVERSPEED_ABS_TRIP_MS) {
            Serial.printf("[Motor] SOBREVELOCIDADE ABSOLUTA — %.2f km/h (limite %.1f) por %lums — corte imediato\n",
                          speedKmh, OVERSPEED_ABS_KMH, now - _overspeedAbsSinceMs);
            _currentPwmPct = 0.0f;
            applyPwmPct(0.0f);
            faultStop(FaultCode::OVERSPEED);
            _overspeedAbsSinceMs = 0;
            return;
        }
    } else {
        _overspeedAbsSinceMs = 0;
    }

    // Fuga do alvo — só faz sentido em malha fechada rodando (compara contra o
    // setpoint rampado, não o alvo bruto, pra não disparar durante rampa normal).
    if (_running && !_openLoop) {
        if (speedKmh > _rampedSetpoint + OVERSPEED_TARGET_MARGIN_KMH) {
            if (_overspeedTargetSinceMs == 0) _overspeedTargetSinceMs = now;
            if (now - _overspeedTargetSinceMs > OVERSPEED_TARGET_TRIP_MS) {
                Serial.printf("[Motor] SOBREVELOCIDADE (fuga do alvo) — %.2f km/h vs alvo %.2f km/h por %lums — possivel carga removida\n",
                              speedKmh, _rampedSetpoint, now - _overspeedTargetSinceMs);
                _currentPwmPct = 0.0f;
                applyPwmPct(0.0f);
                faultStop(FaultCode::OVERSPEED_TARGET);
                _overspeedTargetSinceMs = 0;
                return;
            }
        } else {
            _overspeedTargetSinceMs = 0;
        }
    } else {
        _overspeedTargetSinceMs = 0;
    }
}

void Motor::_loadTuningFromNvs() {
    _prefs.begin(NVS_NAMESPACE, true);
    _spdKp    = _prefs.getFloat(NVS_KEY_PID_SPD_KP, PID_SPD_KP);
    _spdKi    = _prefs.getFloat(NVS_KEY_PID_SPD_KI, PID_SPD_KI);
    _spdKd    = _prefs.getFloat(NVS_KEY_PID_SPD_KD, PID_SPD_KD);
    _curKp    = _prefs.getFloat(NVS_KEY_PID_CUR_KP, PID_CUR_KP);
    _curKi    = _prefs.getFloat(NVS_KEY_PID_CUR_KI, PID_CUR_KI);
    _curKd    = _prefs.getFloat(NVS_KEY_PID_CUR_KD, PID_CUR_KD);
    _fcemMaxV = _prefs.getFloat(NVS_KEY_FCEM_MAX_V, FCEM_MAX_V_DEFAULT);
    _prefs.end();
    Serial.printf("[Motor] Tuning carregado — Spd(Kp=%.3f Ki=%.4f Kd=%.4f) Cur(Kp=%.3f Ki=%.4f Kd=%.4f) FcemMax=%.2fV\n",
                  _spdKp, _spdKi, _spdKd, _curKp, _curKi, _curKd, _fcemMaxV);
}

void Motor::setSpdPidGains(float kp, float ki, float kd) {
    _spdKp = kp; _spdKi = ki; _spdKd = kd;
    _spdTuningDirty = true;
    Serial.printf("[Motor] PID velocidade/FCEM atualizado — Kp=%.3f Ki=%.4f Kd=%.4f\n", _spdKp, _spdKi, _spdKd);
}

void Motor::saveSpdPidToNvs() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putFloat(NVS_KEY_PID_SPD_KP, _spdKp);
    _prefs.putFloat(NVS_KEY_PID_SPD_KI, _spdKi);
    _prefs.putFloat(NVS_KEY_PID_SPD_KD, _spdKd);
    _prefs.end();
    Serial.println("[Motor] PID velocidade/FCEM salvo na NVS");
}

void Motor::setCurPidGains(float kp, float ki, float kd) {
    _curKp = kp; _curKi = ki; _curKd = kd;
    _curTuningDirty = true;
    Serial.printf("[Motor] PID corrente atualizado — Kp=%.3f Ki=%.4f Kd=%.4f\n", _curKp, _curKi, _curKd);
}

void Motor::saveCurPidToNvs() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putFloat(NVS_KEY_PID_CUR_KP, _curKp);
    _prefs.putFloat(NVS_KEY_PID_CUR_KI, _curKi);
    _prefs.putFloat(NVS_KEY_PID_CUR_KD, _curKd);
    _prefs.end();
    Serial.println("[Motor] PID corrente salvo na NVS");
}

void Motor::setFcemMaxV(float v) {
    _fcemMaxV = v;
    Serial.printf("[Motor] FCEM_MAX_V atualizado — %.2fV\n", _fcemMaxV);
}

void Motor::saveFcemMaxToNvs() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putFloat(NVS_KEY_FCEM_MAX_V, _fcemMaxV);
    _prefs.end();
    Serial.println("[Motor] FCEM_MAX_V salvo na NVS");
}

void Motor::applyPwmPct(float pct) {
    uint32_t duty = (uint32_t)(pct / 100.0f * MOTOR_PWM_MAX_COUNTS);
    duty = constrain(duty, 0u, MAX_PWM_COUNTS);
    ledcWrite(MOTOR_PWM_CHANNEL, duty);
}

float Motor::speedToPwmPct(float kmh) const {
    if (kmh <= 0.0f) return 0.0f;
    return (kmh / MAX_SPEED_KMH) * MAX_PWM_PERCENT;
}

float Motor::_compensateBusVoltage(float pwmPct) const {
    float vBusForCalc = _vBus;
    if (vBusForCalc < V_BUS_MIN_FOR_COMPENSATION) {
        // Leitura inválida (bus ausente/ADC não assentou ainda no boot).
        if (!_openLoop) return pwmPct;   // malha fechada: não mascara sensor com problema
        vBusForCalc = V_BUS_OPEN_LOOP_FALLBACK;   // malha aberta: assume padrão e segue compensando
    }
    float ratio = V_BUS_NOMINAL / vBusForCalc;
    ratio = constrain(ratio, V_BUS_COMP_MIN_RATIO, V_BUS_COMP_MAX_RATIO);
    return pwmPct * ratio;
}
