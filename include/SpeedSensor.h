#pragma once
#include <Arduino.h>
#include "Config.h"

class SpeedSensor {
public:
    SpeedSensor(uint8_t pin = ENCODER_PIN);

    void  begin();
    void  update();          // chamar no loop principal

    float getSpeedKmh() const { return _speedKmh; }
    float getRPM()      const { return _rpm; }
    bool  isMoving()    const { return _speedKmh > 0.01f; }

    void reset();              // zera leituras imediatamente (chamar ao parar motor)

    // Chamado pela ISR — deve ser público
    void IRAM_ATTR onPulse();

    // DEBUG — contagem incremental de pulsos válidos (pós-debounce) desde o boot
    uint32_t getPulseCount()    const { return _pulseCount; }
    uint32_t getLastPeriodUs()  const { return (uint32_t)_periodUs; }

private:
    uint8_t _pin;
    float   _speedKmh  = 0.0f;
    float   _rpm       = 0.0f;

    // variáveis compartilhadas com ISR
    volatile unsigned long _lastPulseUs  = 0;
    volatile unsigned long _periodUs     = 0;
    volatile bool          _newPulse     = false;
    volatile uint32_t      _pulseCount   = 0;    // DEBUG

    unsigned long          _lastMoveMs   = 0;
    unsigned long          _lastSerialMs = 0;    // DEBUG — throttle serial print
};
