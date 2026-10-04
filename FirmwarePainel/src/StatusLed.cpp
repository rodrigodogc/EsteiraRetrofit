#include "StatusLed.h"

StatusLed::StatusLed(uint8_t pin, bool activeLow) : _pin(pin), _activeLow(activeLow) {}

void StatusLed::begin() {
    pinMode(_pin, OUTPUT);
    _write(false);
}

void StatusLed::_write(bool on) {
    bool level = _activeLow ? !on : on;
    digitalWrite(_pin, level ? HIGH : LOW);
}

void StatusLed::update(bool connected) {
    unsigned long now = millis();

    if (connected != _lastConnected) {
        _lastConnected = connected;
        _cycleStartMs  = now;   // reinicia o padrão exatamente na troca de estado
    }

    if (connected) {
        unsigned long elapsed     = (now - _cycleStartMs) % LED_CYCLE_MS;
        unsigned long blinkPeriod = LED_BLINK_ON_MS + LED_BLINK_OFF_MS;
        bool on = false;
        if (elapsed < (unsigned long)LED_BLINK_COUNT * blinkPeriod) {
            on = (elapsed % blinkPeriod) < LED_BLINK_ON_MS;
        }
        _write(on);
    } else {
        unsigned long elapsed = (now - _cycleStartMs) % (LED_SLOW_ON_MS + LED_SLOW_OFF_MS);
        _write(elapsed < LED_SLOW_ON_MS);
    }
}
