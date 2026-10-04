#include "SpeedSensor.h"

// Ponteiro global para a ISR acessar a instância (única instância suportada)
static SpeedSensor* _sensorInstance = nullptr;

static void IRAM_ATTR encoderISR() {
    if (_sensorInstance) _sensorInstance->onPulse();
}

SpeedSensor::SpeedSensor(uint8_t pin) : _pin(pin) {}

void SpeedSensor::begin() {
    _sensorInstance = this;
    pinMode(_pin, INPUT);
    // Encoder óptico não está mais fisicamente disponível no hardware atual —
    // controle de velocidade já é 100% sensorless via FCEM (ver Motor.cpp).
    // Interrupção desabilitada de propósito: um pino flutuante sem encoder
    // gera pulsos espúrios por ruído, poluindo _speedKmh/_rpm com lixo em vez
    // de simplesmente ficar em zero. update()/getSpeedKmh()/getRPM() continuam
    // funcionando normalmente, só nunca recebem pulso nenhum.
    // attachInterrupt(digitalPinToInterrupt(_pin), encoderISR, FALLING);
}

void SpeedSensor::reset() {
    noInterrupts();
    _newPulse    = false;
    _periodUs    = 0;    // reseta baseline da plausibilidade
    _lastPulseUs = 0;
    interrupts();
    _lastMoveMs = 0;   // força timeout imediato → speed = 0 no próximo update()
    _speedKmh   = 0.0f;
    _rpm        = 0.0f;
}

void IRAM_ATTR SpeedSensor::onPulse() {
    unsigned long nowUs  = micros();
    unsigned long period = nowUs - _lastPulseUs;

    // 1) Debounce: rejeita pulsos mais rápidos que o período mínimo físico
    if (period < ENCODER_DEBOUNCE_MIN_US) return;

    _lastPulseUs = nowUs;   // referência atualizada apenas após passar no debounce

    // 2) Gap/timeout: período muito longo → sensor teve hiato; reinicia baseline
    //    sem este reset, o próximo pulso real seria rejeitado pela checagem de plausibilidade
    if (period > ENCODER_MAX_PERIOD_US) {
        _periodUs = 0;
        return;
    }

    // 3) Plausibilidade: rejeita se a velocidade implicada pulou mais de 2× de repente
    //    (ruído EMI pode gerar pulsos espaçados > debounce mas absurdamente rápidos vs. real)
    if (_periodUs > 0 && period < (_periodUs >> 1)) return;

    _periodUs = period;
    _newPulse  = true;
    _pulseCount++;   // DEBUG
}

void SpeedSensor::update() {
    unsigned long nowMs = millis();

    if (_newPulse) {
        _newPulse = false;
        _lastMoveMs = nowMs;

        float periodSec = _periodUs / 1000000.0f;

        // Velocidade bruta do pulso atual
        float rawKmh = constrain((METERS_PER_PULSE / periodSec) * 3.6f, 0.0f, MAX_SPEED_KMH);

        // Filtro IIR (passa-baixa): atenua picos residuais de ruído sem afetar muito a resposta
        _speedKmh = ENCODER_SPEED_ALPHA * rawKmh + (1.0f - ENCODER_SPEED_ALPHA) * _speedKmh;

        _rpm = (1.0f / periodSec) / ENCODER_PULSES_PER_REV * 60.0f;
    }

    // Timeout: sem pulso → velocidade zero
    if ((nowMs - _lastMoveMs) > ENCODER_TIMEOUT_MS) {
        _speedKmh = 0.0f;
        _rpm      = 0.0f;
    }

    // DEBUG — imprime na serial a cada 1 s
    if ((nowMs - _lastSerialMs) >= 1000) {
        _lastSerialMs = nowMs;
        Serial.printf("[ENC] pulsos=%lu  vel=%.2f km/h  rpm=%.0f  periodo=%lu us\n",
                      (unsigned long)_pulseCount, _speedKmh, _rpm, (unsigned long)_periodUs);
    }
}
