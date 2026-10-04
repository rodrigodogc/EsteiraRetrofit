#include "Buzzer.h"

Buzzer::Buzzer(uint8_t pin) : _pin(pin) {}

void Buzzer::begin() {
    pinMode(_pin, OUTPUT);
    _off();   // estado desligado desde o boot, já respeitando a polaridade
}

void Buzzer::_on() {
    digitalWrite(_pin, BUZZER_ACTIVE_LOW ? LOW : HIGH);
}

void Buzzer::_off() {
    digitalWrite(_pin, BUZZER_ACTIVE_LOW ? HIGH : LOW);
}

void Buzzer::_beep(unsigned long durationMs) {
    _on();
    _beepActive     = true;
    _beepStartMs    = millis();
    _beepDurationMs = durationMs;
}

void Buzzer::click()         { _beep(40); }
void Buzzer::countdownTick() { _beep(90); }
void Buzzer::go()            { _beep(220); }

void Buzzer::startAlarm(bool emergency) {
    // Idempotente de propósito: se o MESMO alarme já está tocando, não reseta
    // o cronômetro do pulso (evita emendar beeps num tom contínuo se algo
    // chamar startAlarm() repetidamente, ex: entrada de emergência instável).
    if (_alarmActive && _alarmEmergency == emergency) return;

    _alarmActive       = true;
    _alarmEmergency    = emergency;
    _alarmCycleStartMs = millis();
}

void Buzzer::stopAlarm() {
    _alarmActive = false;
    if (!_beepActive) _off();
}

void Buzzer::update() {
    unsigned long now = millis();

    // Beep temporizado (click/countdownTick/go) — desliga sozinho ao expirar,
    // a menos que um alarme esteja tocando por cima (alarme tem prioridade).
    if (_beepActive && (now - _beepStartMs >= _beepDurationMs)) {
        _beepActive = false;
        if (!_alarmActive) _off();
    }

    if (!_alarmActive) return;

    unsigned long interval = _alarmEmergency ? 300 : 800;   // período do pulso
    unsigned long onTime   = 150;                            // duração do "ligado" dentro do período
    unsigned long phase    = (now - _alarmCycleStartMs) % interval;

    if (phase < onTime) _on();
    else                _off();
}
