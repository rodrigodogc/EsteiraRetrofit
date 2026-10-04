#include "SessionTracker.h"

void SessionTracker::start() {
    _active        = true;
    _paused        = false;
    _distanceKm    = 0.0f;
    _elapsedBaseMs = 0;
    _runStartMs    = millis();
    _lastSampleMs  = 0;   // 0 = descarta o primeiro dt (evita salto por tempo parado antes do start)
}

void SessionTracker::pause() {
    if (_active && !_paused) {
        _elapsedBaseMs += millis() - _runStartMs;
        _paused = true;
    }
}

void SessionTracker::resume() {
    if (_active && _paused) {
        _runStartMs   = millis();
        _paused       = false;
        _lastSampleMs = 0;   // idem — descarta o dt do tempo pausado
    }
}

void SessionTracker::onTelemetry(float speedKmh, bool motorRunning) {
    unsigned long now = millis();

    if (_active && !_paused && motorRunning && _lastSampleMs != 0) {
        float dtHours = (now - _lastSampleMs) / 3600000.0f;
        _distanceKm += speedKmh * dtHours;
    }

    _lastSampleMs = now;
}

unsigned long SessionTracker::getElapsedMs() const {
    if (!_active) return 0;
    if (_paused)  return _elapsedBaseMs;
    return _elapsedBaseMs + (millis() - _runStartMs);
}

void SessionTracker::getElapsedMMSS(uint8_t& minutes, uint8_t& seconds) const {
    unsigned long totalSec = getElapsedMs() / 1000;
    unsigned long m        = totalSec / 60;
    minutes = (uint8_t)(m > 99 ? 99 : m);
    seconds = (uint8_t)(totalSec % 60);
}
