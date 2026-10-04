#include "Button.h"

Button::Button(uint8_t pin) : _pin(pin) {}

void Button::begin() {
    pinMode(_pin, INPUT_PULLUP);
    _rawState    = (digitalRead(_pin) == LOW);
    _stableState = _rawState;
}

void Button::update() {
    unsigned long now = millis();
    bool raw = (digitalRead(_pin) == LOW);

    if (raw != _rawState) {
        _rawState     = raw;
        _lastChangeMs = now;
    }

    if ((now - _lastChangeMs) > BUTTON_DEBOUNCE_MS && _rawState != _stableState) {
        _stableState = _rawState;

        if (_stableState) {
            // borda de pressionar
            _pressedFlag  = true;
            _pressStartMs = now;
            _repeating    = false;
        } else {
            // borda de soltar
            _repeating = false;
        }
    }

    if (_stableState && !_repeating && (now - _pressStartMs) > BUTTON_REPEAT_DELAY_MS) {
        _repeating    = true;
        _lastRepeatMs = now;
        _repeatFlag   = true;
    } else if (_stableState && _repeating && (now - _lastRepeatMs) >= BUTTON_REPEAT_INTERVAL_MS) {
        _lastRepeatMs = now;
        _repeatFlag   = true;
    }
}

bool Button::wasPressed() {
    if (_pressedFlag) {
        _pressedFlag = false;
        return true;
    }
    return false;
}

bool Button::isRepeating() {
    if (_repeatFlag) {
        _repeatFlag = false;
        return true;
    }
    return false;
}
