#pragma once
#include <Arduino.h>
#include "Config.h"

// Indicador de conexão ESP-NOW: 3 piscadas rápidas a cada LED_CYCLE_MS
// quando conectado (heartbeat fresco do peer), pisca lento contínuo quando
// não. Padrão idêntico ao usado no painel superior (FirmwarePainel).
class StatusLed {
public:
    StatusLed(uint8_t pin, bool activeLow);

    void begin();
    void update(bool connected);   // chamar a cada loop

private:
    uint8_t _pin;
    bool    _activeLow;

    unsigned long _cycleStartMs  = 0;
    bool          _lastConnected = false;

    void _write(bool on);
};
