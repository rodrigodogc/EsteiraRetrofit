#include "Incline.h"

Incline::Incline(uint8_t pinUp, uint8_t pinDown)
    : _pinUp(pinUp), _pinDown(pinDown) {}

void Incline::begin() {
    pinMode(_pinUp,   OUTPUT);
    pinMode(_pinDown, OUTPUT);
    relayOff();
}

void Incline::startHoming() {
    Serial.println("[Incline] Homing started");
    _homingDone = false;
    _currentDeg = INCLINE_MAX_DEG;   // assume posição pior caso
    _targetDeg  = 0.0f;
    startMove(InclineState::HOMING);
}

void Incline::setTargetDegrees(float deg) {
    _targetDeg = constrain(deg, INCLINE_MIN_DEG, INCLINE_MAX_DEG);
    if (isBusy()) return;   // não interrompe homing em andamento

    float diff = _targetDeg - _currentDeg;
    if (fabsf(diff) < 0.1f) return;   // já está no alvo

    startMove(diff > 0 ? InclineState::MOVING_UP : InclineState::MOVING_DOWN);
}

void Incline::update() {
    if (_state == InclineState::IDLE) return;

    updateEstimate();

    unsigned long elapsed = millis() - _moveStartMs;

    if (_state == InclineState::HOMING) {
        // Para quando atingir tempo máximo de homing (fim de curso físico já desligou o relé,
        // mas garantimos parada por software também)
        if (elapsed >= INCLINE_HOMING_MS) {
            relayOff();
            _currentDeg = 0.0f;
            _targetDeg  = 0.0f;   // descarta comandos enviados durante o homing
            _state       = InclineState::IDLE;
            _homingDone  = true;
            Serial.println("[Incline] Homing done");
        }
        return;
    }

    // MOVING_UP / MOVING_DOWN: para quando chega no alvo estimado
    if (_state == InclineState::MOVING_UP) {
        if (_currentDeg >= _targetDeg) {
            relayOff();
            _currentDeg = _targetDeg;
            _state      = InclineState::IDLE;
        }
    } else if (_state == InclineState::MOVING_DOWN) {
        if (_currentDeg <= _targetDeg) {
            relayOff();
            _currentDeg = _targetDeg;
            _state      = InclineState::IDLE;
        }
    }
}

// ─── Privados ─────────────────────────────────────────────────────────────────

void Incline::relayOff() {
    digitalWrite(_pinUp,   LOW);
    digitalWrite(_pinDown, LOW);
}

void Incline::startMove(InclineState dir) {
    relayOff();
    _state          = dir;
    _moveStartMs    = millis();
    _degAtMoveStart = _currentDeg;

    if (dir == InclineState::MOVING_UP) {
        digitalWrite(_pinUp,   HIGH);
    } else {
        // MOVING_DOWN e HOMING ambos acionam relé DOWN
        digitalWrite(_pinDown, HIGH);
    }
}

void Incline::updateEstimate() {
    float elapsed = (millis() - _moveStartMs) / 1000.0f;
    float delta   = INCLINE_DEG_PER_SEC * elapsed;

    if (_state == InclineState::MOVING_UP) {
        _currentDeg = _degAtMoveStart + delta;
        _currentDeg = constrain(_currentDeg, INCLINE_MIN_DEG, INCLINE_MAX_DEG);
    } else if (_state == InclineState::MOVING_DOWN || _state == InclineState::HOMING) {
        _currentDeg = _degAtMoveStart - delta;
        _currentDeg = constrain(_currentDeg, INCLINE_MIN_DEG, INCLINE_MAX_DEG);
    }
}
