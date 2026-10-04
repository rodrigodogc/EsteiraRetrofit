#include "Motor.h"

Motor::Motor(SpeedSensor& sensor)
    : _sensor(sensor),
      _pid(&_pidInput, &_pidOutput, &_pidSetpoint,
           PID_KP, PID_KI, PID_KD, DIRECT) {}

void Motor::begin() {
    ledcSetup(MOTOR_PWM_CHANNEL, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_RESOLUTION);
    ledcAttachPin(MOTOR_PWM_PIN, MOTOR_PWM_CHANNEL);
    ledcWrite(MOTOR_PWM_CHANNEL, 0);

    _loadPidFromNvs();   // carrega KP/KI/KD da NVS (ou usa defaults de Config.h)

    _pid.SetMode(AUTOMATIC);
    _pid.SetTunings(_kp, _ki, _kd);
    // Saída do PID é uma correção em torno do feedforward: ±50% do range total
    _pid.SetOutputLimits(-MAX_PWM_PERCENT * 0.5f, MAX_PWM_PERCENT * 0.5f);
    _pid.SetSampleTime(PID_INTERVAL_MS);

    _currentPwmPct  = 0.0f;
    _targetPwmPct   = 0.0f;
    _running        = false;
    _lastHeartbeatMs = millis();   // carência inicial para cliente conectar
}

void Motor::start() {
    if (_running) return;
    clearFault();

    // Garante velocidade mínima ao iniciar
    if (_targetSpeedKmh < MIN_SPEED_KMH) {
        _targetSpeedKmh = MIN_SPEED_KMH;
    }

    _running         = true;
    _startedAtMs     = millis();
    resetHeartbeat();   // carência para o cliente enviar o primeiro heartbeat

    // Reinicia PID sem windup acumulado
    _pid.SetMode(MANUAL);
    _pidOutput = 0.0;
    _pid.SetMode(AUTOMATIC);

    _rampedSetpoint = 0.0f;   // sempre sobe do zero — rampa no setpoint controla aceleração
    _sensor.reset();
    Serial.println("[Motor] Start");
}

void Motor::stop() {
#if OPEN_LOOP_MODE
    _running        = false;
    _activeRampDown = RAMP_RATE_DOWN;
    _targetPwmPct   = 0.0f;
    _rampedSetpoint = 0.0f;
    _sensor.reset();
    Serial.println("[Motor] Stop");
#else
    // Malha fechada: mantém _running=true e desacelera via rampedSetpoint → 0
    // O PID rastreia a queda suavemente; update() corta ao rampedSetpoint chegar a zero
    _targetSpeedKmh = 0.0f;
    _stopping       = true;
    Serial.println("[Motor] Stop (controlled decel)");
#endif
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
    float dt = (now - _lastUpdateMs) / 1000.0f;
    if (dt < 0.01f) return;
    _lastUpdateMs = now;

    // Heartbeat centralizado — qualquer interface (WS ou BLE) pode resetar o timer
    if (_running && !isHeartbeatOk()) {
        Serial.println("[Motor] Heartbeat timeout — fault stop");
        faultStop(FaultCode::HEARTBEAT_TIMEOUT);
        return;
    }

    if (_running) {
#if OPEN_LOOP_MODE
        // Malha aberta: converte velocidade alvo → duty-cycle; rampa de PWM abaixo cuida da subida
        _targetPwmPct = speedToPwmPct(_targetSpeedKmh);
#else
        // Detecta perda de encoder — inibe durante desaceleração (velocidade cai intencionalmente)
        if (!_stopping && (now - _startedAtMs) > (ENCODER_TIMEOUT_MS + 2000UL) && !_sensor.isMoving()) {
            faultStop(FaultCode::ENCODER_LOSS);
            return;
        }

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

        // Rampa no setpoint de velocidade (km/h/s) — PID controla PWM livremente sem conflito
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

        _pidSetpoint = _rampedSetpoint;
        _pidInput    = _sensor.getSpeedKmh();
        if (_pid.Compute()) {
            // Feedforward estima o PWM linear para o setpoint atual;
            // PID corrige apenas o desvio residual (pode ser negativo para frear overshoot)
            _targetPwmPct = speedToPwmPct(_rampedSetpoint) + (float)_pidOutput;
        }
#endif
        _targetPwmPct = constrain(_targetPwmPct, 0.0f, MAX_PWM_PERCENT);
    } else {
        _targetPwmPct = 0.0f;
    }

    // ── Aplicação de PWM ─────────────────────────────────────────────────────
    // Malha aberta : rampa de PWM na subida e na descida
    // Malha fechada: PID aplica direto quando rodando; rampa só ao parar (stop/emergency)
#if OPEN_LOOP_MODE
    if (_currentPwmPct < _targetPwmPct) {
        _currentPwmPct += RAMP_RATE_UP * dt;
        if (_currentPwmPct > _targetPwmPct) _currentPwmPct = _targetPwmPct;
    } else if (_currentPwmPct > _targetPwmPct) {
        _currentPwmPct -= _activeRampDown * dt;
        if (_currentPwmPct < _targetPwmPct) _currentPwmPct = _targetPwmPct;
    }
#else
    if (_running) {
        _currentPwmPct = _targetPwmPct;   // PID tem controle total durante operação
    } else if (_currentPwmPct > 0.0f) {
        _currentPwmPct -= _activeRampDown * dt;
        if (_currentPwmPct < 0.0f) _currentPwmPct = 0.0f;
    }
#endif

    _currentPwmPct = constrain(_currentPwmPct, 0.0f, MAX_PWM_PERCENT);
    applyPwmPct(_currentPwmPct);

    if (_currentPwmPct <= 0.0f) {
        _activeRampDown = RAMP_RATE_DOWN;
    }
}

void Motor::_loadPidFromNvs() {
    _prefs.begin(NVS_NAMESPACE, true);
    _kp = _prefs.getFloat(NVS_KEY_PID_KP, PID_KP);
    _ki = _prefs.getFloat(NVS_KEY_PID_KI, PID_KI);
    _kd = _prefs.getFloat(NVS_KEY_PID_KD, PID_KD);
    _prefs.end();
    Serial.printf("[Motor] PID carregado — Kp=%.3f Ki=%.4f Kd=%.4f\n", _kp, _ki, _kd);
}

void Motor::setPidGains(float kp, float ki, float kd) {
    _kp = kp; _ki = ki; _kd = kd;
    _pid.SetTunings(_kp, _ki, _kd);
    Serial.printf("[Motor] PID atualizado — Kp=%.3f Ki=%.4f Kd=%.4f\n", _kp, _ki, _kd);
}

void Motor::savePidToNvs() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putFloat(NVS_KEY_PID_KP, _kp);
    _prefs.putFloat(NVS_KEY_PID_KI, _ki);
    _prefs.putFloat(NVS_KEY_PID_KD, _kd);
    _prefs.end();
    Serial.println("[Motor] PID salvo na NVS");
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
